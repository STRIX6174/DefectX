#pragma once

#include "defect_type.h"
#include "roi.h"

#include <vector>

namespace qf {
namespace model {

struct DefectRecord {
    DefectType type        = DefectType::Crazing;
    double     confidence  = 0.0;
    double     area        = 0.0;
    Roi        roi;
    double     threshold   = 0.0;
    bool       hitGroundTruth = false;
};

}
}