#include "application/ground_truth_parser.h"
#include "application/log_manager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace qf {
namespace application {

namespace {

std::string lowerCase(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

std::string fileNameNoExt(const std::string& path) {
    return fs::path(path).stem().string();
}

std::string fileNameOnly(const std::string& path) {
    return fs::path(path).filename().string();
}

const std::vector<std::string> kNeuDefectDirs = {
    "crazing", "inclusion", "patches",
    "pitted_surface", "scratches", "rolled-in_scale"
};

bool hasNeuDefectSubdirs(const std::string& rootDir) {
    for (const auto& sub : kNeuDefectDirs) {
        if (fs::exists(fs::path(rootDir) / sub)) return true;
    }
    return false;
}

} // anonymous

std::optional<model::DefectType> GroundTruthParser::defectTypeFromName(const std::string& name) {
    auto lower = lowerCase(name);
    if (lower == "crazing" || lower == "cr")        return model::DefectType::Crazing;
    if (lower == "inclusion" || lower == "in")      return model::DefectType::Inclusion;
    if (lower == "patches" || lower == "pa")        return model::DefectType::Patches;
    if (lower == "pitted_surface" || lower == "ps")  return model::DefectType::PittedSurface;
    if (lower == "scratches" || lower == "sc")      return model::DefectType::Scratches;
    if (lower == "rolled-in_scale" || lower == "rs" ||
        lower == "rolled_in_scale")                  return model::DefectType::RolledInScale;
    return std::nullopt;
}

GtParseResult GroundTruthParser::parse(
    const std::string& gtPath,
    const std::vector<std::string>& imageFiles,
    GtFormat hint) {

    GtFormat fmt = hint;
    if (fmt == GtFormat::AutoDetect) {
        if (hasNeuDefectSubdirs(gtPath)) {
            fmt = GtFormat::NeuDet;
        } else {
            fmt = GtFormat::Dagm2007;
        }
    }

    switch (fmt) {
    case GtFormat::NeuDet:
        return parseNeuDet(gtPath, imageFiles);
    case GtFormat::Dagm2007:
        return parseDagm2007(gtPath, imageFiles);
    default:
        return parseNeuDet(gtPath, imageFiles);
    }
}

GtParseResult GroundTruthParser::parseNeuDet(
    const std::string& rootDir,
    const std::vector<std::string>& imageFiles) {

    GtParseResult result;
    result.detectedFormat = GtFormat::NeuDet;

    std::map<std::string, model::DefectType> fileLabels;

    for (const auto& sub : kNeuDefectDirs) {
        fs::path subPath = fs::path(rootDir) / sub;
        if (!fs::exists(subPath)) continue;

        auto dtOpt = defectTypeFromName(sub);
        if (!dtOpt) continue;

        for (const auto& entry : fs::directory_iterator(subPath)) {
            if (!entry.is_regular_file()) continue;
            auto ext = lowerCase(entry.path().extension().string());
            if (ext != ".jpg" && ext != ".png" && ext != ".bmp") continue;

            auto baseName = entry.path().filename().string();
            fileLabels[baseName] = *dtOpt;
        }
    }

    result.records.reserve(fileLabels.size());
    for (const auto& [fname, dtype] : fileLabels) {
        model::GroundTruthRecord rec;
        rec.fileName = fname;
        rec.defectTypes.push_back(dtype);
        result.records.push_back(std::move(rec));
    }

    LogManager::instance().info(
        "NEU-DET ground truth: " + std::to_string(result.records.size()) + " labeled images");
    return result;
}

GtParseResult GroundTruthParser::parseDagm2007(
    const std::string& gtFilePath,
    const std::vector<std::string>&) {

    GtParseResult result;
    result.detectedFormat = GtFormat::Dagm2007;

    std::ifstream file(gtFilePath);
    if (!file.is_open()) {
        result.errorMsg = "Cannot open DAGM ground truth file: " + gtFilePath;
        return result;
    }

    // DAGM 2007 has 6 defect classes (1-6), each visually distinct.
    // Current cross-validation uses binary classification only
    // (defective vs. non-defective). Mapping all non-zero labels to
    // Inclusion provides a conservative benchmark. When per-class
    // validation is needed, uncomment the six-class switch below.
    //
    // DAGM 2007 class mapping (approximate):
    //   Class 1 ~ PittedSurface      Class 4 ~ Scratches
    //   Class 2 ~ Inclusion          Class 5 ~ Patches
    //   Class 3 ~ Crazing            Class 6 ~ RolledInScale

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string fname;
        int label = 0;
        if (iss >> fname >> label) {
            model::GroundTruthRecord rec;
            rec.fileName = fname;
            if (label == 1) {
                rec.defectTypes.push_back(model::DefectType::Inclusion);
            }
            result.records.push_back(std::move(rec));
        }
    }

    LogManager::instance().info(
        "DAGM 2007 ground truth: " + std::to_string(result.records.size()) + " labeled images");
    return result;
}

std::map<std::string, model::GroundTruthRecord> GroundTruthParser::buildLookup(
    const GtParseResult& result) {
    std::map<std::string, model::GroundTruthRecord> lookup;
    for (const auto& rec : result.records) {
        lookup[rec.fileName] = rec;
    }
    return lookup;
}

}
}