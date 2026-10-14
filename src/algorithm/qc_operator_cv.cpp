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
            result.reason = model::QcRejectReason::Overexposed;
        } else if (result.underexposureRatio > kUnderThreshold) {
            result.passed = false;
            result.reason = model::QcRejectReason::Underexposed;
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