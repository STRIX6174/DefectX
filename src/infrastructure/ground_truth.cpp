#include "infrastructure/ground_truth.h"

#include <filesystem>
#include <algorithm>
#include <cctype>

namespace qf {
namespace infrastructure {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string upper(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

}  // anonymous namespace

std::optional<model::DefectClass> parseClassAbbr(const std::string& abbr) {
    const std::string a = upper(trim(abbr));
    for (model::DefectClass c : model::AllDefectClasses) {
        if (a == upper(model::defectClassAbbr(c))) {
            return c;
        }
    }
    return std::nullopt;
}

std::optional<model::DefectClass> FolderNameGroundTruth::lookup(const std::string& imagePath) const {
    std::filesystem::path p(imagePath);
    std::string parent = p.parent_path().filename().string();
    return parseClassAbbr(parent);
}

}  // namespace infrastructure
}  // namespace qf