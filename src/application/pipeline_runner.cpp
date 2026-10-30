#include "application/pipeline_runner.h"
#include "application/log_manager.h"

#include <filesystem>
#include <chrono>
#include <algorithm>
#include <future>
#include <cassert>

namespace fs = std::filesystem;

namespace qf {
namespace application {

PipelineRunner::PipelineRunner() {
    cancelToken_ = std::make_shared<CancelToken>();
}

PipelineRunner::~PipelineRunner() {
    stopWorkers();
}

void PipelineRunner::stopWorkers() {
    {
        std::lock_guard<std::mutex> lk(workMutex_);
        workersActive_ = false;
    }
    workCv_.notify_all();
    for (auto& th : workers_) th.join();
    workers_.clear();
}

void PipelineRunner::setProgressCallback(std::shared_ptr<RunProgressCallback> cb) {
    progressCb_ = std::move(cb);
}

void PipelineRunner::setCancelToken(CancelTokenPtr token) {
    cancelToken_ = std::move(token);
}

void PipelineRunner::setQcEnabled(bool enabled) {
    qcEnabled_.store(enabled, std::memory_order_release);
}

void PipelineRunner::setParallelEnabled(bool enabled) {
    parallelEnabled_.store(enabled, std::memory_order_release);
}

void PipelineRunner::setConfig(const model::PipelineParams& params) {
    if (running_) {
        LogManager::instance().warn("Cannot change config while pipeline is running");
        return;
    }

    params_ = params;

    qcEnabled_.store(params.qcEnabled, std::memory_order_release);
    timeoutMs_ = params.timeoutPerImageMs > 0 ? params.timeoutPerImageMs : 5000;

    qcOp_ = algorithm::createQcOperator(params_);
    defectOps_.clear();
    for (auto dt : model::AllDefectTypes) {
        auto it = params_.defectParams.find(dt);
        auto dp = (it != params_.defectParams.end()) ? it->second : model::DefectParam{};
        defectOps_.push_back(algorithm::createDefectOperator(dt, dp));
    }
    decision_ = algorithm::createDecisionLogic();

    assert(defectOps_.size() == model::AllDefectTypes.size());

    stopWorkers();

    workersActive_ = true;
    size_t N = defectOps_.size();
    for (size_t t = 0; t < N; ++t) {
        workers_.emplace_back([this, N]() {
            while (true) {
                std::unique_lock<std::mutex> lk(workMutex_);
                workCv_.wait(lk, [this]() {
                    return currentWork_.img != nullptr || !workersActive_;
                });
                if (!workersActive_) return;

                auto* img    = currentWork_.img;
                auto* result = currentWork_.result;

                lk.unlock();

                while (true) {
                    size_t i;
                    {
                        std::lock_guard<std::mutex> lk2(workMutex_);
                        if (currentWork_.img == nullptr) break;
                        i = opsDone_;
                        if (i >= N) break;
                        ++opsDone_;
                    }
                    try {
                        auto defs = defectOps_[i]->detect(*img);
                        result->perType[i].defects = std::move(defs);
                        result->perType[i].status =
                            decision_->decidePerType(result->perType[i].defects);
                    } catch (const std::exception& e) {
                        LogManager::instance().error(
                            "Operator " + std::to_string(i) + " crashed: " + e.what());
                        result->perType[i].status = model::ImageStatus::LoadFailed;
                    }
                }

                doneCv_.notify_one();
            }
        });
    }

    LogManager::instance().info("Pipeline configured with " +
        std::to_string(defectOps_.size()) + " defect operators");
}

void PipelineRunner::setImageFolder(const std::string& folderPath) {
    imageFolder_ = folderPath;
}

void PipelineRunner::setGroundTruthPath(const std::string& gtPath) {
    gtPath_ = gtPath;
}

void PipelineRunner::setOutputDir(const std::string& outputDir) {
    outputDir_ = outputDir;
}

void PipelineRunner::setSaveMarkedImages(bool save) {
    saveMarked_ = save;
}

std::vector<std::string> PipelineRunner::scanImageFiles(const std::string& folder) {
    std::vector<std::string> files;
    if (!fs::exists(folder) || !fs::is_directory(folder)) return files;

    const std::vector<std::string> exts = {".jpg", ".jpeg", ".png", ".bmp"};

    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
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

model::ImageEntry PipelineRunner::buildImageEntry(
    const std::string& filePath, const cv::Mat& img) {
    return imageLoader_.buildEntry(filePath, img);
}

model::InspectionResult PipelineRunner::processSingleImage(const cv::Mat& img) {
    model::InspectionResult result;

    if (qcEnabled_.load(std::memory_order_acquire)) {
        if (!qcOp_) {
            LogManager::instance().warn(
                "QC is enabled but qcOp_ is null -- QC will be skipped. "
                "Check config.json QC parameters.");
        } else {
            result.qcResult = qcOp_->evaluate(img);
            result.qcExecuted = true;
            if (!result.qcResult.passed) {
                result.finalStatus = model::ImageStatus::Invalid_Qc;
                return result;
            }
        }
    }

    result.perType.resize(model::AllDefectTypes.size());
    for (size_t i = 0; i < model::AllDefectTypes.size(); ++i) {
        result.perType[i].type = model::AllDefectTypes[i];
    }

    assert(defectOps_.size() == result.perType.size());
    for (size_t i = 0; i < defectOps_.size(); ++i) {
        assert(defectOps_[i]->type() == result.perType[i].type);
    }

    if (parallelEnabled_.load(std::memory_order_acquire) && workersActive_.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> lk(workMutex_);
            currentWork_ = {&img, &result};
            opsDone_ = 0;
        }
        workCv_.notify_all();

        {
            std::unique_lock<std::mutex> lk(workMutex_);
            bool finished = doneCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs_),
                [this]() { return opsDone_ >= defectOps_.size(); });
            if (!finished) {
                for (size_t i = 0; i < defectOps_.size(); ++i) {
                    if (result.perType[i].status == model::ImageStatus::Pending) {
                        result.perType[i].status = model::ImageStatus::LoadFailed;
                    }
                }
                LogManager::instance().error("Parallel detection timed out after " +
                    std::to_string(timeoutMs_) + "ms");
            }
            currentWork_.img = nullptr;
        }
    } else {
        for (size_t i = 0; i < defectOps_.size(); ++i) {
            auto defs = defectOps_[i]->detect(img);
            result.perType[i].defects = std::move(defs);
            result.perType[i].status =
                decision_->decidePerType(result.perType[i].defects);
        }
    }

    bool anyDefective = false;
    for (auto& pt : result.perType) {
        if (pt.status == model::ImageStatus::Defective) {
            anyDefective = true;
            break;
        }
    }
    result.finalStatus = anyDefective ? model::ImageStatus::Defective
                                      : model::ImageStatus::Pass;
    return result;
}

void PipelineRunner::run() {
    if (!qcOp_ || defectOps_.empty() || !decision_) {
        LogManager::instance().error("Pipeline not configured. Call setConfig() first.");
        if (progressCb_) {
            progressCb_->onFinished(model::RunReport{});
        }
        return;
    }

    running_ = true;
    auto startTime = std::chrono::steady_clock::now();
    cancelToken_->reset();

    auto files = scanImageFiles(imageFolder_);
    if (files.empty()) {
        LogManager::instance().warn("No image files found in: " + imageFolder_);
        if (progressCb_) {
            progressCb_->onFinished(model::RunReport{});
        }
        running_ = false;
        return;
    }

    GroundTruthParser gtParser;
    std::map<std::string, model::GroundTruthRecord> gtLookup;
    bool hasGt = false;

    if (!gtPath_.empty() && fs::exists(gtPath_)) {
        auto gtResult = gtParser.parse(gtPath_, files);
        if (!gtResult.errorMsg.has_value()) {
            gtLookup = gtParser.buildLookup(gtResult);
            hasGt = !gtLookup.empty();
        }
    }

    if (!outputDir_.empty()) {
        fs::create_directories(outputDir_);
    }

    ResultAggregator aggregator(gtLookup, hasGt);
    int total = static_cast<int>(files.size());

    for (int i = 0; i < total; ++i) {
        if (cancelToken_->isCancelled()) break;

        auto t0 = std::chrono::steady_clock::now();

        auto [img, errorCode] = imageLoader_.load(files[i]);
        auto entry = buildImageEntry(files[i], img);

        model::InspectionResult ir;

        if (errorCode != model::ErrorCode::None) {
            entry.errorCode = errorCode;
            entry.status    = model::ImageStatus::LoadFailed;
            ir.finalStatus  = model::ImageStatus::LoadFailed;
        } else {
            auto future = std::async(std::launch::async,
                [this, img]() { return processSingleImage(img); });

            auto status = future.wait_for(std::chrono::milliseconds(timeoutMs_));
            if (status == std::future_status::ready) {
                ir = future.get();
            } else {
                entry.errorCode = model::ErrorCode::DefectTimeout;
                entry.status    = model::ImageStatus::LoadFailed;
                ir.finalStatus  = model::ImageStatus::LoadFailed;
                LogManager::instance().warn("Timeout on: " + files[i]);
            }
        }

        auto t1 = std::chrono::steady_clock::now();
        ir.elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        entry.status = ir.finalStatus;

        if (saveMarked_ && !outputDir_.empty() && !img.empty() &&
            ir.finalStatus != model::ImageStatus::LoadFailed) {
            auto fname = fs::path(files[i]).stem().string() + "_marked.jpg";
            auto outPath = (fs::path(outputDir_) / fname).string();
            auto marked = resultMarker_.drawDefectOverlay(img, ir);
            resultMarker_.saveMarkedImage(marked, outPath);
        }

        aggregator.addResult(entry, ir);

        if (progressCb_) {
            if (ir.finalStatus == model::ImageStatus::LoadFailed) {
                progressCb_->onError(entry, entry.errorCode);
            }
            progressCb_->onImageDone(i + 1, total, entry, ir);
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    bool partial = cancelToken_->isCancelled();

    lastReport_ = aggregator.finalize(partial, totalMs);

    if (!outputDir_.empty()) {
        auto csvPath = (fs::path(outputDir_) / "report.csv").string();
        auto jsonPath = (fs::path(outputDir_) / "report.json").string();
        reportGen_.generateCsv(lastReport_, csvPath);
        reportGen_.generateJson(lastReport_, jsonPath);
    }

    if (progressCb_) {
        progressCb_->onFinished(lastReport_);
    }

    LogManager::instance().info(
        "Pipeline finished. Total: " + std::to_string(lastReport_.totalImages) +
        " Defective: " + std::to_string(lastReport_.defective) +
        " Time: " + std::to_string(static_cast<int>(totalMs)) + "ms" +
        (partial ? " [PARTIAL]" : ""));

    running_ = false;
}

}
}