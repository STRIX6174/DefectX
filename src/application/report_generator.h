#pragma once

#include "model/run_report.h"

#include <string>

namespace qf {
namespace application {

class ReportGenerator {
public:
    void generateCsv(const model::RunReport& report, const std::string& outputPath);
    void generateJson(const model::RunReport& report, const std::string& outputPath);
};

}
}