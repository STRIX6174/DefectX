#pragma once

#include "model/run_report.h"
#include "model/image_entry.h"
#include "model/classification_result.h"

#include <vector>

namespace qf {
namespace application {

// 汇总无标签可记录指标：总图数/成功数/失败数/平均耗时/P95耗时/错误明细。
// 不涉及任何标注或评估指标（TP/FP/FN/漏检率/过杀率）。
class ResultAggregator {
public:
    void addSuccess(const model::ImageEntry& img, const model::ClassificationResult& result);
    void addError(const model::ImageEntry& img);
    model::RunReport finalize(bool isPartial);

private:
    int success_ = 0;
    int failed_  = 0;

    std::vector<double> cycleTimes_;
    std::vector<model::ErrorDetail> errors_;
    std::vector<model::ImageDetail> details_;
};

}  // namespace application
}  // namespace qf