#include "application/result_aggregator.h"

#include <algorithm>
#include <filesystem>

namespace qf {
namespace application {

ResultAggregator::ResultAggregator(
    const std::map<std::string, model::GroundTruthRecord>& gtLookup,
    bool hasGroundTruth)
    : gtLookup_(gtLookup)
    , hasGroundTruth_(hasGroundTruth) {}

void ResultAggregator::addResult(
    const model::ImageEntry& img, const model::InspectionResult& ir) {

    ++total_;

    switch (ir.finalStatus) {
    case model::ImageStatus::Invalid_Qc:
        ++qcRej_;
        break;
    case model::ImageStatus::Defective:
        ++defect_;
        break;
    case model::ImageStatus::Pass:
        ++passed_;
        break;
    case model::ImageStatus::LoadFailed:
        ++failed_;
        break;
    default:
        break;
    }

    if (ir.finalStatus == model::ImageStatus::LoadFailed) {
        model::ErrorDetail ed;
        ed.fileName  = img.filePath;
        ed.errorCode = img.errorCode;
        errors_.push_back(ed);
    }

    if (hasGroundTruth_) {
        auto fname = std::filesystem::path(img.filePath).filename().string();
        auto it = gtLookup_.find(fname);

        bool gtDefective = (it != gtLookup_.end() && !it->second.defectTypes.empty());
        bool detectedDefective = (ir.finalStatus == model::ImageStatus::Defective);

        if (gtDefective && detectedDefective)       ++tp_;
        else if (!gtDefective && detectedDefective) ++fp_;
        else if (gtDefective && !detectedDefective) ++fn_;

        for (const auto& pt : ir.perType) {
            bool gtHasThisType = false;
            if (it != gtLookup_.end()) {
                const auto& gtTypes = it->second.defectTypes;
                gtHasThisType = std::find(gtTypes.begin(), gtTypes.end(), pt.type) != gtTypes.end();
            }
            bool detectedThisType = (pt.status == model::ImageStatus::Defective);

            auto& m = perTypeMetrics_[pt.type];
            if (gtHasThisType && detectedThisType)       ++m.tp;
            else if (!gtHasThisType && detectedThisType) ++m.fp;
            else if (gtHasThisType && !detectedThisType) ++m.fn;
        }
    }

    details_.push_back({img, ir});
}

model::RunReport ResultAggregator::finalize(bool isPartial, double totalElapsedMs) {
    model::RunReport report;
    report.totalImages    = total_;
    report.qcRejected     = qcRej_;
    report.defective      = defect_;
    report.passed         = passed_;
    report.failedCount    = failed_;
    report.errorDetails   = std::move(errors_);
    report.details        = std::move(details_);
    report.isPartial      = isPartial;
    report.totalElapsedMs = totalElapsedMs;

    if (hasGroundTruth_ && (tp_ + fn_) > 0) {
        report.missRate    = static_cast<double>(fn_) / (tp_ + fn_);
    }
    if (hasGroundTruth_ && (tp_ + fp_) > 0) {
        report.overkillRate = static_cast<double>(fp_) / (tp_ + fp_);
    }

    for (const auto& [type, m] : perTypeMetrics_) {
        model::PerTypeReport r;
        r.type = type;
        r.tp = m.tp; r.fp = m.fp; r.fn = m.fn;
        int totalPositive = m.tp + m.fn;
        int totalAlarms   = m.tp + m.fp;
        r.missRate    = (totalPositive > 0) ? static_cast<double>(m.fn) / totalPositive : 0.0;
        r.overkillRate = (totalAlarms > 0)   ? static_cast<double>(m.fp) / totalAlarms   : 0.0;
        report.perType.push_back(r);
    }

    return report;
}

}
}