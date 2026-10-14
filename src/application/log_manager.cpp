#include "application/log_manager.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace qf {
namespace application {

LogManager& LogManager::instance() {
    static LogManager mgr;
    return mgr;
}

void LogManager::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) file_.close();
    file_.open(path, std::ios::out | std::ios::app);
}

void LogManager::log(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto tm  = *std::localtime(&tt);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " ";

    switch (level) {
    case LogLevel::Info:  oss << "[INFO]  "; break;
    case LogLevel::Warn:  oss << "[WARN]  "; break;
    case LogLevel::Error: oss << "[ERROR] "; break;
    }
    oss << message;

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << oss.str() << std::endl;
    if (file_.is_open()) {
        file_ << oss.str() << "\n";
    }
}

}
}