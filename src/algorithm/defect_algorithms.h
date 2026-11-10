#pragma once

#include "model/defect_score.h"
#include "model/pipeline_params.h"

#include <opencv2/core.hpp>

namespace qf {
namespace algorithm {

// 六类手写非深度学习识别算法，每类一个函数。
// 输入统一为单通道灰度图（由预处理阶段保证）；各算法内部只读输入，
// 只创建自己的中间副本，产出对应类别的 DefectScore（度量值 + 阈值 + 置信度）。
model::DefectScore detectCrazing(const cv::Mat& gray, const model::AlgorithmParams& p);
model::DefectScore detectInclusion(const cv::Mat& gray, const model::AlgorithmParams& p);
model::DefectScore detectPatches(const cv::Mat& gray, const model::AlgorithmParams& p);
model::DefectScore detectPittedSurface(const cv::Mat& gray, const model::AlgorithmParams& p);
model::DefectScore detectScratches(const cv::Mat& gray, const model::AlgorithmParams& p);
model::DefectScore detectRolledInScale(const cv::Mat& gray, const model::AlgorithmParams& p);

// 按类别分发，供业务层并行调度使用
model::DefectScore runSingleAlgorithm(model::DefectClass type,
                                      const cv::Mat& gray,
                                      const model::AlgorithmParams& p);

}  // namespace algorithm
}  // namespace qf