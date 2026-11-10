#include "ui/run_bridge.h"

#include <opencv2/imgproc.hpp>

namespace qf {
namespace ui {

RunBridge::RunBridge(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<model::ImageEntry>("qf::model::ImageEntry");
    qRegisterMetaType<model::ClassificationResult>("qf::model::ClassificationResult");
    qRegisterMetaType<model::RunReport>("qf::model::RunReport");
}

void RunBridge::onImageDone(int done, int total,
                            const model::ImageEntry& img,
                            const model::ClassificationResult& result) {
    emit imageDone(done, total, img, result);
}

void RunBridge::onError(const model::ImageEntry& img, model::ErrorCode ec) {
    emit errorOccurred(img, ec);
}

void RunBridge::onFinished(const model::RunReport& report) {
    emit finished(report);
}

QImage RunBridge::toDisplayImage(const cv::Mat& mat) {
    if (mat.empty()) return QImage();

    cv::Mat rgb;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    } else if (mat.channels() == 1) {
        cv::cvtColor(mat, rgb, cv::COLOR_GRAY2RGB);
    } else {
        rgb = mat.clone();
    }

    QImage qimg(rgb.data, rgb.cols, rgb.rows,
                static_cast<int>(rgb.step), QImage::Format_RGB888);
    return qimg.copy();
}

}  // namespace ui
}  // namespace qf