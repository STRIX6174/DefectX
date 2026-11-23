#pragma once

#include "defect_class.h"

#include <array>

namespace qf {
namespace model {

// 单类别（one-vs-rest）混淆统计。
// 约定：precision / recall / f1 在分母为 0 时取 NaN（quiet_NaN），
//       落库时 NaN 映射为 SQL NULL，Macro 平均时跳过 NaN 类。
struct ClassMetrics {
    DefectClass defectClass = DefectClass::Crazing;
    int tp = 0;
    int fp = 0;
    int fn = 0;
    int tn = 0;
    int predictedCount = 0;   // 该类被预测的张数（无标签也有，供通讯「六类得分」用）
    double precision = 0.0;
    double recall    = 0.0;
    double f1        = 0.0;
};

// 运行级汇总指标：6×6 混淆矩阵 + 每类明细 + Macro 平均 + Accuracy。
struct RunMetrics {
    bool hasGroundTruth = false;   // 本批是否有任何有效真值
    int  labeledCount   = 0;       // 参与评估的样本数（有真值的图）
    int  unknownCount   = 0;       // 无法判定（Unknown 类别）张数，独立于六类混淆矩阵
    int  macroValidCount = 6;      // Macro 平均实际参与的有效类别数（分母不为 0）
    double accuracy       = 0.0;
    double macroPrecision = 0.0;
    double macroRecall    = 0.0;
    double macroF1        = 0.0;

    std::array<ClassMetrics, 6> perClass{};                 // 顺序同 AllDefectClasses
    std::array<std::array<int, 6>, 6> confusion{};          // [真值][预测]
};

}  // namespace model
}  // namespace qf