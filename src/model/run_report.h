#pragma once

#include "error_code.h"
#include "classification_result.h"

#include <string>
#include <vector>

namespace qf {
namespace model {

struct ErrorDetail {
    std::string fileName;
    ErrorCode errorCode = ErrorCode::None;
};

struct ImageDetail {
    std::string fileName;
    ClassificationResult result;
};

struct RunReport {
    int totalImages  = 0;
    int successCount = 0;
    int failedCount  = 0;

    double avgCycleTimeMs = 0.0;
    double p95CycleTimeMs = 0.0;

    bool isPartial = false;

    std::vector<ErrorDetail> errorDetails;
    std::vector<ImageDetail> details;
};

}  // namespace model
}  // namespace qf