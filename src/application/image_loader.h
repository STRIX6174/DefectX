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

    // 文件内容 MD5（32 位小写十六进制）；读文件失败返回空串。
    std::string computeMd5(const std::string& filePath) const;
    // 相对输入根目录的路径（含子文件夹，统一 '/' 分隔）。无法相对化时回退为文件名。
    static std::string relativePath(const std::string& filePath, const std::string& root);
};

}  // namespace application
}  // namespace qf