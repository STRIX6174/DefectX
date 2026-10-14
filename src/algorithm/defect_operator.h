#pragma once

#include "model/defect_record.h"

#include <opencv2/core.hpp>
#include <vector>

namespace qf {
namespace algorithm {

class DefectOperator {
public:
    virtual ~DefectOperator() = default;
    virtual std::vector<model::DefectRecord> detect(const cv::Mat& img) = 0;
};

}
}