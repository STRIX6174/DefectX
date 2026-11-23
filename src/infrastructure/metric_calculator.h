#pragma once

#include "model/validation_metrics.h"
#include "model/defect_class.h"

#include <array>
#include <optional>

namespace qf {
namespace infrastructure {

// 纯数学指标计算器：累积 ground_truth × predicted，产出 RunMetrics。
// 不依赖 Qt / OpenCV / SQLite，可独立单测。
//
// 约定（与《基础属性设计蓝图2.1》§七一致）：
//  - 预测为 nullopt（无法判定，Unknown 类别）时，不进入 6×6 混淆矩阵，单独计 unknownCount；
//  - 某类 TP+FP=0（或 TP+FN=0）时对应 P/R/F1 记 NaN，Macro 平均跳过 NaN 类；
//  - 六类预测数量（predictedCount）无论是否有真值都会累计，供通讯"六类得分"用。
class MetricCalculator {
public:
    void reset();

    // truth 为空（推理模式）：只累计六类预测数量。
    // truth 有效（验证模式）：累计混淆矩阵；pred 为空则计入 Unknown（独立于混淆矩阵）。
    void accumulate(const std::optional<model::DefectClass>& truth,
                    const std::optional<model::DefectClass>& pred);

    model::RunMetrics finalize() const;

private:
    std::array<std::array<int, 6>, 6> confusion_{};  // [真值][预测]（仅含确定预测）
    std::array<int, 6> predictedCount_{};            // 每类被预测的张数
    int unknownCount_ = 0;                           // 无法判定（Unknown）张数
    int labeledCount_ = 0;                           // 有真值的样本数
};

}  // namespace infrastructure
}  // namespace qf