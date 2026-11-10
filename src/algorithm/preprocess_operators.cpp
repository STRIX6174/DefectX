#include "algorithm/preprocess_operators.h"

#include <opencv2/imgproc.hpp>

namespace qf {
namespace algorithm {

bool isValidInput(const cv::Mat& img) {
    return !img.empty() && img.rows > 0 && img.cols > 0;
}

cv::Mat toGray(const cv::Mat& img) {
    if (img.channels() == 1) {
        return img;
    }
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

cv::Mat resizeToStandard(const cv::Mat& gray, int size) {
    if (size <= 0) size = 200;
    if (gray.rows == size && gray.cols == size) {
        return gray;
    }
    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(size, size), 0, 0, cv::INTER_AREA);
    return resized;
}

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