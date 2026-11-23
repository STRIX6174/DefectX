#include "infrastructure/result_store.h"

#include "infrastructure/ground_truth.h"
#include "infrastructure/metric_calculator.h"
#include "model/image_status.h"

#include <sqlite3.h>

#include <cmath>
#include <ctime>
#include <limits>

namespace qf {
namespace infrastructure {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::string nowIso8601Utc() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

const char* statusToString(model::ImageStatus s) {
    switch (s) {
    case model::ImageStatus::Pending:    return "Pending";
    case model::ImageStatus::Processing: return "Processing";
    case model::ImageStatus::LoadFailed: return "LoadFailed";
    case model::ImageStatus::Classified: return "Classified";
    case model::ImageStatus::Skipped:    return "Skipped";
    }
    return "Unknown";
}

const char* errorCodeToString(model::ErrorCode c) {
    switch (c) {
    case model::ErrorCode::None:            return "None";
    case model::ErrorCode::FileNotFound:    return "FileNotFound";
    case model::ErrorCode::DecodeFailed:    return "DecodeFailed";
    case model::ErrorCode::AlgorithmFailed: return "AlgorithmFailed";
    case model::ErrorCode::Timeout:         return "Timeout";
    case model::ErrorCode::Unknown:         return "Unknown";
    }
    return "Unknown";
}

// 轻量语句封装：准备失败时 ok() 为 false。
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        ok_ = (sqlite3_prepare_v2(db_, sql, -1, &st_, nullptr) == SQLITE_OK);
    }
    ~Statement() {
        if (st_) sqlite3_finalize(st_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool ok() const { return ok_; }

    void bindInt(int idx, int v) { sqlite3_bind_int(st_, idx, v); }
    void bindInt64(int idx, int64_t v) { sqlite3_bind_int64(st_, idx, v); }
    void bindDouble(int idx, double v) { sqlite3_bind_double(st_, idx, v); }
    void bindText(int idx, const std::string& v) {
        sqlite3_bind_text(st_, idx, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }
    void bindNull(int idx) { sqlite3_bind_null(st_, idx); }
    // 指标分母为 0 时内存侧是 NaN，落库映射为 SQL NULL。
    void bindDoubleOrNull(int idx, double v) {
        if (std::isnan(v)) bindNull(idx);
        else bindDouble(idx, v);
    }

    int step() { return sqlite3_step(st_); }
    void reset() { sqlite3_reset(st_); }

    operator sqlite3_stmt*() const { return st_; }

    sqlite3* db() const { return db_; }
    const char* sqliteError() const { return sqlite3_errmsg(db_); }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* st_ = nullptr;
    bool ok_ = false;
};

std::string columnText(sqlite3_stmt* st, int col) {
    const unsigned char* text = sqlite3_column_text(st, col);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}

bool columnIsNull(sqlite3_stmt* st, int col) {
    return sqlite3_column_type(st, col) == SQLITE_NULL;
}

std::optional<model::DefectClass> parseOptionalClass(sqlite3_stmt* st, int col) {
    if (columnIsNull(st, col)) return std::nullopt;
    return parseClassAbbr(columnText(st, col));
}

}  // namespace

ResultStore::~ResultStore() {
    close();
}

bool ResultStore::open(const std::string& dbPath) {
    close();
    lastError_.clear();

    if (!db_.open(dbPath)) {
        lastError_ = db_.lastError();
        return false;
    }
    dbPath_ = dbPath;
    return true;
}

void ResultStore::close() {
    if (inBatch_) {
        db_.commit();
        inBatch_ = false;
    }
    db_.close();
}

int64_t ResultStore::beginRun(const std::string& mode, const std::string& configJson) {
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return -1;
    }

    Statement st(db_.handle(),
                 "INSERT INTO runs (started_at, mode, config_json) VALUES (?, ?, ?);");
    if (!st.ok()) {
        lastError_ = st.sqliteError();
        return -1;
    }
    st.bindText(1, nowIso8601Utc());
    st.bindText(2, mode);
    if (configJson.empty()) st.bindNull(3);
    else st.bindText(3, configJson);

    if (st.step() != SQLITE_DONE) {
        lastError_ = st.sqliteError();
        return -1;
    }

    const int64_t runId = sqlite3_last_insert_rowid(db_.handle());
    if (!db_.begin()) {
        lastError_ = db_.lastError();
        inBatch_ = false;
    } else {
        inBatch_ = true;
    }
    return runId;
}

bool ResultStore::addMeasurement(int64_t runId,
                                 const model::ImageDetail& detail,
                                 const std::optional<model::DefectClass>& truth) {
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return false;
    }

    Statement st(db_.handle(),
        "INSERT INTO measurements (run_id, rel_path, file_name, ground_truth, predicted, confidence,"
        " score_cr, score_in, score_pa, score_ps, score_rs, score_sc, cycle_time_ms, status, error_code)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    if (!st.ok()) {
        lastError_ = st.sqliteError();
        return false;
    }

    st.bindInt64(1, runId);
    st.bindText(2, detail.relPath);
    st.bindText(3, detail.fileName);
    if (truth.has_value()) st.bindText(4, model::defectClassAbbr(*truth));
    else st.bindNull(4);
    if (detail.result.predicted.has_value())
        st.bindText(5, model::defectClassAbbr(*detail.result.predicted));
    else
        st.bindNull(5);
    st.bindDouble(6, detail.result.confidence);

    // scores 顺序 = AllDefectClasses（Cr/In/Pa/PS/Sc/RS），
    // 而表列顺序为 cr/in/pa/ps/rs/sc —— 第 11、12 个参数需交换。
    st.bindDouble(7,  detail.result.scores[0]);  // cr
    st.bindDouble(8,  detail.result.scores[1]);  // in
    st.bindDouble(9,  detail.result.scores[2]);  // pa
    st.bindDouble(10, detail.result.scores[3]);  // ps
    st.bindDouble(11, detail.result.scores[5]);  // rs
    st.bindDouble(12, detail.result.scores[4]);  // sc

    st.bindDouble(13, detail.result.cycleTimeMs);
    st.bindText(14, statusToString(model::ImageStatus::Classified));
    st.bindNull(15);

    if (st.step() != SQLITE_DONE) {
        lastError_ = st.sqliteError();
        return false;
    }
    return true;
}

bool ResultStore::addMeasurementError(int64_t runId, const model::ErrorDetail& error) {
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return false;
    }

    Statement st(db_.handle(),
        "INSERT INTO measurements (run_id, rel_path, file_name, ground_truth, predicted, status, error_code)"
        " VALUES (?, ?, ?, NULL, NULL, ?, ?);");
    if (!st.ok()) {
        lastError_ = st.sqliteError();
        return false;
    }

    st.bindInt64(1, runId);
    st.bindText(2, error.relPath);
    st.bindText(3, error.fileName);
    st.bindText(4, statusToString(model::ImageStatus::LoadFailed));
    st.bindText(5, errorCodeToString(error.errorCode));

    if (st.step() != SQLITE_DONE) {
        lastError_ = st.sqliteError();
        return false;
    }
    return true;
}

bool ResultStore::saveMetrics(int64_t runId, const model::RunMetrics& metrics) {
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return false;
    }

    Statement cls(db_.handle(),
        "INSERT OR REPLACE INTO class_metrics (run_id, defect_class, predicted_count, tp, fp, fn, tn,"
        " precision, recall, f1) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    if (!cls.ok()) {
        lastError_ = cls.sqliteError();
        return false;
    }

    for (size_t i = 0; i < metrics.perClass.size(); ++i) {
        const auto& cm = metrics.perClass[i];
        cls.bindInt64(1, runId);
        cls.bindText(2, model::defectClassAbbr(cm.defectClass));
        cls.bindInt(3, cm.predictedCount);
        cls.bindInt(4, cm.tp);
        cls.bindInt(5, cm.fp);
        cls.bindInt(6, cm.fn);
        cls.bindInt(7, cm.tn);
        cls.bindDoubleOrNull(8, cm.precision);
        cls.bindDoubleOrNull(9, cm.recall);
        cls.bindDoubleOrNull(10, cm.f1);

        if (cls.step() != SQLITE_DONE) {
            lastError_ = cls.sqliteError();
            return false;
        }
        cls.reset();
    }

    Statement run(db_.handle(),
        "INSERT OR REPLACE INTO run_metrics (run_id, labeled_count, unknown_count, valid_class_count,"
        " accuracy, macro_precision, macro_recall, macro_f1) VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    if (!run.ok()) {
        lastError_ = run.sqliteError();
        return false;
    }
    run.bindInt64(1, runId);
    run.bindInt(2, metrics.labeledCount);
    run.bindInt(3, metrics.unknownCount);
    run.bindInt(4, metrics.macroValidCount);
    run.bindDoubleOrNull(5, metrics.accuracy);
    run.bindDoubleOrNull(6, metrics.macroPrecision);
    run.bindDoubleOrNull(7, metrics.macroRecall);
    run.bindDoubleOrNull(8, metrics.macroF1);

    if (run.step() != SQLITE_DONE) {
        lastError_ = run.sqliteError();
        return false;
    }
    return true;
}

bool ResultStore::finishRun(int64_t runId, const model::RunReport& report) {
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return false;
    }

    Statement st(db_.handle(),
        "UPDATE runs SET finished_at = ?, total_images = ?, success_count = ?, failed_count = ?,"
        " avg_cycle_ms = ?, p95_cycle_ms = ?, is_partial = ? WHERE run_id = ?;");
    if (!st.ok()) {
        lastError_ = st.sqliteError();
        return false;
    }
    st.bindText(1, nowIso8601Utc());
    st.bindInt(2, report.totalImages);
    st.bindInt(3, report.successCount);
    st.bindInt(4, report.failedCount);
    st.bindDouble(5, report.avgCycleTimeMs);
    st.bindDouble(6, report.p95CycleTimeMs);
    st.bindInt(7, report.isPartial ? 1 : 0);
    st.bindInt64(8, runId);

    if (st.step() != SQLITE_DONE) {
        lastError_ = st.sqliteError();
        return false;
    }

    if (inBatch_) {
        if (!db_.commit()) {
            lastError_ = db_.lastError();
            db_.rollback();
            inBatch_ = false;
            return false;
        }
        inBatch_ = false;
    }
    return true;
}

std::optional<model::RunMetrics> ResultStore::computeRunMetrics(int64_t runId) {
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return std::nullopt;
    }

    MetricCalculator calculator;
    std::array<int, 6> predictedCounts{};

    // ① 确定预测的 6×6 混淆矩阵
    {
        Statement st(db_.handle(),
            "SELECT ground_truth, predicted, COUNT(*) FROM measurements"
            " WHERE run_id = ? AND ground_truth IS NOT NULL AND predicted IS NOT NULL"
            " GROUP BY ground_truth, predicted;");
        if (!st.ok()) {
            lastError_ = st.sqliteError();
            return std::nullopt;
        }
        st.bindInt64(1, runId);
        while (st.step() == SQLITE_ROW) {
            auto truth = parseClassAbbr(columnText(st, 0));
            auto pred  = parseClassAbbr(columnText(st, 1));
            const int count = sqlite3_column_int(st, 2);
            if (!truth || !pred) continue;
            for (int i = 0; i < count; ++i) calculator.accumulate(truth, pred);
        }
    }

    // ② 六类预测数量（无标签也有，供通讯「六类得分」用）
    {
        Statement st(db_.handle(),
            "SELECT predicted, COUNT(*) FROM measurements"
            " WHERE run_id = ? AND predicted IS NOT NULL GROUP BY predicted;");
        if (!st.ok()) {
            lastError_ = st.sqliteError();
            return std::nullopt;
        }
        st.bindInt64(1, runId);
        while (st.step() == SQLITE_ROW) {
            auto pred = parseClassAbbr(columnText(st, 0));
            const int count = sqlite3_column_int(st, 1);
            if (!pred) continue;
            int idx = model::defectClassIndex(*pred);
            if (idx >= 0 && idx < 6) predictedCounts[idx] = count;
        }
    }

    // ③ 无法判定（predicted IS NULL）按真值分组计入 Unknown
    {
        Statement st(db_.handle(),
            "SELECT ground_truth, COUNT(*) FROM measurements"
            " WHERE run_id = ? AND ground_truth IS NOT NULL AND predicted IS NULL"
            " GROUP BY ground_truth;");
        if (!st.ok()) {
            lastError_ = st.sqliteError();
            return std::nullopt;
        }
        st.bindInt64(1, runId);
        while (st.step() == SQLITE_ROW) {
            auto truth = parseClassAbbr(columnText(st, 0));
            const int count = sqlite3_column_int(st, 1);
            if (!truth) continue;
            for (int i = 0; i < count; ++i) calculator.accumulate(truth, std::nullopt);
        }
    }

    model::RunMetrics metrics = calculator.finalize();
    // 预测数量以 SQL GROUP BY 的全量为准（含未标注样本），覆盖累积过程内的重复计数。
    for (int i = 0; i < 6; ++i) metrics.perClass[i].predictedCount = predictedCounts[i];
    return metrics;
}

std::vector<RunRecord> ResultStore::listRuns(int limit) {
    std::vector<RunRecord> rows;
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return rows;
    }
    if (limit <= 0) limit = 200;

    Statement st(db_.handle(),
        "SELECT r.run_id, r.started_at, COALESCE(r.finished_at, ''), r.mode,"
        " r.total_images, r.success_count, r.failed_count, r.avg_cycle_ms, r.p95_cycle_ms, r.is_partial,"
        " CASE WHEN m.run_id IS NULL THEN 0 ELSE 1 END,"
        " COALESCE(m.labeled_count, 0), COALESCE(m.unknown_count, 0), COALESCE(m.valid_class_count, 0),"
        " COALESCE(m.accuracy, 0), COALESCE(m.macro_precision, 0),"
        " COALESCE(m.macro_recall, 0), COALESCE(m.macro_f1, 0)"
        " FROM runs r LEFT JOIN run_metrics m ON m.run_id = r.run_id"
        " ORDER BY r.run_id DESC LIMIT ?;");
    if (!st.ok()) {
        lastError_ = st.sqliteError();
        return rows;
    }
    st.bindInt(1, limit);

    while (st.step() == SQLITE_ROW) {
        RunRecord rec;
        rec.runId        = sqlite3_column_int64(st, 0);
        rec.startedAt    = columnText(st, 1);
        rec.finishedAt   = columnText(st, 2);
        rec.mode         = columnText(st, 3);
        rec.totalImages  = sqlite3_column_int(st, 4);
        rec.successCount = sqlite3_column_int(st, 5);
        rec.failedCount  = sqlite3_column_int(st, 6);
        rec.avgCycleMs   = sqlite3_column_double(st, 7);
        rec.p95CycleMs   = sqlite3_column_double(st, 8);
        rec.isPartial    = sqlite3_column_int(st, 9) != 0;
        rec.hasMetrics   = sqlite3_column_int(st, 10) != 0;
        rec.labeledCount = sqlite3_column_int(st, 11);
        rec.unknownCount = sqlite3_column_int(st, 12);
        rec.validClassCount = sqlite3_column_int(st, 13);
        rec.accuracy       = sqlite3_column_double(st, 14);
        rec.macroPrecision  = sqlite3_column_double(st, 15);
        rec.macroRecall    = sqlite3_column_double(st, 16);
        rec.macroF1        = sqlite3_column_double(st, 17);
        rows.push_back(std::move(rec));
    }
    return rows;
}

std::vector<MeasurementRecord> ResultStore::listMeasurements(int64_t runId, int limit) {
    std::vector<MeasurementRecord> rows;
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return rows;
    }
    if (limit <= 0) limit = 2000;

    Statement st(db_.handle(),
        "SELECT measurement_id, run_id, rel_path, file_name, ground_truth, predicted, confidence,"
        " score_cr, score_in, score_pa, score_ps, score_rs, score_sc, cycle_time_ms, status,"
        " COALESCE(error_code, '')"
        " FROM measurements WHERE run_id = ? ORDER BY measurement_id LIMIT ?;");
    if (!st.ok()) {
        lastError_ = st.sqliteError();
        return rows;
    }
    st.bindInt64(1, runId);
    st.bindInt(2, limit);

    while (st.step() == SQLITE_ROW) {
        MeasurementRecord rec;
        rec.measurementId = sqlite3_column_int64(st, 0);
        rec.runId         = sqlite3_column_int64(st, 1);
        rec.relPath       = columnText(st, 2);
        rec.fileName      = columnText(st, 3);
        rec.groundTruth   = parseOptionalClass(st, 4);
        rec.predicted     = parseOptionalClass(st, 5);
        rec.confidence    = sqlite3_column_double(st, 6);
        rec.scores[0]     = sqlite3_column_double(st, 7);   // cr
        rec.scores[1]     = sqlite3_column_double(st, 8);   // in
        rec.scores[2]     = sqlite3_column_double(st, 9);   // pa
        rec.scores[3]     = sqlite3_column_double(st, 10);  // ps
        rec.scores[5]     = sqlite3_column_double(st, 11);  // rs
        rec.scores[4]     = sqlite3_column_double(st, 12);  // sc
        rec.cycleTimeMs   = sqlite3_column_double(st, 13);
        rec.status        = columnText(st, 14);
        rec.errorCode     = columnText(st, 15);
        rows.push_back(std::move(rec));
    }
    return rows;
}

std::vector<model::ClassMetrics> ResultStore::listClassMetrics(int64_t runId) {
    std::vector<model::ClassMetrics> rows;
    if (!db_.isOpen()) {
        lastError_ = "database not open";
        return rows;
    }

    Statement st(db_.handle(),
        "SELECT defect_class, predicted_count, tp, fp, fn, tn, precision, recall, f1"
        " FROM class_metrics WHERE run_id = ?;");
    if (!st.ok()) {
        lastError_ = st.sqliteError();
        return rows;
    }
    st.bindInt64(1, runId);

    while (st.step() == SQLITE_ROW) {
        model::ClassMetrics cm;
        auto cls = parseClassAbbr(columnText(st, 0));
        if (!cls) continue;
        cm.defectClass    = *cls;
        cm.predictedCount = sqlite3_column_int(st, 1);
        cm.tp = sqlite3_column_int(st, 2);
        cm.fp = sqlite3_column_int(st, 3);
        cm.fn = sqlite3_column_int(st, 4);
        cm.tn = sqlite3_column_int(st, 5);
        cm.precision = columnIsNull(st, 6) ? kNaN : sqlite3_column_double(st, 6);
        cm.recall    = columnIsNull(st, 7) ? kNaN : sqlite3_column_double(st, 7);
        cm.f1        = columnIsNull(st, 8) ? kNaN : sqlite3_column_double(st, 8);
        rows.push_back(cm);
    }
    return rows;
}

}  // namespace infrastructure
}  // namespace qf
