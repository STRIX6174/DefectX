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

    // 覆盖写入 JSON 配置；失败返回 false。
    bool saveToFile(const std::string& configPath, const model::PipelineParams& params);

    // 内存结构 → JSON 文本（与 loadFromFile 对称，同时用作 runs.config_json 快照）。
    std::string toJsonString(const model::PipelineParams& params) const;

    model::PipelineParams defaults() const;};

}  // namespace application
}  // namespace qf