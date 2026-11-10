#pragma once

#include "model/pipeline_params.h"

#include <opencv2/core.hpp>

namespace qf {
namespace algorithm {

// 输入有效性检查：非空且尺寸合理
bool isValidInput(const cv::Mat& img);

// 统一为单通道灰度（NEU-CLS 为灰度图；三通道输入转灰度）
cv::Mat toGray(const cv::Mat& img);

// 统一到标准尺寸（默认 200×200，保持内容缩放）
cv::Mat resizeToStandard(const cv::Mat& gray, int size = 200);

// 可选预处理算子（按配置开关，默认全部关闭）
cv::Mat applyDenoise(const cv::Mat& gray, const model::DenoiseParams& p);
cv::Mat applyContrast(const cv::Mat& gray, const model::ContrastParams& p);
cv::Mat applySharpen(const cv::Mat& gray, const model::SharpenParams& p);

// 汇总入口：有效性检查 → 灰度 → 尺寸统一 → 可选增强
// 输入可能为任意尺寸/通道；输出为统一后的单通道灰度图
cv::Mat preprocess(const cv::Mat& input, const model::PreprocessParams& p);

}  // namespace algorithm
}  // namespace qf