#include "application/result_marker.h"
#include "model/defect_type.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace qf {
namespace application {

namespace {

cv::Scalar colorForType(model::DefectType t) {
    switch (t) {
    case model::DefectType::Crazing:       return cv::Scalar(0, 0, 255);
    case model::DefectType::Inclusion:     return cv::Scalar(0, 255, 0);
    case model::DefectType::Patches:       return cv::Scalar(255, 0, 0);
    case model::DefectType::PittedSurface: return cv::Scalar(255, 0, 255);
    case model::DefectType::Scratches:     return cv::Scalar(0, 255, 255);
    case model::DefectType::RolledInScale: return cv::Scalar(255, 255, 0);
    }
    return cv::Scalar(255, 255, 255);
}

} // anonymous

cv::Mat ResultMarker::drawDefectOverlay(
    const cv::Mat& original, const model::InspectionResult& result) {

    cv::Mat marked = original.clone();

    for (const auto& def : result.defects) {
        cv::Scalar color = colorForType(def.type);
        cv::Rect roi(def.roi.x, def.roi.y, def.roi.width, def.roi.height);

        cv::rectangle(marked, roi, color, 2);

        std::string label = std::string(model::defectTypeAbbr(def.type))
                          + " " + std::to_string(static_cast<int>(def.confidence * 100)) + "%";
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        cv::rectangle(marked,
                      cv::Point(roi.x, roi.y - textSize.height - 4),
                      cv::Point(roi.x + textSize.width, roi.y),
                      color, cv::FILLED);
        cv::putText(marked, label,
                    cv::Point(roi.x, roi.y - 3),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(255, 255, 255), 1);
    }

    std::string statusText;
    cv::Scalar statusColor;
    switch (result.finalStatus) {
    case model::ImageStatus::Defective:
        statusText = "DEFECT"; statusColor = cv::Scalar(0, 0, 255); break;
    case model::ImageStatus::Pass:
        statusText = "PASS";   statusColor = cv::Scalar(0, 255, 0); break;
    case model::ImageStatus::Invalid_Qc:
        statusText = "QC_REJ"; statusColor = cv::Scalar(0, 255, 255); break;
    default:
        statusText = "ERROR";  statusColor = cv::Scalar(128, 128, 128); break;
    }

    cv::putText(marked, statusText,
                cv::Point(10, marked.rows - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, statusColor, 2);

    return marked;
}

void ResultMarker::saveMarkedImage(const cv::Mat& marked, const std::string& outputPath) {
    cv::imwrite(outputPath, marked);
}

}
}