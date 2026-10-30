#include "algorithm/defect_operator.h"
#include "model/pipeline_params.h"
#include "model/defect_type.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>
#include <opencv2/highgui.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <map>

namespace qf {
namespace algorithm {

namespace {

cv::Mat toGray(const cv::Mat& img) {
    if (img.channels() == 3) {
        cv::Mat gray;
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }
    return img;
}

} // anonymous

// =============================================================================
// [Cr] CrazingOperator — 龟裂（网状裂纹）检测
// =============================================================================
// 1. 检测目标
//    钢带表面因轧制应力集中产生的网状微裂纹，视觉上表现为密集的细线交叉网络。
//    核心特征：局部区域内边缘像素密度异常偏高。
//
// 2. 设计逻辑
//    a. Canny 边缘检测(50/150) → 提取所有边缘像素。
//    b. 遍历统计边缘像素数 → edgeDensity = 边缘像素 / 总像素。
//    c. edgeDensity > threshold(默认0.05) → 判定为龟裂。
//    d. confidence = min(1.0, density / (threshold * 2.0))，密度越高信心越强。
//    e. 输出整图 ROI（龟裂通常遍布区域，不做单轮廓定位）。
//
// 3. 可优化点
//    - Canny(50,150) 阈值硬编码，对不同光照/对比度的图像缺乏适应性；
//      应改用 Otsu 自动阈值或让 cannyLow/cannyHigh 从 DefectParam 中读取。
//    - 全局统一 density 判定丧失空间分布信息，可能误判仅边缘密集的正常纹理区域；
//      应引入分块(grid-based)密度分析，仅当多个相邻块的密度都超标时才报警。
//    - 未使用形态学去噪（如先 blur 或 morph_close），噪点会被计入边缘像素导致虚警。
//    - confidence 公式过于简单线性，可考虑用 sigmoid 做更平滑的信度映射。
// =============================================================================
class CrazingOperator : public DefectOperator {
public:
    explicit CrazingOperator(const model::DefectParam& p) : params_(p) {}

    model::DefectType type() const override { return model::DefectType::Crazing; }

    std::vector<model::DefectRecord> detect(const cv::Mat& img) override {
        std::vector<model::DefectRecord> results;
        cv::Mat gray = toGray(img);

        cv::Mat edges;
        cv::Canny(gray, edges, 50, 150);

        int edgePixels = 0;
        for (int y = 0; y < edges.rows; ++y) {
            for (int x = 0; x < edges.cols; ++x) {
                if (edges.at<uchar>(y, x) > 0) ++edgePixels;
            }
        }
        double totalPixels = static_cast<double>(edges.rows * edges.cols);
        double density = edgePixels / totalPixels;

        double threshold = params_.edgeDensity > 0.0 ? params_.edgeDensity : 0.05;
        if (density > threshold) {
            model::DefectRecord rec;
            rec.type       = model::DefectType::Crazing;
            rec.confidence = std::min(1.0, density / (threshold * 2.0));
            rec.area       = static_cast<double>(edgePixels);
            rec.roi        = model::Roi{0, 0, img.cols, img.rows};
            rec.threshold  = threshold;
            results.push_back(rec);
        }
        return results;
    }

private:
    model::DefectParam params_;
};

// =============================================================================
// [In] InclusionOperator — 夹杂物检测
// =============================================================================
// 1. 检测目标
//    炼钢过程中混入的非金属异物（脱氧产物、炉渣、耐火材料碎片等），轧制后嵌入表面。
//    核心特征：与基体灰度差异明显的孤立块状暗色区域，边界较清晰。
//
// 2. 设计逻辑
//    a. 自适应高斯阈值(ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY_INV)
//       → 每个像素根据 blockSize×blockSize 邻域动态计算阈值，暗色异物翻转为白。
//    b. findContours(RETR_EXTERNAL) → 提取所有外轮廓。
//    c. 面积过滤(area >= minArea, 默认30px) → 排除噪点小斑点。
//    d. 每个合格轮廓 → 输出一条 DefectRecord，携带 bbox ROI。
//    e. confidence = min(1.0, area / (imgArea * 0.05))，异物越大信心越强。
//
// 3. 可优化点
//    - blockSize 默认31较大（200×200图像中占15.5%），对小尺寸夹杂物可能被邻域平均抹去；
//      应引入多尺度（如 11/21/31 三尺度）分别检测后合并。
//    - THRESH_BINARY_INV 仅检测暗异物，无法覆盖比基体更亮的夹杂物（如某些氧化物）；
//      可增加一支 THRESH_BINARY 并行检测亮异物。
//    - C=3.0 对低对比度图像过于保守，弱对比度夹杂物会漏检；
//      可改用 C=0 或 1.0 并通过后续面积/形状过滤抑制虚警。
//    - confidence 仅与面积挂钩，未考虑"灰度差异度"，小但极黑的异物信心应更高。
//    - 可加入轮廓紧凑度/圆度过滤，排除长条状伪影。
// =============================================================================
class InclusionOperator : public DefectOperator {
public:
    explicit InclusionOperator(const model::DefectParam& p) : params_(p) {}

    model::DefectType type() const override { return model::DefectType::Inclusion; }

    std::vector<model::DefectRecord> detect(const cv::Mat& img) override {
        std::vector<model::DefectRecord> results;
        cv::Mat gray = toGray(img);

        int blockSize = params_.blockSize > 0 ? params_.blockSize : 31;
        if (blockSize % 2 == 0) ++blockSize;
        double cValue = params_.cValue > 0 ? static_cast<double>(params_.cValue) : 3.0;

        cv::Mat binary;
        cv::adaptiveThreshold(gray, binary, 255,
                              cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv::THRESH_BINARY_INV,
                              blockSize, cValue);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        int minArea = params_.minArea > 0 ? params_.minArea : 30;
        double imgArea = static_cast<double>(img.rows * img.cols);

        for (const auto& cnt : contours) {
            double area = cv::contourArea(cnt);
            if (area < minArea) continue;

            cv::Rect bbox = cv::boundingRect(cnt);
            double confidence = std::min(1.0, area / (imgArea * 0.05));

            model::DefectRecord rec;
            rec.type       = model::DefectType::Inclusion;
            rec.confidence = confidence;
            rec.area       = area;
            rec.roi        = model::Roi{bbox.x, bbox.y, bbox.width, bbox.height};
            rec.threshold  = static_cast<double>(minArea);
            results.push_back(rec);
        }
        return results;
    }

private:
    model::DefectParam params_;
};

// =============================================================================
// [Pa] PatchesOperator — 斑块检测
// =============================================================================
// 1. 检测目标
//    表面氧化不均、水渍残留或油污导致的区域性灰度异常区域。
//    核心特征：局部区域内灰度值波动（方差）显著高于正常表面。
//
// 2. 设计逻辑
//    a. 灰度图转 float → 计算 gray²。
//    b. 均值模糊(15×15)得到局部 E[X] 和 E[X²]。
//    c. Var = E[X²] - (E[X])² → 局部方差图（统计经典公式）。
//    d. threshold(Var > localVarThreshold, 默认500) → 高方差区域二值化。
//    e. 形态学闭运算(5×5椭圆核) → 将相邻高方差碎片合并成连续区域。
//    f. 轮廓提取 + 面积过滤(>50px) → 输出每个斑块记录。
//    g. confidence = min(1.0, area / 10000.0)。
//
// 3. 可优化点
//    - kernelSize=15 和 localVarThreshold=500 强耦合且硬编码；
//      不同分辨率和对比度下需独立调参，缺乏自适应归一化。
//    - 方差对光照渐变非常敏感，图像边缘渐变区域方差虚高会导致大面积误检；
//      应先用顶帽/同态滤波去除光照趋势，再计算方差。
//    - 闭运算 kernel 固定 5×5 椭圆，碎片化严重的斑块可能无法完全合并；
//      应改为可配置参数或对结果做连通域分析后按距离合并。
//    - 未区分"斑块方向性"，某些正常轧制纹理也可能局部方差高；
//      可加入纹理方向分析（如 Gabor 或梯度方向直方图）区分方向性纹理 vs 无方向斑块。
//    - 未使用 meanMat 和 stddevMat 变量（声明了但未使用），属死代码。
// =============================================================================
class PatchesOperator : public DefectOperator {
public:
    explicit PatchesOperator(const model::DefectParam& p) : params_(p) {}

    model::DefectType type() const override { return model::DefectType::Patches; }

    std::vector<model::DefectRecord> detect(const cv::Mat& img) override {
        std::vector<model::DefectRecord> results;
        cv::Mat gray = toGray(img);

        double localVarThreshold = params_.localVariance > 0.0 ? params_.localVariance : 500.0;

        cv::Mat meanMat, stddevMat;
        const int kernelSize = 15;
        cv::Mat grayFloat;
        gray.convertTo(grayFloat, CV_32F);
        cv::Mat graySq;
        cv::multiply(grayFloat, grayFloat, graySq);

        cv::Mat meanBlur, sqBlur;
        cv::blur(grayFloat, meanBlur, cv::Size(kernelSize, kernelSize));
        cv::blur(graySq, sqBlur, cv::Size(kernelSize, kernelSize));

        cv::Mat variance;
        cv::subtract(sqBlur, meanBlur.mul(meanBlur), variance);

        cv::Mat varMask;
        cv::threshold(variance, varMask, localVarThreshold, 255, cv::THRESH_BINARY);
        varMask.convertTo(varMask, CV_8U);

        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
        cv::morphologyEx(varMask, varMask, cv::MORPH_CLOSE, kernel);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(varMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        for (const auto& cnt : contours) {
            double area = cv::contourArea(cnt);
            if (area < 50.0) continue;

            cv::Rect bbox = cv::boundingRect(cnt);
            double confidence = std::min(1.0, area / 10000.0);

            model::DefectRecord rec;
            rec.type       = model::DefectType::Patches;
            rec.confidence = confidence;
            rec.area       = area;
            rec.roi        = model::Roi{bbox.x, bbox.y, bbox.width, bbox.height};
            rec.threshold  = localVarThreshold;
            results.push_back(rec);
        }
        return results;
    }

private:
    model::DefectParam params_;
};

// =============================================================================
// [PS] PittedSurfaceOperator — 麻点/点蚀面检测
// =============================================================================
// 1. 检测目标
//    轧辊磨损或酸洗过度导致表面出现大量微小的点状凹坑。
//    核心特征：大量（≥5个）小斑点（3~200px²）同时存在，单个斑点不重要，集群才致命。
//
// 2. 设计逻辑
//    a. Top-hat 形态学(15×15椭圆核) → 提取比结构元素更小的亮/暗局部细节。
//       TopHat = src - opening(src)，去除大面积背景，只留下小尺度特征。
//    b. threshold(tophat, 30) → 二值化提取斑点候选区域。
//    c. findContours → 遍历每个轮廓，统计面积在 (3, 200) 区间的有效斑点。
//    d. validBlobs >= blobCountMin(默认5) → 判定为点蚀面。
//    e. confidence = min(1.0, validBlobs / (blobCountMin * 4.0))。
//
// 3. 可优化点
//    - tophatKernel=15 硬编码：若实际点蚀直径大于15px则被滤除，小于3px被面积下限过滤；
//      应引入多尺度 Top-hat（如 7/15/25）分别检测后合并结果。
//    - threshold(tophat, 30) 硬编码：对低对比度点蚀（如浅凹坑）会漏检；
//      应使用 Otsu 自动阈值或可配置参数。
//    - 面积区间(3, 200) 仅适合 NEU-DET 的 200×200 图像，不同分辨率需等比缩放。
//    - 未区分凹坑与凸点、灰尘噪点：Top-hat 提取的是亮细节，点蚀凹坑在暗场照明下呈现亮点；
//      实际应用中应根据打光方向选择 Top-hat（亮）或 Black-hat（暗）。
//    - blobCountMin=5 过于简单，点蚀严重程度应同时考虑密度（每单位面积斑点数）而非绝对数量。
// =============================================================================
class PittedSurfaceOperator : public DefectOperator {
public:
    explicit PittedSurfaceOperator(const model::DefectParam& p) : params_(p) {}

    model::DefectType type() const override { return model::DefectType::PittedSurface; }

    std::vector<model::DefectRecord> detect(const cv::Mat& img) override {
        std::vector<model::DefectRecord> results;
        cv::Mat gray = toGray(img);

        int ksize = params_.tophatKernel > 0 ? params_.tophatKernel : 15;
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
                                                    cv::Size(ksize, ksize));
        cv::Mat tophat;
        cv::morphologyEx(gray, tophat, cv::MORPH_TOPHAT, kernel);

        cv::Mat binary;
        cv::threshold(tophat, binary, 30, 255, cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        int blobCountMin = params_.blobCountMin > 0 ? params_.blobCountMin : 5;
        int validBlobs = 0;
        for (const auto& cnt : contours) {
            double area = cv::contourArea(cnt);
            if (area > 3.0 && area < 200.0) {
                ++validBlobs;
            }
        }

        if (validBlobs >= blobCountMin) {
            model::DefectRecord rec;
            rec.type       = model::DefectType::PittedSurface;
            rec.confidence = std::min(1.0, static_cast<double>(validBlobs) / (blobCountMin * 4.0));
            rec.area       = static_cast<double>(validBlobs);
            rec.roi        = model::Roi{0, 0, img.cols, img.rows};
            rec.threshold  = static_cast<double>(blobCountMin);
            results.push_back(rec);
        }
        return results;
    }

private:
    model::DefectParam params_;
};

// =============================================================================
// [Sc] ScratchesOperator — 划痕检测
// =============================================================================
// 1. 检测目标
//    钢带在传送/卷取过程中与设备部件机械摩擦产生的细长线性划伤。
//    核心特征：细长直线或弧线，长度远大于宽度（长宽比 ≥ 3:1），方向通常一致。
//
// 2. 设计逻辑
//    a. Canny 边缘检测(cannyLow/cannyHigh) → 提取所有边缘，划痕边缘响应强。
//    b. HoughLinesP 概率霍夫直线检测(投票阈值=30, minLength=80, maxGap=10)
//       → 从边缘图中提取直线段，过滤散乱噪点。
//    c. 每条线段计算长宽比(aspect = length / 2.0)，过滤 aspect < minAspectRatio(默认3.0)。
//       → 排除块状/点状边缘碎片。
//    d. 每条合格线段 → 输出 DefectRecord，bbox 由线段端点外扩构成。
//    e. confidence = min(1.0, length / 400.0)，划痕越长信心越强。
//
// 3. 可优化点
//    - 宽度硬编码为 2.0：HoughLinesP 返回的线段没有宽度信息，
//      实际划痕宽度可能在 1~10px，用固定值算长宽比会低估宽划痕的 aspect。
//      应在线段位置做垂向灰度剖面分析，估算真实宽度。
//    - HoughLinesP 投票阈值=30、minLength=80、maxGap=10 全部硬编码：
//      当图像分辨率变化时这些参数需要等比调整。
//    - Hough 对弧线划痕不敏感：仅检测直线段，弯曲划痕会被拆成多个短段然后被 minLength 淘汰；
//      可补充基于轮廓曲率的弧线检测分支。
//    - 未做线段合并：同一条划痕因边缘断裂可能被 Hough 拆成多条线段，
//      导致重复输出。应加入共线合并（角度差<5° 且端点间距<maxGap 的线段合并）。
//    - cannyLow/cannyHigh 虽然可从 params 读取，但默认值 50/150 对弱对比度划痕可能太高；
//      应增加对比度拉伸预处理或改用 auto-canny（中位数法自动选阈值）。
// =============================================================================
class ScratchesOperator : public DefectOperator {
public:
    explicit ScratchesOperator(const model::DefectParam& p) : params_(p) {}

    model::DefectType type() const override { return model::DefectType::Scratches; }

    std::vector<model::DefectRecord> detect(const cv::Mat& img) override {
        std::vector<model::DefectRecord> results;
        cv::Mat gray = toGray(img);

        int cannyLow  = params_.cannyLow  > 0 ? params_.cannyLow  : 50;
        int cannyHigh = params_.cannyHigh > 0 ? params_.cannyHigh : 150;

        cv::Mat edges;
        cv::Canny(gray, edges, cannyLow, cannyHigh);

        std::vector<cv::Vec4i> lines;
        cv::HoughLinesP(edges, lines, 1, CV_PI / 180, 30,
                        params_.minLength > 0 ? static_cast<double>(params_.minLength) : 80.0,
                        10.0);

        double minAspectRatio = params_.aspectRatio > 0.0 ? params_.aspectRatio : 3.0;

        for (const auto& line : lines) {
            double dx = static_cast<double>(line[2] - line[0]);
            double dy = static_cast<double>(line[3] - line[1]);
            double length = std::sqrt(dx * dx + dy * dy);
            double width  = 2.0;
            double aspect = length / width;

            if (aspect < minAspectRatio) continue;

            int x = std::min(line[0], line[2]);
            int y = std::min(line[1], line[3]);
            int w = std::max(std::abs(line[2] - line[0]), 5);
            int h = std::max(std::abs(line[3] - line[1]), 5);

            model::DefectRecord rec;
            rec.type       = model::DefectType::Scratches;
            rec.confidence = std::min(1.0, length / 400.0);
            rec.area       = length * width;
            rec.roi        = model::Roi{x, y, w, h};
            rec.threshold  = minAspectRatio;
            results.push_back(rec);
        }
        return results;
    }

private:
    model::DefectParam params_;
};

// =============================================================================
// [RS] RolledInScaleOperator — 轧入氧化皮检测
// =============================================================================
// 1. 检测目标
//    热轧时钢坯表面氧化铁皮被压入钢带，形成片状/层状嵌入的暗色纹理区域。
//    核心特征：大范围纹理密度异常（前景占比 > 15%），呈鱼鳞状或层叠状分布。
//
// 2. 设计逻辑
//    a. 自适应高斯阈值(ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY)
//       blockSize=21, C=2.0 → 将比邻域略亮的氧化皮纹理翻转出来（与 Inclusion 的 INVERSE 相反）。
//    b. 形态学闭运算(矩形核 closeKernel×closeKernel) → 连接碎片化的氧化皮区域。
//    c. findContours → 统计全部前景总面积 → textureDensity = 前景/总像素。
//    d. textureDensity > 0.15(15%) → 总体判定为氧化皮缺陷。
//    e. 对面积 ≥ 100px 的单个轮廓逐一输出记录。
//    f. confidence = min(1.0, textureDensity / 0.5)。
//
// 3. 可优化点
//    - 全局 textureDensity 判定导致：若一个 1000×1000 大图中只有一小块氧化皮，
//      密度会远低于 15% → 漏检。应改为分块局部密度分析后再汇总。
//    - adaptiveThreshold 的 C=2.0 和 blockSize=21 硬编码且与 Inclusion 算子共享参数名
//      （都用 blockSize），但语义不同（In=暗异物，RS=亮纹理），
//      导致参数调优时互相牵制。应拆分独立参数。
//    - 形态学闭运算默认 5×5 矩形核：对稀疏大纹理可能不足以连接，
//      对密集小纹理可能过度合并。应设为可配置且根据 blockSize 联动缩放。
//    - kDensityThreshold=0.15 和 confidence 分母 0.5 均为魔术数字，
//      缺乏统计依据，应基于数据集标定（如 F1-score 曲线取最佳阈值）。
//    - 面积下限 100px 在 200×200 图像中占 0.25%，对于更小块的氧化皮碎片会漏掉；
//      应与图像尺寸联动。
//    - 未考虑纹理的方向性：轧入氧化皮通常沿轧制方向拉长，
//      可加入方向滤波（如定向 Gabor）增强与方向性噪声的区分度。
// =============================================================================
class RolledInScaleOperator : public DefectOperator {
public:
    explicit RolledInScaleOperator(const model::DefectParam& p) : params_(p) {}

    model::DefectType type() const override { return model::DefectType::RolledInScale; }

    std::vector<model::DefectRecord> detect(const cv::Mat& img) override {
        std::vector<model::DefectRecord> results;
        cv::Mat gray = toGray(img);

        int blockSize = params_.blockSize > 0 ? params_.blockSize : 21;
        if (blockSize % 2 == 0) ++blockSize;
        int closeKernelSize = params_.closeKernel > 0 ? params_.closeKernel : 5;

        cv::Mat binary;
        cv::adaptiveThreshold(gray, binary, 255,
                              cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv::THRESH_BINARY,
                              blockSize, 2.0);

        cv::Mat closeKernel = cv::getStructuringElement(cv::MORPH_RECT,
                                                         cv::Size(closeKernelSize, closeKernelSize));
        cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, closeKernel);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        double totalForeground = 0.0;
        double imgArea = static_cast<double>(img.rows * img.cols);
        for (const auto& cnt : contours) {
            totalForeground += cv::contourArea(cnt);
        }
        double textureDensity = totalForeground / imgArea;

        constexpr double kDensityThreshold = 0.15;
        if (textureDensity > kDensityThreshold) {
            for (const auto& cnt : contours) {
                double area = cv::contourArea(cnt);
                if (area < 100.0) continue;

                cv::Rect bbox = cv::boundingRect(cnt);
                model::DefectRecord rec;
                rec.type       = model::DefectType::RolledInScale;
                rec.confidence = std::min(1.0, textureDensity / 0.5);
                rec.area       = area;
                rec.roi        = model::Roi{bbox.x, bbox.y, bbox.width, bbox.height};
                rec.threshold  = kDensityThreshold;
                results.push_back(rec);
            }
        }
        return results;
    }

private:
    model::DefectParam params_;
};

std::unique_ptr<DefectOperator> createDefectOperator(
    model::DefectType type, const model::DefectParam& params) {
    switch (type) {
    case model::DefectType::Crazing:
        return std::make_unique<CrazingOperator>(params);
    case model::DefectType::Inclusion:
        return std::make_unique<InclusionOperator>(params);
    case model::DefectType::Patches:
        return std::make_unique<PatchesOperator>(params);
    case model::DefectType::PittedSurface:
        return std::make_unique<PittedSurfaceOperator>(params);
    case model::DefectType::Scratches:
        return std::make_unique<ScratchesOperator>(params);
    case model::DefectType::RolledInScale:
        return std::make_unique<RolledInScaleOperator>(params);
    }
    return nullptr;
}

}
}