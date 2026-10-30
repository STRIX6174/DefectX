#pragma once

#include "image_entry.h"
#include "qc_result.h"
#include "defect_record.h"
#include "defect_type.h"
#include "image_status.h"

#include <vector>
#include <memory>
#include <optional>

namespace qf {
namespace model {

struct PerTypeResult {
    DefectType                    type = DefectType::Crazing;
    std::vector<DefectRecord>     defects;
    ImageStatus                   status = ImageStatus::Pending;
};

struct InspectionResult {
    const ImageEntry*             image = nullptr;
    QcResult                      qcResult;
    bool                          qcExecuted = false;
    std::vector<PerTypeResult>    perType;
    double                        elapsedMs = 0.0;
    ImageStatus                   finalStatus = ImageStatus::Pending;

    const std::vector<DefectRecord>& flatDefects() const {
        if (!cachedFlatDefects_.has_value()) {
            std::vector<DefectRecord> all;
            for (const auto& pt : perType) {
                all.insert(all.end(), pt.defects.begin(), pt.defects.end());
            }
            cachedFlatDefects_ = std::move(all);
        }
        return *cachedFlatDefects_;
    }

private:
    mutable std::optional<std::vector<DefectRecord>> cachedFlatDefects_;
};

}
}