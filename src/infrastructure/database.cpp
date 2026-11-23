#include "infrastructure/database.h"

#include <sqlite3.h>

namespace qf {
namespace infrastructure {

namespace {

// 一个库文件累积所有历史运行：3 张主表 + 1 张运行级指标缓存表。
// 约定：precision / recall / f1 分母为 0 时写 NULL（内存侧为 NaN）。
const char* const kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS runs (
    run_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at    TEXT NOT NULL,
    finished_at   TEXT,
    mode          TEXT NOT NULL DEFAULT 'inference',
    total_images  INTEGER NOT NULL DEFAULT 0,
    success_count INTEGER NOT NULL DEFAULT 0,
    failed_count  INTEGER NOT NULL DEFAULT 0,
    avg_cycle_ms  REAL NOT NULL DEFAULT 0,
    p95_cycle_ms  REAL NOT NULL DEFAULT 0,
    is_partial    INTEGER NOT NULL DEFAULT 0,
    config_json   TEXT
);

CREATE TABLE IF NOT EXISTS measurements (
    measurement_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id         INTEGER NOT NULL,
    rel_path       TEXT NOT NULL,
    file_name      TEXT NOT NULL,
    ground_truth   TEXT,
    predicted      TEXT,
    confidence     REAL NOT NULL DEFAULT 0,
    score_cr REAL NOT NULL DEFAULT 0,
    score_in REAL NOT NULL DEFAULT 0,
    score_pa REAL NOT NULL DEFAULT 0,
    score_ps REAL NOT NULL DEFAULT 0,
    score_rs REAL NOT NULL DEFAULT 0,
    score_sc REAL NOT NULL DEFAULT 0,
    cycle_time_ms  REAL NOT NULL DEFAULT 0,
    status         TEXT NOT NULL,
    error_code     TEXT,
    FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_measurements_run ON measurements(run_id);

CREATE TABLE IF NOT EXISTS class_metrics (
    run_id          INTEGER NOT NULL,
    defect_class    TEXT NOT NULL,
    predicted_count INTEGER NOT NULL DEFAULT 0,
    tp INTEGER NOT NULL DEFAULT 0,
    fp INTEGER NOT NULL DEFAULT 0,
    fn INTEGER NOT NULL DEFAULT 0,
    tn INTEGER NOT NULL DEFAULT 0,
    precision REAL,
    recall    REAL,
    f1        REAL,
    PRIMARY KEY (run_id, defect_class),
    FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS run_metrics (
    run_id            INTEGER PRIMARY KEY,
    labeled_count     INTEGER NOT NULL DEFAULT 0,
    unknown_count     INTEGER NOT NULL DEFAULT 0,
    valid_class_count INTEGER NOT NULL DEFAULT 6,
    accuracy          REAL,
    macro_precision   REAL,
    macro_recall      REAL,
    macro_f1          REAL,
    FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE
);
)SQL";

}  // namespace

Database::~Database() {
    close();
}

bool Database::open(const std::string& path) {
    close();
    lastError_.clear();

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        captureError("sqlite3_open_v2 failed");
        if (db_) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
        return false;
    }

    sqlite3_busy_timeout(db_, 3000);
    if (!exec("PRAGMA foreign_keys = ON;")) {
        close();
        return false;
    }
    if (!applySchema()) {
        close();
        return false;
    }
    return true;
}

void Database::close() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool Database::exec(const std::string& sql) {
    if (!db_) {
        lastError_ = "database not open";
        return false;
    }
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        lastError_ = errMsg ? errMsg : sqlite3_errmsg(db_);
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::begin() {
    return exec("BEGIN;");
}

bool Database::commit() {
    return exec("COMMIT;");
}

bool Database::rollback() {
    return exec("ROLLBACK;");
}

bool Database::applySchema() {
    return exec(kSchema);
}

void Database::captureError(const char* fallback) {
    lastError_ = db_ ? sqlite3_errmsg(db_) : fallback;
}

}  // namespace infrastructure
}  // namespace qf
