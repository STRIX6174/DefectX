#include "application/pipeline_runner.h"
#include "application/log_manager.h"
#include "application/result_aggregator.h"
#include "application/report_generator.h"

#include "algorithm/preprocess_operators.h"
#include "algorithm/defect_algorithms.h"
#include "algorithm/result_fusion.h"
#include "model/defect_class.h"

#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace qf {
namespace application {

// ---------------------------------------------------------------------------
// ParallelClassifier
// ---------------------------------------------------------------------------
ParallelClassifier::~ParallelClassifier() {
    shutdown();
}

// ============================================================
// 六线程创立 configure：按类别数起 worker 线程（1~6，默认 6）。
//   数学原理：每个 worker 是"生产者-消费者"模型里的消费者——条件变量
//     startCv_ 唤醒后取走当前灰度图（浅拷贝，引用计数保活），算完写入
//     results_[index]；generation_ 每次 classify 自增，作为"代际标签"
//     丢弃慢线程的过期结果，doneCount_ 判定六类是否全部完成。
//   选择原因：六类算法彼此独立、无数据依赖，天然可并行；"一类别一线程"
//     直接映射六类、避免超订；std::thread + 条件变量比 std::async/线程池
//     更可控（可精确 join/shutdown、无任务队列开销）。
// ============================================================
void ParallelClassifier::configure(int threadCount, const model::PipelineParams& params) {
    shutdown();

    if (threadCount < 1) threadCount = 1;
    if (threadCount > 6) threadCount = 6;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = false;
        generation_ = 0;
        doneCount_ = 0;
    }

    for (int i = 0; i < threadCount; ++i) {
        model::DefectClass type = model::AllDefectClasses[i];
        auto it = params.algorithms.find(type);
        model::AlgorithmParams ap =
            (it != params.algorithms.end()) ? it->second : model::AlgorithmParams{};
        workers_.emplace_back(&ParallelClassifier::workerLoop, this, i, type, ap);
    }

    LogManager::instance().info("ParallelClassifier started with " +
                                std::to_string(threadCount) + " workers");
}

// ============================================================
// workerLoop：单个类别工作线程的主循环（与 configure 的创立对应）。
//   阻塞等待 startCv_ 唤醒 → 运行本类算法 → 代际一致时写回结果并通知
//   doneCv_；stop_ 置位则退出，代际落后则丢弃结果。
// ============================================================
void ParallelClassifier::workerLoop(int index, model::DefectClass type,
                                    model::AlgorithmParams params) {
    int lastGen = 0;
    while (true) {
        cv::Mat gray;
        int gen = 0;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            startCv_.wait(lk, [this, &lastGen]() {
                return stop_ || generation_ > lastGen;
            });
            if (stop_) return;
            lastGen = generation_;
            gen = lastGen;
            gray = currentGray_;
        }

        // 运行该类别算法（共享输入为浅拷贝，数据引用计数保证存活）
        model::DefectScore score = algorithm::runSingleAlgorithm(type, gray, params);

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (gen == generation_ && !stop_) {
                results_[index] = score;
                ++doneCount_;
                doneCv_.notify_one();
            }
        }
    }
}

std::array<model::DefectScore, 6> ParallelClassifier::classify(const cv::Mat& gray, int timeoutMs) {
    std::array<model::DefectScore, 6> out{};
    if (workers_.empty()) {
        return out;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        currentGray_ = gray;  // 浅拷贝，引用计数保证底层数据存活
        for (size_t i = 0; i < out.size(); ++i) {
            out[i].defectClass = model::AllDefectClasses[i];
        }
        results_.fill(model::DefectScore{});
        doneCount_ = 0;
        ++generation_;
    }
    startCv_.notify_all();

    {
        std::unique_lock<std::mutex> lk(mtx_);
        bool finished = doneCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this]() {
            return doneCount_ >= static_cast<int>(workers_.size());
        });
        if (!finished) {
            LogManager::instance().warn("Parallel classify timed out");
        }
        out = results_;
    }
    return out;
}

void ParallelClassifier::shutdown() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
    }
    startCv_.notify_all();
    for (auto& th : workers_) {
        if (th.joinable()) th.join();
    }
    workers_.clear();
}

// ---------------------------------------------------------------------------
// PipelineRunner
// ---------------------------------------------------------------------------
PipelineRunner::PipelineRunner()
    : cancelToken_(std::make_shared<CancelToken>()) {}

PipelineRunner::~PipelineRunner() {
    classifier_.shutdown();
}

void PipelineRunner::setProgressCallback(std::shared_ptr<RunProgressCallback> cb) {
    progressCb_ = std::move(cb);
}

void PipelineRunner::setCancelToken(CancelTokenPtr token) {
    cancelToken_ = std::move(token);
}

void PipelineRunner::setConfig(const model::PipelineParams& params) {
    if (running_.load()) {
        LogManager::instance().warn("Cannot change config while pipeline is running");
        return;
    }
    params_ = params;
    classifier_.configure(params.gate.parallelThreads, params_);
}

void PipelineRunner::setImageFolder(const std::string& folderPath) {
    imageFolder_ = folderPath;
}

std::vector<std::string> PipelineRunner::scanImageFiles(const std::string& folder) {
    std::vector<std::string> files;
    if (!fs::exists(folder) || !fs::is_directory(folder)) return files;

    const std::vector<std::string> exts = {".jpg", ".jpeg", ".png", ".bmp"};

    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const auto& valid : exts) {
            if (ext == valid) {
                files.push_back(entry.path().string());
                break;
            }
        }
    }
    std::sort(files.begin(), files.end());

    LogManager::instance().info("Scanned " + std::to_string(files.size()) +
                                " images from " + folder);
    return files;
}

model::ClassificationResult PipelineRunner::processSingleImage(const cv::Mat& img) {
    model::ClassificationResult result;

    // 预处理：灰度 → 标准尺寸 → 可选增强
    cv::Mat gray = algorithm::preprocess(img, params_.preprocess);
    if (gray.empty()) {
        result.predicted = std::nullopt;
        return result;
    }

    // 六类并行识别
    auto scores = classifier_.classify(gray, params_.gate.timeoutPerImageMs);

    // 汇总
    result = algorithm::fuseScores(scores, params_.decision);
    return result;
}

void PipelineRunner::run() {
    if (running_.load()) {
        LogManager::instance().warn("Pipeline already running");
        return;
    }

    running_ = true;
    cancelToken_->reset();

    ResultAggregator aggregator;

    auto files = scanImageFiles(imageFolder_);
    int total = static_cast<int>(files.size());

    for (int i = 0; i < total; ++i) {
        if (cancelToken_->isCancelled()) {
            break;
        }

        const auto& filePath = files[i];

        auto [img, err] = imageLoader_.load(filePath);
        if (img.empty()) {
            model::ImageEntry entry = imageLoader_.buildEntry(filePath, img);
            entry.status = model::ImageStatus::LoadFailed;
            entry.errorCode = (err != model::ErrorCode::None) ? err : model::ErrorCode::DecodeFailed;
            if (progressCb_) progressCb_->onError(entry, entry.errorCode);
            aggregator.addError(entry);
            continue;
        }

        model::ImageEntry entry = imageLoader_.buildEntry(filePath, img);
        entry.status = model::ImageStatus::Processing;

        auto t0 = std::chrono::steady_clock::now();
        model::ClassificationResult result = processSingleImage(img);
        auto t1 = std::chrono::steady_clock::now();
        result.cycleTimeMs =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        entry.status = model::ImageStatus::Classified;
        aggregator.addSuccess(entry, result);

        if (progressCb_) {
            progressCb_->onImageDone(i + 1, total, entry, result);
        }
    }

    bool isPartial = cancelToken_->isCancelled();

    lastReport_ = aggregator.finalize(isPartial);

    LogManager::instance().info("Pipeline finished: " +
                                std::to_string(lastReport_.successCount) + " ok, " +
                                std::to_string(lastReport_.failedCount) + " failed");

    if (progressCb_) {
        progressCb_->onFinished(lastReport_);
    }

    running_ = false;
}

}  // namespace application
}  // namespace qf