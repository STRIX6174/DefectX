#include "application/result_aggregator.h"

#include <algorithm>
#include <filesystem>

namespace qf {
namespace application {

void ResultAggregator::addSuccess(const model::ImageEntry& img,
                                  const model::ClassificationResult& result) {
    ++success_;
    cycleTimes_.push_back(result.cycleTimeMs);

    model::ImageDetail detail;
    detail.fileName = std::filesystem::path(img.filePath).filename().string();
    detail.result   = result;
    details_.push_back(std::move(detail));
}

void ResultAggregator::addError(const model::ImageEntry& img) {
    ++failed_;

    model::ErrorDetail ed;
    ed.fileName  = std::filesystem::path(img.filePath).filename().string();
    ed.errorCode = img.errorCode;
    errors_.push_back(std::move(ed));
}

model::RunReport ResultAggregator::finalize(bool isPartial) {
    model::RunReport report;
    report.totalImages  = success_ + failed_;
    report.successCount = success_;
    report.failedCount  = failed_;
    report.isPartial    = isPartial;

    if (!cycleTimes_.empty()) {
        double sum = 0.0;
        for (double t : cycleTimes_) sum += t;
        report.avgCycleTimeMs = sum / static_cast<double>(cycleTimes_.size());

        std::vector<double> sorted = cycleTimes_;
        std::sort(sorted.begin(), sorted.end());
        size_t p95Index = static_cast<size_t>(0.95 * (sorted.size() - 1));
        report.p95CycleTimeMs = sorted[p95Index];
    }

    report.errorDetails = std::move(errors_);
    report.details      = std::move(details_);
    return report;
}

}  // namespace application
}  // namespace qf