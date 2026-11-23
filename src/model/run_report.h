#pragma once

#include "error_code.h"
#include "classification_result.h"

#include <string>
#include <vector>

namespace qf {
namespace model {

struct ErrorDetail {
    std::string fileName;
    std::string filePath;   // 绝对路径
    std::string relPath;    // 相对输入根目录的路径（含子文件夹，'/' 分隔）
    ErrorCode errorCode = ErrorCode::None;
};

struct ImageDetail {
    std::string fileName;     // 文件名（如 Cr_1.bmp）
    std::string filePath;     // 绝对路径
    std::string relPath;      // 相对输入根目录的路径（含子文件夹，如 Cr/Cr_1.bmp）
    std::string contentHash;  // 文件字节 MD5（32 位小写十六进制），跨运行稳定的内容唯一标识
    ClassificationResult result;
};

struct RunReport {
    int totalImages  = 0;
    int successCount = 0;
    int failedCount  = 0;

    double avgCycleTimeMs = 0.0;
    double p95CycleTimeMs = 0.0;

    bool isPartial = false;

    std::vector<ErrorDetail> errorDetails;
    std::vector<ImageDetail> details;
};

}  // namespace model
}  // namespace qf