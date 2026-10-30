#include "algorithm/qc_operator.h"
#include "model/pipeline_params.h"

#include <opencv2/imgproc.hpp>

namespace qf {
namespace algorithm {

class QcOperatorCv : public QcOperator {
public:
    explicit QcOperatorCv(const model::PipelineParams& params)
        : laplacianThreshold_(params.laplacianThreshold)
        , overexposureHigh_(params.overexposureHigh)
        , underexposureLow_(params.underexposureLow)
    {}

    model::QcResult evaluate(const cv::Mat& img) override {
        model::QcResult result;
        result.passed = true;

        cv::Mat gray;
        if (img.channels() == 3) {
            cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = img;
        }

        // 【QC-1 模糊检测】Laplacian方差法
        // 原理：Laplacian是二阶导数算子，对灰度突变（边缘）响应强。
        //       清晰图像边缘锐利 → Laplacian值剧烈正负震荡 → 方差大（通常>150）。
        //       模糊图像边缘被抹平 → Laplacian值趋近于0 → 方差小（通常<60）。
        //       因此 Laplacian值的方差 可直接量化图像清晰度，一个数值定胜负。
        // 条件：laplacianVariance < laplacianThreshold（默认100）→ 拒收为模糊。
        cv::Mat lap;
        cv::Laplacian(gray, lap, CV_64F);
        cv::Scalar mean, stddev;
        cv::meanStdDev(lap, mean, stddev);
        result.laplacianVariance = stddev.val[0] * stddev.val[0];

        if (result.laplacianVariance < laplacianThreshold_) {
            result.passed = false;
            result.reason = model::QcRejectReason::Blurry;
            return result;
        }

        // 【QC-2 过曝检测】【QC-3 欠曝检测】直方图极端像素占比法
        // 原理：相机sensor动态范围有限，过曝时高亮像素被"顶"到255附近饱和，
        //       欠曝时暗部像素被"压"到0附近截止，二者都会导致表面细节永久丢失。
        //       缺陷检测依赖灰度对比度，若大多数像素已处于无效区，检测无法进行。
        // 条件：
        //   - 灰度 > overexposureHigh_(默认245) 的像素占比 > 85% → 拒收为过曝。
        //     (用245而非255，因为sensor在接近饱和时已非线性压缩，245是警戒线)
        //   - 灰度 ≤ underexposureLow_(默认10) 的像素占比 > 85% → 拒收为欠曝。
        //     (用10而非0，因为暗电流噪声会随机抬高暗部像素值，<10即被噪声淹没)
        int histSize = 256;
        float range[] = {0, 256};
        const float* histRange = {range};
        cv::Mat hist;
        cv::calcHist(&gray, 1, nullptr, cv::Mat(), hist, 1, &histSize, &histRange);

        int totalPixels = gray.rows * gray.cols;
        double overCount = 0.0;
        double underCount = 0.0;

        for (int i = 0; i <= underexposureLow_; ++i) {
            underCount += hist.at<float>(i);
        }
        for (int i = overexposureHigh_; i < 256; ++i) {
            overCount += hist.at<float>(i);
        }

        result.overexposureRatio = overCount / totalPixels;
        result.underexposureRatio = underCount / totalPixels;

        constexpr double kOverThreshold = 0.85;
        constexpr double kUnderThreshold = 0.85;

        if (result.overexposureRatio > kOverThreshold) {
            result.passed = false;
            result.reason = model::QcRejectReason::Overexposed;   // >85%像素近饱和 → 过曝
        } else if (result.underexposureRatio > kUnderThreshold) {
            result.passed = false;
            result.reason = model::QcRejectReason::Underexposed;  // >85%像素近截止 → 欠曝
        }

        return result;
    }

private:
    double laplacianThreshold_;
    int    overexposureHigh_;
    int    underexposureLow_;
};

std::unique_ptr<QcOperator> createQcOperator(const model::PipelineParams& params) {
    return std::make_unique<QcOperatorCv>(params);
}

}
}