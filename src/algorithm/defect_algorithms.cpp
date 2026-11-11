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

// ============================================================
// [Cr] Crazing — 龟裂（网状交错细裂纹）
//   数学原理：M = 边缘像素密度 = countNonZero(Canny(gray)) / 总像素。
//     Canny 用 Sobel 梯度幅值做双阈值 + 迟滞连接，得到强边缘二值图；
//     龟裂的细密网状裂纹会显著抬高"边缘像素占比"。
//   选择原因：龟裂最稳定、最廉价的特征就是边缘密度，对光照变化不敏感，
//     无需灰度共生矩阵等纹理统计即可区分平滑/裂纹表面，
//     契合"手写算子、无深度学习"的约束。
// ============================================================
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

// ============================================================
// [In] Inclusion — 夹杂（异质斑点/不规则暗色区域）
//   数学原理：M = 异常区域面积占比 = Σ polygonArea(轮廓) / 总像素。
//     adaptiveThreshold 以"局部高斯加权均值 − C"为阈值，THRESH_BINARY_INV
//     提取比邻域明显更暗的斑点；轮廓面积用手写 shoelace（鞋带）公式累加。
//   选择原因：夹杂是"与周围灰度反差大的暗色团块"，局部自适应阈值能抗
//     光照不均；手写多边形面积可避免 OpenCV5 把 contourArea 移入 geometry
//     模块带来的依赖，守住"仅 imgproc 算子"边界。
// ============================================================
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

// ============================================================
// [Pa] Patches — 斑块（成片异常纹理）
//   数学原理：M = 局部灰度方差 = E[X²] − E[X]²（二阶中心矩估计）。
//     用盒式均值滤波分别对 X 与 X² 求局部均值再相减，得方差图后取全图
//     均值；等价于滑动窗口内的方差统计。
//   选择原因：斑块区域灰度波动大，方差是"纹理起伏"最直接的度量；
//     box blur 近似积分图，复杂度低，且对旋转/尺度不敏感。
// ============================================================
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

// ============================================================
// [PS] Pitted Surface — 麻面（密集点状凹陷）
//   数学原理：M = 顶帽斑点数量。形态学顶帽 = 原图 − 开运算(原图)，
//     用椭圆结构元素提取"比 SE 更小"的亮/暗细节；阈值化后统计面积
//     落在 [3, 200] 的小轮廓个数。
//   选择原因：麻面是大量微小坑点，顶帽变换专门分离小尺度特征；
//     统计"合理尺寸斑点计数"比面积占比更贴合麻面的密集点状形态。
// ============================================================
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

// ============================================================
// [Sc] Scratches — 划痕（细长线状痕迹，多方向）
//   数学原理：M = 长线段总长。Canny 提取边缘后，用概率霍夫变换
//     HoughLinesP 在参数空间 (ρ, θ) 投票拟合直线段；过滤长宽比过低的
//     线段后，对满足条件的线段做 Σ√(dx²+dy²) 长度累加。
//   选择原因：划痕本质是细长直线结构，霍夫直线是直接建模线结构的经典
//     方法；累加长度对方向鲁棒，比 blob 面积更能刻画划痕严重程度。
// ============================================================
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

// ============================================================
// [RS] Rolled-in Scale — 轧入氧化铁皮（片状/条带状不规则纹理）
//   数学原理：M = 纹理密度 = 前景像素 / 总像素。
//     adaptiveThreshold（高斯、THRESH_BINARY）提取与邻域灰度差异明显的
//     纹理，形态学闭运算（先膨胀后腐蚀）连接断裂片段，再统计前景占比。
//   选择原因：氧化铁皮呈大片不规则纹理，局部阈值 + 闭运算能把碎片连成
//     整体；前景占比作为纹理密度，计算简单且能反映缺陷覆盖范围。
// ============================================================
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