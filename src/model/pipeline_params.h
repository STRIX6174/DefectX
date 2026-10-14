#pragma once

#include "defect_type.h"

#include <string>
#include <map>

namespace qf {
namespace model {

struct DefectParam {
    int    blockSize    = 0;
    int    cValue       = 0;
    int    minArea      = 0;
    double edgeDensity  = 0.0;
    double histDiff     = 0.0;
    double localVariance = 0.0;
    int    tophatKernel = 0;
    int    blobCountMin = 0;
    int    cannyLow     = 0;
    int    cannyHigh    = 0;
    int    minLength    = 0;
    double aspectRatio  = 0.0;
    int    closeKernel  = 0;
};

struct PipelineParams {
    int    version = 1;

    double laplacianThreshold  = 100.0;
    int    overexposureHigh    = 245;
    int    underexposureLow    = 10;

    std::map<DefectType, DefectParam> defectParams;

    double missRateMax        = 0.05;
    double overkillRateMax    = 0.10;
    int    timeoutPerImageMs  = 5000;
};

}
}