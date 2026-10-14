#pragma once

#include "defect_type.h"

namespace qf {
namespace model {

enum class QcRejectReason {
    None,
    Blurry,
    Overexposed,
    Underexposed
};

inline const char* qcRejectReasonName(QcRejectReason r) {
    switch (r) {
    case QcRejectReason::Blurry:       return "Blurry";
    case QcRejectReason::Overexposed:  return "Overexposed";
    case QcRejectReason::Underexposed: return "Underexposed";
    case QcRejectReason::None:
    default: return "None";
    }
}

struct QcResult {
    bool     passed = false;
    QcRejectReason reason = QcRejectReason::None;
    double   laplacianVariance = 0.0;
    double   overexposureRatio = 0.0;
    double   underexposureRatio = 0.0;
};

}
}