#pragma once

#include "model/qc_result.h"

#include <opencv2/core.hpp>

namespace qf {
namespace algorithm {

class QcOperator {
public:
    virtual ~QcOperator() = default;
    virtual model::QcResult evaluate(const cv::Mat& img) = 0;
};

}
}