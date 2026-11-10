#pragma once

#include <array>

namespace qf {
namespace model {

// NEU-CLS 六类热轧带钢表面缺陷（与数据集类别一一对应）
enum class DefectClass {
    Crazing,        // 龟裂 Cr
    Inclusion,      // 夹杂 In
    Patches,        // 斑块 Pa
    PittedSurface,  // 麻面 PS
    Scratches,      // 划痕 Sc
    RolledInScale   // 轧入氧化铁皮 RS
};

constexpr std::array<DefectClass, 6> AllDefectClasses = {
    DefectClass::Crazing,
    DefectClass::Inclusion,
    DefectClass::Patches,
    DefectClass::PittedSurface,
    DefectClass::Scratches,
    DefectClass::RolledInScale
};

inline int defectClassIndex(DefectClass c) {
    switch (c) {
    case DefectClass::Crazing:       return 0;
    case DefectClass::Inclusion:     return 1;
    case DefectClass::Patches:       return 2;
    case DefectClass::PittedSurface: return 3;
    case DefectClass::Scratches:     return 4;
    case DefectClass::RolledInScale: return 5;
    }
    return -1;
}

inline const char* defectClassName(DefectClass c) {
    switch (c) {
    case DefectClass::Crazing:       return "Crazing";
    case DefectClass::Inclusion:     return "Inclusion";
    case DefectClass::Patches:       return "Patches";
    case DefectClass::PittedSurface: return "PittedSurface";
    case DefectClass::Scratches:     return "Scratches";
    case DefectClass::RolledInScale: return "RolledInScale";
    }
    return "Unknown";
}

inline const char* defectClassAbbr(DefectClass c) {
    switch (c) {
    case DefectClass::Crazing:       return "Cr";
    case DefectClass::Inclusion:     return "In";
    case DefectClass::Patches:       return "Pa";
    case DefectClass::PittedSurface: return "PS";
    case DefectClass::Scratches:     return "Sc";
    case DefectClass::RolledInScale: return "RS";
    }
    return "??";
}

inline const char* defectClassChinese(DefectClass c) {
    switch (c) {
    case DefectClass::Crazing:       return "龟裂";
    case DefectClass::Inclusion:     return "夹杂";
    case DefectClass::Patches:       return "斑块";
    case DefectClass::PittedSurface: return "麻面";
    case DefectClass::Scratches:     return "划痕";
    case DefectClass::RolledInScale: return "轧入氧化铁皮";
    }
    return "未知";
}

}  // namespace model
}  // namespace qf