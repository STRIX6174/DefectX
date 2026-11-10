#pragma once

#include "application/run_progress_callback.h"
#include "model/image_entry.h"
#include "model/classification_result.h"
#include "model/run_report.h"

#include <opencv2/core.hpp>

#include <QObject>
#include <QImage>

Q_DECLARE_METATYPE(qf::model::ImageEntry)
Q_DECLARE_METATYPE(qf::model::ClassificationResult)
Q_DECLARE_METATYPE(qf::model::RunReport)

namespace qf {
namespace ui {

// 业务层（工作线程）→ UI（主线程）桥接：
// 回调在业务线程触发，转成 Qt 信号后以 QueuedConnection 投递主线程。
class RunBridge : public QObject, public application::RunProgressCallback {
    Q_OBJECT

public:
    explicit RunBridge(QObject* parent = nullptr);

    void onImageDone(int done, int total,
                     const model::ImageEntry& img,
                     const model::ClassificationResult& result) override;
    void onError(const model::ImageEntry& img, model::ErrorCode ec) override;
    void onFinished(const model::RunReport& report) override;

    static QImage toDisplayImage(const cv::Mat& mat);

signals:
    void imageDone(int done, int total,
                   const model::ImageEntry& img,
                   const model::ClassificationResult& result);
    void errorOccurred(const model::ImageEntry& img, model::ErrorCode ec);
    void finished(const model::RunReport& report);
};

}  // namespace ui
}  // namespace qf