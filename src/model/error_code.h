#pragma once

namespace qf {
namespace model {

enum class ErrorCode {
    None,
    FileNotFound,
    DecodeFailed,
    QcTimeout,
    DefectTimeout,
    Unknown
};

}
}