#pragma once

#include "model/inspection_result.h"
#include "model/run_report.h"

#include <opencv2/core.hpp>
#include <string>

namespace qf {
namespace application {

class ResultMarker {
public:
    cv::Mat drawDefectOverlay(
        const cv::Mat& original,
        const model::InspectionResult& result);

    void saveMarkedImage(
        const cv::Mat& marked,
        const std::string& outputPath);
};

}
}