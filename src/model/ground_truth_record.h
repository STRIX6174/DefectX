#pragma once

#include "defect_type.h"
#include "roi.h"

#include <string>
#include <vector>

namespace qf {
namespace model {

struct GroundTruthRecord {
    std::string              fileName;
    std::vector<DefectType>   defectTypes;
    std::vector<Roi>          bboxes;
};

inline bool isDefective(const GroundTruthRecord& gt) {
    return !gt.defectTypes.empty();
}

}
}