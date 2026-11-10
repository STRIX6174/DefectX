#pragma once

#include "defect_class.h"

#include <array>
#include <optional>

namespace qf {
namespace model {

// 单张图的最终分类结果
struct ClassificationResult {
    std::optional<DefectClass> predicted;  // 预测类别（低置信时为 nullopt，无法判定）
    double confidence = 0.0;               // 六类最高置信度
    std::array<double, 6> scores{};        // 六类置信度明细（0~1）
    double cycleTimeMs = 0.0;              // 单张耗时
};

}  // namespace model
}  // namespace qf