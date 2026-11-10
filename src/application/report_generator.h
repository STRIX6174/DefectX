#pragma once

#include "model/run_report.h"

#include <string>

namespace qf {
namespace application {

// 报告仅输出无标签可记录指标（总图数/成功数/失败数/平均耗时/P95耗时/错误明细），
// 不输出任何图片、不包含任何评估指标。
class ReportGenerator {
public:
    void generateCsv(const model::RunReport& report, const std::string& outputPath);
    void generateJson(const model::RunReport& report, const std::string& outputPath);
};

}  // namespace application
}  // namespace qf