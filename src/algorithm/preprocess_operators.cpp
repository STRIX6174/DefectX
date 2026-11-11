#include "algorithm/preprocess_operators.h"

#include <opencv2/imgproc.hpp>

namespace qf {
namespace algorithm {

bool isValidInput(const cv::Mat& img) {
    return !img.empty() && img.rows > 0 && img.cols > 0;
}

// ============================================================
// 灰度化 toGray
//   数学原理：Y = 0.299·R + 0.587·G + 0.114·B（BT.601 亮度加权），
//     COLOR_BGR2GRAY 即按此权重对三通道做线性组合；单通道原样返回。
//   选择原因：NEU-CLS 本就是灰度图；统一成单通道让六类算法共享同一
//     输入、规避颜色噪声干扰，并降低后续计算量。
// ============================================================
cv::Mat toGray(const cv::Mat& img) {
    if (img.channels() == 1) {
        return img;
    }
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

// ============================================================
// 尺寸标准化 resizeToStandard
//   数学原理：缩放到 200×200；INTER_AREA 在降采样时对像素做区域平均
//     （等价于先低通滤波再抽样），抑制混叠与锯齿。
//   选择原因：统一尺寸消除尺度差异，使阈值、结构元素等绝对参数在不同
//     来源图像上可比；降采样用 INTER_AREA 避免摩尔纹/锯齿。
// ============================================================
cv::Mat resizeToStandard(const cv::Mat& gray, int size) {
    if (size <= 0) size = 200;
    if (gray.rows == size && gray.cols == size) {
        return gray;
    }
    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(size, size), 0, 0, cv::INTER_AREA);
    return resized;
}

// ============================================================
// 去噪 applyDenoise（gaussian / bilateral / median）
//   数学原理：Gaussian 用高斯核做加权平均；median 取邻域中值（顺序
//     统计量，非线性，可去椒盐噪声）；bilateral 在高斯空间权重上再乘
//     灰度域权重，实现"保边平滑"。
//   选择原因：在边缘/纹理分析前抑制传感器噪声；中值针对点状噪声、
//     双边在保留裂纹边缘的同时平滑平坦区，避免噪声被误判为缺陷。
// ============================================================
cv::Mat applyDenoise(const cv::Mat& gray, const model::DenoiseParams& p) {
    if (!p.enable) return gray;

    cv::Mat out;
    int kernel = p.kernel > 0 ? p.kernel : 3;
    if (kernel % 2 == 0) ++kernel;

    if (p.method == "gaussian") {
        cv::GaussianBlur(gray, out, cv::Size(kernel, kernel), 0);
    } else if (p.method == "bilateral") {
        cv::bilateralFilter(gray, out, kernel, kernel * 2.0, kernel / 2.0);
    } else {
        // 默认 median
        cv::medianBlur(gray, out, kernel);
    }
    return out;
}

// ============================================================
// 对比度增强 applyContrast（equalize / clahe）
//   数学原理：直方图均衡化用累积分布函数 CDF 把灰度重映射到近似均匀
//     分布；CLAHE 分块并限制 CDF 斜率（clip limit），再用双线性插值
//     拼接，防止局部对比度被过度放大。
//   选择原因：统一光照、增强微弱缺陷的可辨度；CLAHE 比全局均衡更克制，
//     不会把噪声一起放大，适合灰度缓慢变化的钢表面。
// ============================================================
cv::Mat applyContrast(const cv::Mat& gray, const model::ContrastParams& p) {
    if (!p.enable) return gray;

    cv::Mat out;
    if (p.method == "equalize") {
        cv::equalizeHist(gray, out);
    } else {
        // 默认 clahe
        double clip = p.clip > 0.0 ? p.clip : 2.0;
        auto clahe = cv::createCLAHE(clip, cv::Size(8, 8));
        clahe->apply(gray, out);
    }
    return out;
}

// ============================================================
// 锐化 applySharpen（laplacian / unsharp mask）
//   数学原理：非锐化掩模 out = (1+α)·原图 − α·高斯模糊 ≈ 原图 +
//     α·(原图 − 模糊) = 原图 + α·高通分量；拉普拉斯用 ∇²（二阶导数）
//     提取边缘后反相叠加。
//   选择原因：锐化拉高细裂纹/划痕的边缘对比度，利于后续 Canny 与霍夫
//     直线检测；非锐化掩模噪声比拉普拉斯更小、更稳定，故为默认。
// ============================================================
cv::Mat applySharpen(const cv::Mat& gray, const model::SharpenParams& p) {
    if (!p.enable) return gray;

    cv::Mat out;
    if (p.method == "laplacian") {
        cv::Mat lap;
        cv::Laplacian(gray, lap, CV_16S, 3);
        cv::Mat lapAbs;
        cv::convertScaleAbs(lap, lapAbs);
        cv::addWeighted(gray, 1.0, lapAbs, -0.5, 0, out);
    } else {
        // 默认 unsharp mask
        double sigma = p.sigma > 0.0 ? p.sigma : 1.0;
        cv::Mat blurred;
        cv::GaussianBlur(gray, blurred, cv::Size(0, 0), sigma);
        cv::addWeighted(gray, 1.5, blurred, -0.5, 0, out);
    }
    return out;
}

// ============================================================
// 预处理流水线 preprocess
//   数学原理：有效性检查 → 灰度化 → 200×200 标准化 → 可选去噪/对比度/
//     锐化，各算子按配置开关串联（默认全关，保证基准可复现）。
//   选择原因：把"任意通道/尺寸"的输入收敛为"统一单通道灰度"，作为六类
//     算法共享的确定性前端；分步可选便于消融实验与调参。
// ============================================================
cv::Mat preprocess(const cv::Mat& input, const model::PreprocessParams& p) {
    if (!isValidInput(input)) {
        return cv::Mat();
    }

    cv::Mat gray = toGray(input);
    gray = resizeToStandard(gray, 200);

    gray = applyDenoise(gray, p.denoise);
    gray = applyContrast(gray, p.contrast);
    gray = applySharpen(gray, p.sharpen);

    return gray;
}

}  // namespace algorithm
}  // namespace qf