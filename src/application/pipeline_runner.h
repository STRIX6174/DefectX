#pragma once

#include "application/run_progress_callback.h"
#include "application/cancel_token.h"
#include "application/image_loader.h"
#include "application/config_manager.h"
#include "model/pipeline_params.h"
#include "model/run_report.h"
#include "model/classification_result.h"
#include "model/defect_score.h"
#include "model/validation_metrics.h"
#include "infrastructure/ground_truth.h"
#include "infrastructure/metric_calculator.h"
#include "infrastructure/result_store.h"
#include "infrastructure/comm/i_result_transmitter.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>
#include <memory>
#include <array>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace qf {
namespace application {

// 六类算法并行分类器（固定 6 个工作线程，每个线程绑定一个缺陷类别）
class ParallelClassifier {
public:
    ~ParallelClassifier();

    // 配置线程数与每类算法参数（在 setConfig 时调用一次）
    void configure(int threadCount, const model::PipelineParams& params);
    // 对单张灰度图并行运行六类算法，返回六类得分
    std::array<model::DefectScore, 6> classify(const cv::Mat& gray, int timeoutMs);
    void shutdown();

private:
    void workerLoop(int index, model::DefectClass type, model::AlgorithmParams params);

    std::vector<std::thread> workers_;
    std::mutex mtx_;
    std::condition_variable startCv_;
    std::condition_variable doneCv_;

    cv::Mat currentGray_;
    std::array<model::DefectScore, 6> results_{};
    int generation_ = 0;
    int doneCount_  = 0;
    bool stop_      = false;
};

// 流水线执行器：扫描文件夹 → 逐张 加载/预处理/六类并行识别/汇总 → 汇总报告
class PipelineRunner {
public:
    PipelineRunner();
    ~PipelineRunner();

    void setProgressCallback(std::shared_ptr<RunProgressCallback> cb);
    void setCancelToken(CancelTokenPtr token);

    void setConfig(const model::PipelineParams& params);
    void setImageFolder(const std::string& folderPath);

    void run();

    const model::RunReport& lastReport() const { return lastReport_; }
    const model::RunMetrics& lastMetrics() const { return lastMetrics_; }
    CancelTokenPtr cancelToken() const { return cancelToken_; }
    const model::PipelineParams& params() const { return params_; }

    // ---- 职责2（CommPanel）只读查询：拉取式，线程安全 ----
    infrastructure::TransmitterStatus commStatus() const;
    infrastructure::RegisterImage commRegisterImage() const;
    std::vector<std::string> takeCommLog();
    bool isCommChannelOpen() const;
    // 诊断「测试监听」：按当前配置尝试打开从站通道。
    bool testCommChannel(std::string& error);
    void closeCommChannel();

private:
    std::vector<std::string> scanImageFiles(const std::string& folder);
    model::ClassificationResult processSingleImage(const cv::Mat& img);
    void rebuildTransmitter();

    std::shared_ptr<RunProgressCallback> progressCb_;
    CancelTokenPtr cancelToken_;

    model::PipelineParams params_;
    std::string imageFolder_;

    ImageLoader imageLoader_;
    ParallelClassifier classifier_;

    model::RunReport lastReport_;
    model::RunMetrics lastMetrics_;

    infrastructure::ResultStore store_;
    std::unique_ptr<infrastructure::IResultTransmitter> transmitter_;
    mutable std::mutex commMtx_;

    std::atomic<bool> running_{false};
};

}  // namespace application
}  // namespace qf