#pragma once

#include <fstream>
#include <string>
#include <mutex>

namespace qf {
namespace application {

enum class LogLevel {
    Info,
    Warn,
    Error
};

class LogManager {
public:
    static LogManager& instance();

    void setLogFile(const std::string& path);
    void log(LogLevel level, const std::string& message);
    void info(const std::string& msg)  { log(LogLevel::Info,  msg); }
    void warn(const std::string& msg)  { log(LogLevel::Warn,  msg); }
    void error(const std::string& msg) { log(LogLevel::Error, msg); }

private:
    LogManager() = default;
    std::ofstream file_;
    std::mutex    mutex_;
};

}
}