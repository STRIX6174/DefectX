#pragma once

#include "model/pipeline_params.h"

#include <string>
#include <optional>

namespace qf {
namespace application {

class ConfigManager {
public:
    std::optional<model::PipelineParams> loadFromFile(const std::string& configPath);
    model::PipelineParams defaults();

private:
    static model::DefectParam parseDefectParam(const std::string& json);
};

}
}