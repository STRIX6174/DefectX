#pragma once

namespace qf {
namespace model {

enum class ImageStatus {
    Pending,
    Processing,
    LoadFailed,
    Invalid_Qc,
    Defective,
    Pass
};

}
}