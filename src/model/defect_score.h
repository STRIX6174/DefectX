#pragma once

#include "defect_class.h"

namespace qf {
namespace model {

// 单类识别结果：度量值 M_c 相对阈值 T_c 的余量归一化置信度（0~1，非概率）
struct DefectScore {
    DefectClass defectClass = DefectClass::Crazing;
    double metricValue = 0.0;   // M_c（物理度量值）
    double threshold   = 0.0;   // T_c（该类判定阈值）
    double confidence  = 0.0;   // clamp((M_c - T_c) / T_c, 0, 1)
};

}  // namespace model
}  // namespace qf