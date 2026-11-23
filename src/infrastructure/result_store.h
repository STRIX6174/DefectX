#pragma once

#include "infrastructure/database.h"
#include "model/pipeline_params.h"
#include "model/run_report.h"
#include "model/validation_metrics.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qf {
namespace infrastructure {

// 历史查询用的运行批次视图（runs LEFT JOIN run_metrics）。
struct RunRecord {
    int64_t runId = 0;
    std::string startedAt;
    std::string finishedAt;
    std::string mode;
    int totalImages  = 0;
    int successCount = 0;
    int failedCount  = 0;
    double avgCycleMs = 0.0;
    double p95CycleMs = 0.0;
    bool isPartial = false;

    bool hasMetrics = false;
    int labeledCount = 0;
    int unknownCount = 0;
    int validClassCount = 0;
    double accuracy = 0.0;
    double macroPrecision = 0.0;
    double macroRecall = 0.0;
    double macroF1 = 0.0;
};

// 历史查询用的单张测量视图。
struct MeasurementRecord {
    int64_t measurementId = 0;
    int64_t runId = 0;
    std::string relPath;
    std::string fileName;
    std::optional<model::DefectClass> groundTruth;
    std::optional<model::DefectClass> predicted;
    double confidence = 0.0;
    std::array<double, 6> scores{};  // 顺序同 AllDefectClasses
    double cycleTimeMs = 0.0;
    std::string status;
    std::string errorCode;
};

// 存储：写 runs / measurements / class_metrics / run_metrics，并从库 GROUP BY 汇总指标。
// 只依赖 SQLite + model，不依赖 Qt / OpenCV / application。
//
// 事务约定：beginRun() 写入 runs 后打开批量事务，addMeasurement 逐张写入，
// saveMetrics() 写指标，finishRun() 回填统计并 COMMIT —— 避免逐张 autocommit。
class ResultStore {
public:
    ResultStore() = default;
    ~ResultStore();

    ResultStore(const ResultStore&) = delete;
    ResultStore& operator=(const ResultStore&) = delete;

    bool open(const std::string& dbPath);
    void close();
    bool isOpen() const { return db_.isOpen(); }
    const std::string& dbPath() const { return dbPath_; }

    // 插入 runs 行并开启批量事务；失败返回 -1。
    int64_t beginRun(const std::string& mode,
                     const std::string& configJson = std::string());
    // 回填运行统计并提交事务。
    bool finishRun(int64_t runId, const model::RunReport& report);

    // 逐张落库（分类成功）。truth 为空表示未标注。
    bool addMeasurement(int64_t runId,
                        const model::ImageDetail& detail,
                        const std::optional<model::DefectClass>& truth);
    // 加载失败的图也落库（status=LoadFailed）。
    bool addMeasurementError(int64_t runId, const model::ErrorDetail& error);

    // 写六类明细缓存 + 运行级指标缓存（仅验证模式）。
    bool saveMetrics(int64_t runId, const model::RunMetrics& metrics);

    // 从库 GROUP BY 重算 6×6 混淆矩阵与指标（用于校验 / 缓存缺失时回退）。
    std::optional<model::RunMetrics> computeRunMetrics(int64_t runId);

    // ---- 历史查询（拉取式）----
    std::vector<RunRecord> listRuns(int limit = 200);
    std::vector<MeasurementRecord> listMeasurements(int64_t runId, int limit = 2000);
    std::vector<model::ClassMetrics> listClassMetrics(int64_t runId);

    const std::string& lastError() const { return lastError_; }

private:
    Database db_;
    std::string dbPath_;
    std::string lastError_;
    bool inBatch_ = false;
};

}  // namespace infrastructure
}  // namespace qf
