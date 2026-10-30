#pragma once

#include "application/run_progress_callback.h"
#include "application/cancel_token.h"
#include "application/image_loader.h"
#include "application/ground_truth_parser.h"
#include "application/result_aggregator.h"
#include "application/result_marker.h"
#include "application/report_generator.h"
#include "application/config_manager.h"
#include "model/pipeline_params.h"
#include "model/inspection_result.h"
#include "algorithm/algorithm_factory.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace qf {
namespace application {

class PipelineRunner {
public:
    PipelineRunner();
    ~PipelineRunner();

    void setProgressCallback(std::shared_ptr<RunProgressCallback> cb);
    void setCancelToken(CancelTokenPtr token);

    void setConfig(const model::PipelineParams& params);
    void setImageFolder(const std::string& folderPath);
    void setGroundTruthPath(const std::string& gtPath);
    void setOutputDir(const std::string& outputDir);
    void setSaveMarkedImages(bool save);

    void setQcEnabled(bool enabled);
    void setParallelEnabled(bool enabled);

    void run();

    CancelTokenPtr cancelToken() const { return cancelToken_; }

    const model::RunReport& lastReport() const { return lastReport_; }

private:
    struct WorkRequest {
        const cv::Mat*              img = nullptr;
        model::InspectionResult*    result = nullptr;
    };

    void stopWorkers();

    std::vector<std::string> scanImageFiles(const std::string& folder);

    model::ImageEntry buildImageEntry(const std::string& filePath, const cv::Mat& img);

    model::InspectionResult processSingleImage(const cv::Mat& img);

    std::shared_ptr<RunProgressCallback> progressCb_;
    CancelTokenPtr cancelToken_;

    model::PipelineParams params_;
    std::string imageFolder_;
    std::string gtPath_;
    std::string outputDir_;
    bool saveMarked_ = false;

    std::unique_ptr<algorithm::QcOperator> qcOp_;
    std::vector<std::unique_ptr<algorithm::DefectOperator>> defectOps_;
    std::unique_ptr<algorithm::DecisionLogic> decision_;

    ImageLoader imageLoader_;
    ResultMarker resultMarker_;
    ReportGenerator reportGen_;

    model::RunReport lastReport_;

    std::atomic<bool> qcEnabled_{true};
    std::atomic<bool> parallelEnabled_{false};
    std::atomic<bool> workersActive_{false};
    bool running_ = false;
    int  timeoutMs_ = 5000;

    std::vector<std::thread> workers_;
    std::mutex workMutex_;
    std::condition_variable workCv_;
    std::condition_variable doneCv_;
    WorkRequest currentWork_{};
    size_t opsDone_ = 0;
};

}
}