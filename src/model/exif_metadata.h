#pragma once

#include <map>
#include <string>

namespace qf {
namespace model {

struct ExifMetadata {
    std::map<std::string, std::string> entries;
};

}
}