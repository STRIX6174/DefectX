#pragma once

#include "model/classification_result.h"
#include "model/defect_score.h"
#include "model/pipeline_params.h"

#include <array>

namespace qf {
namespace algorithm {

// 汇总六类置信度 → 最终类别。
// 取六类置信度最高者；低于 decision.minConfidence 时判为"无法判定"（predicted 为空）。
model::ClassificationResult fuseScores(
    const std::array<model::DefectScore, 6>& scores,
    const model::DecisionParams& decision);

}  // namespace algorithm
}  // namespace qf