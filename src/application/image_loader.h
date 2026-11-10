#pragma once

#include "model/image_entry.h"
#include "model/error_code.h"

#include <opencv2/core.hpp>
#include <string>
#include <utility>

namespace qf {
namespace application {

// cv::imread 唯一调用点：以灰度方式加载（NEU-CLS 为单通道灰度图）。
class ImageLoader {
public:
    std::pair<cv::Mat, model::ErrorCode> load(const std::string& filePath);
    model::ImageEntry buildEntry(const std::string& filePath, const cv::Mat& img);
};

}  // namespace application
}  // namespace qf