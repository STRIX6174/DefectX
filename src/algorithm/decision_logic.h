#pragma once

#include "model/image_status.h"
#include "model/defect_record.h"
#include "model/qc_result.h"

#include <vector>

namespace qf {
namespace algorithm {

class DecisionLogic {
public:
    virtual ~DecisionLogic() = default;

    virtual model::ImageStatus decide(
        const std::vector<model::DefectRecord>& defects,
        const model::QcResult& qcResult) const = 0;
};

}
}