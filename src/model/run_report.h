#pragma once

#include "image_entry.h"
#include "inspection_result.h"
#include "error_code.h"

#include <string>
#include <vector>
#include <optional>
#include <utility>

namespace qf {
namespace model {

struct ErrorDetail {
    std::string fileName;
    ErrorCode   errorCode;
};

struct RunReport {
    int  totalImages   = 0;
    int  qcRejected    = 0;
    int  defective     = 0;
    int  passed        = 0;
    int  failedCount   = 0;

    std::optional<double> missRate;
    std::optional<double> overkillRate;

    double totalElapsedMs = 0.0;
    bool   isPartial      = false;

    std::vector<ErrorDetail> errorDetails;
    std::vector<std::pair<ImageEntry, InspectionResult>> details;
};

}
}