#pragma once

#include "image_status.h"
#include "error_code.h"

#include <string>

namespace qf {
namespace model {

struct ImageEntry {
    std::string filePath;
    int width  = 0;
    int height = 0;
    ImageStatus status = ImageStatus::Pending;
    ErrorCode errorCode = ErrorCode::None;
};

}  // namespace model
}  // namespace qf