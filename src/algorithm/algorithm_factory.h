#pragma once

#include "algorithm/qc_operator.h"
#include "algorithm/defect_operator.h"
#include "algorithm/decision_logic.h"
#include "model/pipeline_params.h"
#include "model/defect_type.h"

#include <memory>

namespace qf {
namespace algorithm {

std::unique_ptr<QcOperator> createQcOperator(const model::PipelineParams& params);
std::unique_ptr<DefectOperator> createDefectOperator(
    model::DefectType type, const model::DefectParam& params);
std::unique_ptr<DecisionLogic> createDecisionLogic();

}
}