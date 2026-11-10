#include "application/report_generator.h"
#include "application/log_manager.h"
#include "model/defect_class.h"
#include "model/error_code.h"

#include <fstream>
#include <iomanip>

namespace qf {
namespace application {

namespace {

const char* errorCodeName(model::ErrorCode ec) {
    switch (ec) {
    case model::ErrorCode::None:          return "NONE";
    case model::ErrorCode::FileNotFound:  return "FILE_NOT_FOUND";
    case model::ErrorCode::DecodeFailed:  return "DECODE_FAILED";
    case model::ErrorCode::AlgorithmFailed: return "ALGORITHM_FAILED";
    case model::ErrorCode::Timeout:       return "TIMEOUT";
    case model::ErrorCode::Unknown:       return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // anonymous namespace

void ReportGenerator::generateCsv(const model::RunReport& report, const std::string& outputPath) {
    std::ofstream f(outputPath);
    if (!f.is_open()) {
        LogManager::instance().error("Cannot write CSV: " + outputPath);
        return;
    }

    f << "# Summary\n";
    f << "TotalImages," << report.totalImages << "\n";
    f << "SuccessCount," << report.successCount << "\n";
    f << "FailedCount," << report.failedCount << "\n";
    f << "AvgCycleTimeMs," << std::fixed << std::setprecision(3) << report.avgCycleTimeMs << "\n";
    f << "P95CycleTimeMs," << std::fixed << std::setprecision(3) << report.p95CycleTimeMs << "\n";
    f << "Partial," << (report.isPartial ? "true" : "false") << "\n";

    if (!report.errorDetails.empty()) {
        f << "\n# Errors\n";
        f << "FileName,ErrorCode\n";
        for (const auto& ed : report.errorDetails) {
            f << ed.fileName << "," << errorCodeName(ed.errorCode) << "\n";
        }
    }

    LogManager::instance().info("CSV report saved: " + outputPath);
}

void ReportGenerator::generateJson(const model::RunReport& report, const std::string& outputPath) {
    std::ofstream f(outputPath);
    if (!f.is_open()) {
        LogManager::instance().error("Cannot write JSON: " + outputPath);
        return;
    }

    f << "{\n";
    f << "  \"total_images\": " << report.totalImages << ",\n";
    f << "  \"success_count\": " << report.successCount << ",\n";
    f << "  \"failed_count\": " << report.failedCount << ",\n";
    f << "  \"avg_cycle_time_ms\": " << std::fixed << std::setprecision(3) << report.avgCycleTimeMs << ",\n";
    f << "  \"p95_cycle_time_ms\": " << std::fixed << std::setprecision(3) << report.p95CycleTimeMs << ",\n";
    f << "  \"partial\": " << (report.isPartial ? "true" : "false") << ",\n";

    f << "  \"errors\": [\n";
    for (size_t i = 0; i < report.errorDetails.size(); ++i) {
        const auto& ed = report.errorDetails[i];
        f << "    {\"file\": \"" << ed.fileName << "\", \"error_code\": \""
          << errorCodeName(ed.errorCode) << "\"}"
          << (i < report.errorDetails.size() - 1 ? "," : "") << "\n";
    }
    f << "  ]\n";
    f << "}\n";

    LogManager::instance().info("JSON report saved: " + outputPath);
}

}  // namespace application
}  // namespace qf