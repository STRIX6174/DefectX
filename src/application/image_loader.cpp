#include "application/image_loader.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace qf {
namespace application {

std::pair<cv::Mat, model::ErrorCode> ImageLoader::load(const std::string& filePath) {
    cv::Mat img = cv::imread(filePath, cv::IMREAD_COLOR);
    if (img.empty()) {
        return {img, model::ErrorCode::DecodeFailed};
    }
    return {img, model::ErrorCode::None};
}

model::ImageEntry ImageLoader::buildEntry(const std::string& filePath, const cv::Mat& img) {
    model::ImageEntry entry;
    entry.filePath = filePath;
    entry.width    = img.cols;
    entry.height   = img.rows;
    entry.status   = model::ImageStatus::Pending;
    return entry;
}

}
}