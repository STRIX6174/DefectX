#include "application/report_generator.h"
#include "application/log_manager.h"
#include "model/defect_type.h"

#include <fstream>
#include <iomanip>
#include <filesystem>

namespace qf {
namespace application {

void ReportGenerator::generateCsv(const model::RunReport& report, const std::string& outputPath) {
    std::ofstream f(outputPath);
    if (!f.is_open()) {
        LogManager::instance().error("Cannot write CSV: " + outputPath);
        return;
    }

    f << "FileName,Status,DefectTypes,DefectCount,ElapsedMs\n";
    for (const auto& [img, ir] : report.details) {
        auto fname = std::filesystem::path(img.filePath).filename().string();

        std::string status;
        switch (ir.finalStatus) {
        case model::ImageStatus::Defective:  status = "DEFECT";   break;
        case model::ImageStatus::Pass:       status = "PASS";     break;
        case model::ImageStatus::Invalid_Qc: status = "QC_REJECT"; break;
        case model::ImageStatus::LoadFailed: status = "FAILED";   break;
        default:                            status = "UNKNOWN";  break;
        }

        std::string types;
        for (const auto& def : ir.defects) {
            if (!types.empty()) types += ";";
            types += model::defectTypeName(def.type);
        }
        if (types.empty()) types = "none";

        f << fname << ","
          << status << ","
          << types << ","
          << ir.defects.size() << ","
          << std::fixed << std::setprecision(1) << ir.elapsedMs << "\n";
    }

    f << "\n# Summary\n";
    f << "Total," << report.totalImages << "\n";
    f << "Defective," << report.defective << "\n";
    f << "Passed," << report.passed << "\n";
    f << "QC_Rejected," << report.qcRejected << "\n";
    f << "Failed," << report.failedCount << "\n";

    if (report.missRate.has_value()) {
        f << "MissRate," << std::fixed << std::setprecision(4) << *report.missRate << "\n";
    }
    if (report.overkillRate.has_value()) {
        f << "OverkillRate," << std::fixed << std::setprecision(4) << *report.overkillRate << "\n";
    }
    f << "TotalElapsedMs," << std::fixed << std::setprecision(0) << report.totalElapsedMs << "\n";
    f << "Partial," << (report.isPartial ? "true" : "false") << "\n";

    LogManager::instance().info("CSV report saved: " + outputPath);
}

void ReportGenerator::generateJson(const model::RunReport& report, const std::string& outputPath) {
    std::ofstream f(outputPath);
    if (!f.is_open()) {
        LogManager::instance().error("Cannot write JSON: " + outputPath);
        return;
    }

    f << "{\n";
    f << "  \"total\": " << report.totalImages << ",\n";
    f << "  \"defective\": " << report.defective << ",\n";
    f << "  \"passed\": " << report.passed << ",\n";
    f << "  \"qc_rejected\": " << report.qcRejected << ",\n";
    f << "  \"failed\": " << report.failedCount << ",\n";

    if (report.missRate.has_value())
        f << "  \"miss_rate\": " << std::fixed << std::setprecision(4) << *report.missRate << ",\n";
    if (report.overkillRate.has_value())
        f << "  \"overkill_rate\": " << std::fixed << std::setprecision(4) << *report.overkillRate << ",\n";

    f << "  \"total_elapsed_ms\": " << std::fixed << std::setprecision(0) << report.totalElapsedMs << ",\n";
    f << "  \"partial\": " << (report.isPartial ? "true" : "false") << ",\n";

    f << "  \"details\": [\n";
    for (size_t i = 0; i < report.details.size(); ++i) {
        const auto& [img, ir] = report.details[i];
        auto fname = std::filesystem::path(img.filePath).filename().string();
        f << "    {\n";
        f << "      \"file\": \"" << fname << "\",\n";
        f << "      \"status\": \"";
        switch (ir.finalStatus) {
        case model::ImageStatus::Defective:  f << "DEFECT"; break;
        case model::ImageStatus::Pass:       f << "PASS"; break;
        case model::ImageStatus::Invalid_Qc: f << "QC_REJECT"; break;
        case model::ImageStatus::LoadFailed: f << "FAILED"; break;
        default:                            f << "UNKNOWN"; break;
        }
        f << "\",\n";
        f << "  \"defect_count\": " << ir.defects.size() << ",\n";
        f << "  \"elapsed_ms\": " << std::fixed << std::setprecision(1) << ir.elapsedMs << "\n";
        f << "    }" << (i < report.details.size() - 1 ? "," : "") << "\n";
    }
    f << "  ]\n";
    f << "}\n";

    LogManager::instance().info("JSON report saved: " + outputPath);
}

}
}