#include "application/config_manager.h"
#include "application/log_manager.h"
#include "model/defect_type.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace qf {
namespace application {

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

double extractDouble(const std::string& json, const std::string& key, double def) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    auto valStr = trim(json.substr(pos + 1));
    auto commaPos = valStr.find_first_of(",}\n\r");
    if (commaPos != std::string::npos) valStr = valStr.substr(0, commaPos);
    valStr = trim(valStr);
    try { return std::stod(valStr); }
    catch (...) { return def; }
}

int extractInt(const std::string& json, const std::string& key, int def) {
    return static_cast<int>(extractDouble(json, key, static_cast<double>(def)));
}

} // anonymous

model::PipelineParams ConfigManager::defaults() {
    model::PipelineParams p;
    p.laplacianThreshold = 100.0;
    p.overexposureHigh   = 245;
    p.underexposureLow   = 10;
    p.missRateMax        = 0.05;
    p.overkillRateMax    = 0.10;
    p.timeoutPerImageMs  = 5000;

    model::DefectParam crazing;
    crazing.edgeDensity = 0.05;
    crazing.minArea     = 50;
    p.defectParams[model::DefectType::Crazing] = crazing;

    model::DefectParam inclusion;
    inclusion.blockSize = 31;
    inclusion.cValue    = 3;
    inclusion.minArea   = 30;
    p.defectParams[model::DefectType::Inclusion] = inclusion;

    model::DefectParam patches;
    patches.histDiff       = 0.3;
    patches.localVariance  = 500.0;
    p.defectParams[model::DefectType::Patches] = patches;

    model::DefectParam pitted;
    pitted.tophatKernel = 15;
    pitted.blobCountMin = 5;
    p.defectParams[model::DefectType::PittedSurface] = pitted;

    model::DefectParam scratches;
    scratches.cannyLow     = 50;
    scratches.cannyHigh    = 150;
    scratches.minLength    = 80;
    scratches.aspectRatio  = 3.0;
    p.defectParams[model::DefectType::Scratches] = scratches;

    model::DefectParam ris;
    ris.blockSize   = 21;
    ris.closeKernel = 5;
    p.defectParams[model::DefectType::RolledInScale] = ris;

    return p;
}

std::optional<model::PipelineParams> ConfigManager::loadFromFile(const std::string& configPath) {
    try {
        auto json = readFile(configPath);
        auto params = defaults();

        params.laplacianThreshold = extractDouble(json, "laplacian_threshold", params.laplacianThreshold);
        params.overexposureHigh   = extractInt(json, "overexposure_high", params.overexposureHigh);
        params.underexposureLow   = extractInt(json, "underexposure_low", params.underexposureLow);

        auto missPos = json.find("\"miss_rate_max\"");
        if (missPos != std::string::npos) {
            params.missRateMax = extractDouble(json, "miss_rate_max", params.missRateMax);
        }
        auto overPos = json.find("\"overkill_rate_max\"");
        if (overPos != std::string::npos) {
            params.overkillRateMax = extractDouble(json, "overkill_rate_max", params.overkillRateMax);
        }
        params.timeoutPerImageMs = extractInt(json, "timeout_per_image_ms", params.timeoutPerImageMs);

        LogManager::instance().info("Config loaded from: " + configPath);
        return params;
    } catch (const std::exception& e) {
        LogManager::instance().error(std::string("Config load failed: ") + e.what());
        return std::nullopt;
    }
}

}
}