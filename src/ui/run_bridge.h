#pragma once

#include "application/run_progress_callback.h"
#include "model/image_entry.h"
#include "model/inspection_result.h"
#include "model/run_report.h"

#include <opencv2/core.hpp>

#include <QObject>
#include <QImage>

namespace qf {
namespace ui {

class RunBridge : public QObject, public application::RunProgressCallback {
    Q_OBJECT

public:
    explicit RunBridge(QObject* parent = nullptr);

    void onImageDone(int done, int total,
                     const model::ImageEntry& img,
                     const model::InspectionResult& result) override;
    void onError(const model::ImageEntry& img, model::ErrorCode ec) override;
    void onFinished(const model::RunReport& report) override;

    static QImage toDisplayImage(const model::ImageEntry& entry,
                                 const cv::Mat& mat,
                                 const model::InspectionResult* result = nullptr);

signals:
    void imageDone(int done, int total,
                   const model::ImageEntry& img,
                   const model::InspectionResult& result);
    void errorOccurred(const model::ImageEntry& img, model::ErrorCode ec);
    void finished(const model::RunReport& report);
};

}
}