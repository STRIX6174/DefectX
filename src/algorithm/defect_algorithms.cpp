#include "algorithm/defect_algorithms.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace qf {
namespace algorithm {

namespace {

// 置信度 = clamp((M - T) / T, 0, 1)：度量值超出阈值的相对余量，非概率。
double confidenceFromMargin(double metric, double threshold) {
    if (threshold <= 0.0) return 0.0;
    double margin = (metric - threshold) / threshold;
    return std::max(0.0, std::min(1.0, margin));
}

int oddKernel(int k) {
    if (k < 1) k = 1;
    if (k % 2 == 0) ++k;
    return k;
}

// 轮廓面积（shoelace 公式）。
// OpenCV 5 将 contourArea 移至 geometry 模块，为保持"仅 imgproc 算子"
// 的约束，这里用手写多边形面积替代。
double polygonArea(const std::vector<cv::Point>& pts) {
    if (pts.size() < 3) return 0.0;
    double area = 0.0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const cv::Point& p0 = pts[i];
        const cv::Point& p1 = pts[(i + 1) % pts.size()];
        area += static_cast<double>(p0.x) * p1.y
              - static_cast<double>(p1.x) * p0.y;
    }
    return std::abs(area) * 0.5;
}

}  // namespace

// ---------------------------------------------------------------------------
// [Cr] Crazing — 龟裂：网状交错细裂纹
// 度量值 M_c = 边缘像素占比（edge_density）。
// 细密裂纹使 Canny 边缘像素密度显著高于平滑表面。
// ---------------------------------------------------------------------------
model::DefectScore detectCrazing(const cv::Mat& gray, const model::AlgorithmParams& p) {
    model::DefectScore s;
    s.defectClass = model::DefectClass::Crazing;

    int low  = p.cannyLow  > 0 ? p.cannyLow  : 50;
    int high = p.cannyHigh > 0 ? p.cannyHigh : 150;

    cv::Mat edges;
    cv::Canny(gray, edges, low, high);

    int edgePixels = cv::countNonZero(edges);
    double totalPixels = static_cast<double>(edges.rows * edges.cols);
    double density = (totalPixels > 0.0) ? edgePixels / totalPixels : 0.0;

    s.metricValue = density;
    s.threshold   = (p.threshold > 0.0) ? p.threshold : 0.05;
    s.confidence  = confidenceFromMargin(density, s.threshold);
    return s;
}

// ---------------------------------------------------------------------------
// [In] Inclusion — 夹杂：异质斑点/不规则暗色区域
// 度量值 M_c = 异常区域面积占比（area_ratio）。
// 自适应阈值提取与邻域灰度差异明显的暗色区域，累计面积占比。
// ---------------------------------------------------------------------------
model::DefectScore detectInclusion(const cv::Mat& gray, const model::AlgorithmParams& p) {
    model::DefectScore s;
    s.defectClass = model::DefectClass::Inclusion;

    int blockSize = oddKernel(p.blockSize > 0 ? p.blockSize : 31);
    double cValue = (p.cValue > 0) ? static_cast<double>(p.cValue) : 3.0;

    cv::Mat binary;
    cv::adaptiveThreshold(gray, binary, 255,
                          cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV,
                          blockSize, cValue);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double totalArea = 0.0;
    for (const auto& cnt : contours) {
        totalArea += polygonArea(cnt);
    }
    double totalPixels = static_cast<double>(gray.rows * gray.cols);
    double areaRatio = (totalPixels > 0.0) ? totalArea / totalPixels : 0.0;

    s.metricValue = areaRatio;
    s.threshold   = (p.threshold > 0.0) ? p.threshold : 0.08;
    s.confidence  = confidenceFromMargin(areaRatio, s.threshold);
    return s;
}

// ---------------------------------------------------------------------------
// [Pa] Patches — 斑块：成片异常纹理，灰度差异
// 度量值 M_c = 局部灰度方差（local_variance，取整图均值）。
// 斑块区域灰度波动大，局部方差显著高于均匀表面。
// ---------------------------------------------------------------------------
model::DefectScore detectPatches(const cv::Mat& gray, const model::AlgorithmParams& p) {
    model::DefectScore s;
    s.defectClass = model::DefectClass::Patches;

    cv::Mat grayFloat;
    gray.convertTo(grayFloat, CV_32F);

    cv::Mat graySq;
    cv::multiply(grayFloat, grayFloat, graySq);

    const int kernelSize = 15;
    cv::Mat meanBlur, sqBlur;
    cv::blur(grayFloat, meanBlur, cv::Size(kernelSize, kernelSize));
    cv::blur(graySq, sqBlur, cv::Size(kernelSize, kernelSize));

    cv::Mat variance;
    cv::subtract(sqBlur, meanBlur.mul(meanBlur), variance);

    cv::Scalar meanVar = cv::mean(variance);
    double localVariance = meanVar.val[0];

    s.metricValue = localVariance;
    s.threshold   = (p.threshold > 0.0) ? p.threshold : 500.0;
    s.confidence  = confidenceFromMargin(localVariance, s.threshold);
    return s;
}

// ---------------------------------------------------------------------------
// [PS] Pitted Surface — 麻面：密集点状凹陷
// 度量值 M_c = 顶帽暗点数量（blob_count）。
// 顶帽变换提取比结构元素更小的亮/暗细节，统计尺寸合理的小斑点数量。
// ---------------------------------------------------------------------------
model::DefectScore detectPittedSurface(const cv::Mat& gray, const model::AlgorithmParams& p) {
    model::DefectScore s;
    s.defectClass = model::DefectClass::PittedSurface;

    int ksize = oddKernel(p.tophatKernel > 0 ? p.tophatKernel : 15);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(ksize, ksize));

    cv::Mat tophat;
    cv::morphologyEx(gray, tophat, cv::MORPH_TOPHAT, kernel);

    cv::Mat binary;
    cv::threshold(tophat, binary, 30, 255, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int validBlobs = 0;
    for (const auto& cnt : contours) {
        double area = polygonArea(cnt);
        if (area > 3.0 && area < 200.0) {
            ++validBlobs;
        }
    }

    s.metricValue = static_cast<double>(validBlobs);
    s.threshold   = (p.threshold > 0.0) ? p.threshold
                                        : static_cast<double>(p.blobCountMin > 0 ? p.blobCountMin : 5);
    s.confidence  = confidenceFromMargin(static_cast<double>(validBlobs), s.threshold);
    return s;
}

// ---------------------------------------------------------------------------
// [Sc] Scratches — 划痕：细长线状痕迹，多方向
// 度量值 M_c = 长线段总长（line_length）。
// Canny + 概率霍夫直线提取，过滤长宽比过低线段后累加长度。
// ---------------------------------------------------------------------------
model::DefectScore detectScratches(const cv::Mat& gray, const model::AlgorithmParams& p) {
    model::DefectScore s;
    s.defectClass = model::DefectClass::Scratches;

    int cannyLow  = p.cannyLow  > 0 ? p.cannyLow  : 50;
    int cannyHigh = p.cannyHigh > 0 ? p.cannyHigh : 150;

    cv::Mat edges;
    cv::Canny(gray, edges, cannyLow, cannyHigh);

    double minLength = p.minLength > 0 ? static_cast<double>(p.minLength) : 80.0;
    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, 1, CV_PI / 180, 30, minLength, 10.0);

    double minAspectRatio = p.aspectRatio > 0.0 ? p.aspectRatio : 3.0;
    double totalLength = 0.0;

    for (const auto& line : lines) {
        double dx = static_cast<double>(line[2] - line[0]);
        double dy = static_cast<double>(line[3] - line[1]);
        double length = std::sqrt(dx * dx + dy * dy);
        double width  = 2.0;  // Hough 线段无宽度信息，用固定值估算长宽比
        if (length / width < minAspectRatio) continue;
        totalLength += length;
    }

    s.metricValue = totalLength;
    s.threshold   = (p.threshold > 0.0) ? p.threshold : 80.0;
    s.confidence  = confidenceFromMargin(totalLength, s.threshold);
    return s;
}

// ---------------------------------------------------------------------------
// [RS] Rolled-in Scale — 轧入氧化铁皮：片状/条带状不规则纹理
// 度量值 M_c = 纹理密度（texture_density）。
// 自适应阈值提取与邻域灰度差异明显的纹理，形态学闭合后统计前景占比。
// ---------------------------------------------------------------------------
model::DefectScore detectRolledInScale(const cv::Mat& gray, const model::AlgorithmParams& p) {
    model::DefectScore s;
    s.defectClass = model::DefectClass::RolledInScale;

    int blockSize = oddKernel(p.blockSize > 0 ? p.blockSize : 21);
    int closeKernelSize = p.closeKernel > 0 ? p.closeKernel : 5;

    cv::Mat binary;
    cv::adaptiveThreshold(gray, binary, 255,
                          cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY,
                          blockSize, 2.0);

    cv::Mat closeKernel = cv::getStructuringElement(cv::MORPH_RECT,
                                                     cv::Size(closeKernelSize, closeKernelSize));
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, closeKernel);

    int foreground = cv::countNonZero(binary);
    double totalPixels = static_cast<double>(gray.rows * gray.cols);
    double textureDensity = (totalPixels > 0.0) ? foreground / totalPixels : 0.0;

    s.metricValue = textureDensity;
    s.threshold   = (p.threshold > 0.0) ? p.threshold : 0.3;
    s.confidence  = confidenceFromMargin(textureDensity, s.threshold);
    return s;
}

model::DefectScore runSingleAlgorithm(model::DefectClass type,
                                      const cv::Mat& gray,
                                      const model::AlgorithmParams& p) {
    switch (type) {
    case model::DefectClass::Crazing:       return detectCrazing(gray, p);
    case model::DefectClass::Inclusion:     return detectInclusion(gray, p);
    case model::DefectClass::Patches:       return detectPatches(gray, p);
    case model::DefectClass::PittedSurface: return detectPittedSurface(gray, p);
    case model::DefectClass::Scratches:     return detectScratches(gray, p);
    case model::DefectClass::RolledInScale: return detectRolledInScale(gray, p);
    }
    model::DefectScore s;
    s.defectClass = type;
    return s;
}

}  // namespace algorithm
}  // namespace qf