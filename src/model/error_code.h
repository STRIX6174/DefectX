#pragma once

namespace qf {
namespace model {

enum class ErrorCode {
    None,
    FileNotFound,
    DecodeFailed,
    AlgorithmFailed,
    Timeout,
    Unknown
};

}  // namespace model
}  // namespace qf