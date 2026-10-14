#pragma once

#include <string>
#include <array>

namespace qf {
namespace model {

enum class DefectType {
    Crazing,
    Inclusion,
    Patches,
    PittedSurface,
    Scratches,
    RolledInScale
};

constexpr std::array<DefectType, 6> AllDefectTypes = {
    DefectType::Crazing,
    DefectType::Inclusion,
    DefectType::Patches,
    DefectType::PittedSurface,
    DefectType::Scratches,
    DefectType::RolledInScale
};

inline const char* defectTypeName(DefectType t) {
    switch (t) {
    case DefectType::Crazing:       return "Crazing";
    case DefectType::Inclusion:     return "Inclusion";
    case DefectType::Patches:       return "Patches";
    case DefectType::PittedSurface: return "PittedSurface";
    case DefectType::Scratches:     return "Scratches";
    case DefectType::RolledInScale: return "RolledInScale";
    }
    return "Unknown";
}

inline const char* defectTypeAbbr(DefectType t) {
    switch (t) {
    case DefectType::Crazing:       return "Cr";
    case DefectType::Inclusion:     return "In";
    case DefectType::Patches:       return "Pa";
    case DefectType::PittedSurface: return "PS";
    case DefectType::Scratches:     return "Sc";
    case DefectType::RolledInScale: return "RS";
    }
    return "??";
}

}
}