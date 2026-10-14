#pragma once

#include "image_status.h"
#include "error_code.h"
#include "exif_metadata.h"

#include <string>
#include <optional>

namespace qf {
namespace model {

struct ImageEntry {
    std::string filePath;
    int width  = 0;
    int height = 0;
    ImageStatus status = ImageStatus::Pending;
    ErrorCode errorCode = ErrorCode::None;
    std::optional<std::string> sourceHint;
    std::optional<ExifMetadata> exif;
};

}
}