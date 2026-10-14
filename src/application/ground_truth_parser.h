#pragma once

#include "model/ground_truth_record.h"
#include "model/defect_type.h"

#include <string>
#include <vector>
#include <map>
#include <optional>

namespace qf {
namespace application {

enum class GtFormat {
    NeuDet,
    Dagm2007,
    AutoDetect
};

struct GtParseResult {
    std::vector<model::GroundTruthRecord> records;
    std::optional<std::string> errorMsg;
    GtFormat detectedFormat = GtFormat::AutoDetect;
};

class GroundTruthParser {
public:
    GtParseResult parse(
        const std::string& gtPath,
        const std::vector<std::string>& imageFiles,
        GtFormat hint = GtFormat::AutoDetect);

    std::map<std::string, model::GroundTruthRecord> buildLookup(
        const GtParseResult& result);

    static std::optional<model::DefectType> defectTypeFromName(const std::string& name);

private:
    GtParseResult parseNeuDet(
        const std::string& rootDir,
        const std::vector<std::string>& imageFiles);
    GtParseResult parseDagm2007(
        const std::string& gtFilePath,
        const std::vector<std::string>& imageFiles);
};

}
}