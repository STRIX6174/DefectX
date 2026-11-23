#include "infrastructure/metric_calculator.h"

#include <cmath>
#include <limits>

namespace qf {
namespace infrastructure {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
}

void MetricCalculator::reset() {
    for (auto& row : confusion_) row.fill(0);
    predictedCount_.fill(0);
    unknownCount_ = 0;
    labeledCount_ = 0;
}

void MetricCalculator::accumulate(const std::optional<model::DefectClass>& truth,
                                  const std::optional<model::DefectClass>& pred) {
    if (pred.has_value()) {
        int p = model::defectClassIndex(*pred);
        if (p >= 0 && p < 6) ++predictedCount_[p];
    }

    if (!truth.has_value()) return;

    int t = model::defectClassIndex(*truth);
    if (t < 0 || t >= 6) return;
    ++labeledCount_;

    if (pred.has_value()) {
        int p = model::defectClassIndex(*pred);
        if (p >= 0 && p < 6) {
            ++confusion_[t][p];
        }
    } else {
        ++unknownCount_;  // 无法判定（Unknown 类别），独立于混淆矩阵
    }
}

model::RunMetrics MetricCalculator::finalize() const {
    model::RunMetrics m;
    m.hasGroundTruth = (labeledCount_ > 0);
    m.labeledCount   = labeledCount_;
    m.unknownCount   = unknownCount_;
    m.confusion      = confusion_;

    int totalDeterministic = 0;
    int diagonal = 0;
    for (int i = 0; i < 6; ++i) {
        diagonal += confusion_[i][i];
        for (int j = 0; j < 6; ++j) totalDeterministic += confusion_[i][j];
    }
    m.accuracy = (totalDeterministic > 0)
                 ? static_cast<double>(diagonal) / static_cast<double>(totalDeterministic)
                 : kNaN;

    int validP = 0, validR = 0, validF = 0;
    double sumP = 0.0, sumR = 0.0, sumF = 0.0;

    for (int c = 0; c < 6; ++c) {
        // one-vs-rest：TP = C[c][c]
        int tp = confusion_[c][c];

        // FN = 真值是 c 但没判成 c（Unknown 不计入任一类的 FN）
        int fn = 0;
        for (int j = 0; j < 6; ++j) fn += confusion_[c][j];
        fn -= tp;

        // FP = 真值不是 c 却判成 c
        int fp = 0;
        for (int i = 0; i < 6; ++i) fp += confusion_[i][c];
        fp -= tp;

        int tn = totalDeterministic - tp - fp - fn;

        double precision = (tp + fp == 0) ? kNaN : static_cast<double>(tp) / (tp + fp);
        double recall    = (tp + fn == 0) ? kNaN : static_cast<double>(tp) / (tp + fn);
        double f1 = kNaN;
        if (!std::isnan(precision) && !std::isnan(recall)) {
            f1 = (precision + recall == 0.0)
                 ? 0.0
                 : 2.0 * precision * recall / (precision + recall);
        }

        auto& cm = m.perClass[c];
        cm.defectClass    = model::AllDefectClasses[c];
        cm.tp = tp; cm.fp = fp; cm.fn = fn; cm.tn = tn;
        cm.predictedCount = predictedCount_[c];
        cm.precision = precision;
        cm.recall    = recall;
        cm.f1        = f1;

        if (!std::isnan(precision)) { sumP += precision; ++validP; }
        if (!std::isnan(recall))    { sumR += recall;    ++validR; }
        if (!std::isnan(f1))        { sumF += f1;        ++validF; }
    }

    m.macroPrecision = (validP > 0) ? sumP / validP : kNaN;
    m.macroRecall    = (validR > 0) ? sumR / validR : kNaN;
    m.macroF1        = (validF > 0) ? sumF / validF : kNaN;
    m.macroValidCount = validF;

    return m;
}

}  // namespace infrastructure
}  // namespace qf