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

class CrazingOperator : public DefectOperator {
public:
    explicit CrazingOperator(const model::DefectParam& p) : params_(p) {}

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

class InclusionOperator : public DefectOperator {
public:
    explicit InclusionOperator(const model::DefectParam& p) : params_(p) {}

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

class PatchesOperator : public DefectOperator {
public:
    explicit PatchesOperator(const model::DefectParam& p) : params_(p) {}

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

class PittedSurfaceOperator : public DefectOperator {
public:
    explicit PittedSurfaceOperator(const model::DefectParam& p) : params_(p) {}

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

class ScratchesOperator : public DefectOperator {
public:
    explicit ScratchesOperator(const model::DefectParam& p) : params_(p) {}

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

class RolledInScaleOperator : public DefectOperator {
public:
    explicit RolledInScaleOperator(const model::DefectParam& p) : params_(p) {}

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