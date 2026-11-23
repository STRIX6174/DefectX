#pragma once

#include <string>

struct sqlite3;  // 前置声明，避免公共头文件暴露 SQLite 头

namespace qf {
namespace infrastructure {

// 极简 SQLite RAII 封装：open/exec/事务句柄。
// 只依赖 SQLite，不依赖 Qt / OpenCV / algorithm / application。
// 建表 Schema（runs / measurements / class_metrics / run_metrics）在 open() 时执行。
class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 打开/创建库文件并建表；失败返回 false，原因见 lastError()。
    bool open(const std::string& path);
    void close();
    bool isOpen() const { return db_ != nullptr; }

    // 执行无结果集语句（DDL/DML）。
    bool exec(const std::string& sql);

    bool begin();
    bool commit();
    bool rollback();

    sqlite3* handle() const { return db_; }
    const std::string& lastError() const { return lastError_; }

private:
    bool applySchema();
    void captureError(const char* fallback);

    sqlite3* db_ = nullptr;
    std::string lastError_;
};

}  // namespace infrastructure
}  // namespace qf
