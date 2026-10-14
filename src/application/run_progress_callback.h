#pragma once

#include "model/image_entry.h"
#include "model/inspection_result.h"
#include "model/run_report.h"
#include "model/error_code.h"

namespace qf {
namespace application {

class RunProgressCallback {
public:
    virtual ~RunProgressCallback() = default;

    virtual void onImageDone(int done, int total,
                             const model::ImageEntry& img,
                             const model::InspectionResult& result) = 0;
    virtual void onError(const model::ImageEntry& img, model::ErrorCode ec) = 0;
    virtual void onFinished(const model::RunReport& report) = 0;
};

}
}