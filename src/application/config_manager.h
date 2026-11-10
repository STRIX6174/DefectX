#pragma once

#include "model/pipeline_params.h"

#include <string>
#include <optional>

namespace qf {
namespace application {

// JSON 配置 → PipelineParams。
// 配置结构见《基础属性设计蓝图2.0》2.3.1 节。
class ConfigManager {
public:
    std::optional<model::PipelineParams> loadFromFile(const std::string& configPath);
    model::PipelineParams defaults();
};

}  // namespace application
}  // namespace qf