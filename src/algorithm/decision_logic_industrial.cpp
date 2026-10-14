#include "algorithm/decision_logic.h"

#include <memory>

namespace qf {
namespace algorithm {

class IndustrialDecisionLogic : public DecisionLogic {
public:
    model::ImageStatus decide(
        const std::vector<model::DefectRecord>& defects,
        const model::QcResult& qcResult) const override {

        if (!qcResult.passed) {
            return model::ImageStatus::Invalid_Qc;
        }

        if (!defects.empty()) {
            return model::ImageStatus::Defective;
        }

        return model::ImageStatus::Pass;
    }
};

std::unique_ptr<DecisionLogic> createDecisionLogic() {
    return std::make_unique<IndustrialDecisionLogic>();
}

}
}