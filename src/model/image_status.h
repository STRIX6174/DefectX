#pragma once

namespace qf {
namespace model {

enum class ImageStatus {
    Pending,
    Processing,
    LoadFailed,
    Classified,
    Skipped
};

}  // namespace model
}  // namespace qf