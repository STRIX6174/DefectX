#pragma once

#include "model/run_report.h"
#include "model/inspection_result.h"
#include "model/ground_truth_record.h"

#include <map>
#include <string>
#include <vector>

namespace qf {
namespace application {

class ResultAggregator {
public:
    ResultAggregator(
        const std::map<std::string, model::GroundTruthRecord>& gtLookup,
        bool hasGroundTruth);

    void addResult(const model::ImageEntry& img, const model::InspectionResult& ir);
    model::RunReport finalize(bool isPartial, double totalElapsedMs);

private:
    std::map<std::string, model::GroundTruthRecord> gtLookup_;
    bool hasGroundTruth_;

    int total_   = 0;
    int qcRej_   = 0;
    int defect_  = 0;
    int passed_  = 0;
    int failed_  = 0;

    int tp_ = 0;
    int fp_ = 0;
    int fn_ = 0;

    std::vector<model::ErrorDetail> errors_;
    std::vector<std::pair<model::ImageEntry, model::InspectionResult>> details_;
};

}
}