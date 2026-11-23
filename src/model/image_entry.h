#pragma once

#include "image_status.h"
#include "error_code.h"

#include <string>

namespace qf {
namespace model {

struct ImageEntry {
    std::string filePath;
    std::string relPath;       // 相对输入根目录的路径（含子文件夹，'/' 分隔）
    std::string contentHash;   // 文件字节 MD5（32 位小写十六进制），跨运行稳定的内容唯一标识
    int width  = 0;
    int height = 0;
    ImageStatus status = ImageStatus::Pending;
    ErrorCode errorCode = ErrorCode::None;
};

}  // namespace model
}  // namespace qf