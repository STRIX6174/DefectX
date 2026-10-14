#include "application/pipeline_runner.h"
#include "application/log_manager.h"

#include <filesystem>
#include <chrono>
#include <algorithm>
#include <future>

namespace fs = std::filesystem;

namespace qf {
namespace application {

PipelineRunner::PipelineRunner() {
    cancelToken_ = std::make_shared<CancelToken>();
}

void PipelineRunner::setProgressCallback(std::shared_ptr<RunProgressCallback> cb) {
    progressCb_ = std::move(cb);
}

void PipelineRunner::setCancelToken(CancelTokenPtr token) {
    cancelToken_ = std::move(token);
}

void PipelineRunner::setConfig(const model::PipelineParams& params) {
    params_ = params;

    qcOp_ = algorithm::createQcOperator(params_);
    defectOps_.clear();
    for (auto dt : model::AllDefectTypes) {
        auto it = params_.defectParams.find(dt);
        auto dp = (it != params_.defectParams.end()) ? it->second : model::DefectParam{};
        defectOps_.push_back(algorithm::createDefectOperator(dt, dp));
    }
    decision_ = algorithm::createDecisionLogic();

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

    result.qcResult = qcOp_->evaluate(img);
    if (!result.qcResult.passed) {
        result.finalStatus = model::ImageStatus::Invalid_Qc;
        return result;
    }

    for (auto& op : defectOps_) {
        auto defs = op->detect(img);
        for (auto& d : defs) {
            result.defects.push_back(std::move(d));
        }
    }

    result.finalStatus = decision_->decide(result.defects, result.qcResult);
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

    auto startTime = std::chrono::steady_clock::now();
    cancelToken_->reset();

    auto files = scanImageFiles(imageFolder_);
    if (files.empty()) {
        LogManager::instance().warn("No image files found in: " + imageFolder_);
        if (progressCb_) {
            progressCb_->onFinished(model::RunReport{});
        }
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

    int timeoutMs = params_.timeoutPerImageMs > 0 ? params_.timeoutPerImageMs : 5000;

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
                [this, &img]() { return processSingleImage(img); });

            auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
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
}

}
}