#include "algorithm/result_fusion.h"

#include "model/defect_class.h"

#include <algorithm>

namespace qf {
namespace algorithm {

model::ClassificationResult fuseScores(
    const std::array<model::DefectScore, 6>& scores,
    const model::DecisionParams& decision) {

    model::ClassificationResult result;

    int bestIndex = -1;
    double bestConfidence = 0.0;

    for (size_t i = 0; i < scores.size(); ++i) {
        const auto& s = scores[i];
        int idx = model::defectClassIndex(s.defectClass);
        if (idx < 0 || idx >= 6) continue;

        result.scores[idx] = s.confidence;

        if (s.confidence > bestConfidence) {
            bestConfidence = s.confidence;
            bestIndex = idx;
        }
    }

    result.confidence = bestConfidence;

    double minConfidence = decision.minConfidence;
    if (bestIndex >= 0 && bestConfidence >= minConfidence) {
        result.predicted = model::AllDefectClasses[bestIndex];
    } else {
        result.predicted = std::nullopt;
    }

    return result;
}

}  // namespace algorithm
}  // namespace qf