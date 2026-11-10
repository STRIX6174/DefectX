#pragma once

#include "defect_class.h"

#include <string>
#include <map>

namespace qf {
namespace model {

struct DenoiseParams {
    bool enable = false;
    std::string method = "median";
    int kernel = 3;
};

struct ContrastParams {
    bool enable = false;
    std::string method = "clahe";
    double clip = 2.0;
};

struct SharpenParams {
    bool enable = false;
    std::string method = "unsharp";
    double sigma = 1.0;
};

struct PreprocessParams {
    DenoiseParams denoise;
    ContrastParams contrast;
    SharpenParams sharpen;
};

struct AlgorithmParams {
    std::string metric;      // 度量值名称（edge_density / area_ratio / ...）
    double threshold = 0.0;  // T_c，置信度 = clamp((M_c - T_c) / T_c, 0, 1)
    int cannyLow     = 50;
    int cannyHigh    = 150;
    int blockSize    = 31;
    int cValue       = 3;
    double histDiff  = 0.3;
    int tophatKernel = 15;
    int blobCountMin = 5;
    int closeKernel  = 5;
    double aspectRatio = 3.0;
    int minLength      = 80;
};

struct DecisionParams {
    std::string mode = "max_confidence";
    double minConfidence = 0.3;  // 最高置信度低于它则判"无法判定"
};

struct GateParams {
    int timeoutPerImageMs = 5000;
    int parallelThreads   = 6;
};

struct PipelineParams {
    int version = 3;
    PreprocessParams preprocess;
    std::map<DefectClass, AlgorithmParams> algorithms;
    DecisionParams decision;
    GateParams gate;
};

}  // namespace model
}  // namespace qf