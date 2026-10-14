#pragma once

#include "image_entry.h"
#include "qc_result.h"
#include "defect_record.h"

#include <vector>
#include <memory>

namespace qf {
namespace model {

struct InspectionResult {
    const ImageEntry*       image = nullptr;
    QcResult                qcResult;
    std::vector<DefectRecord> defects;
    double                  elapsedMs = 0.0;
    ImageStatus             finalStatus = ImageStatus::Pending;
};

}
}