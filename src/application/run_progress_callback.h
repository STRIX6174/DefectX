#pragma once

#include "model/image_entry.h"
#include "model/classification_result.h"
#include "model/run_report.h"
#include "model/error_code.h"

namespace qf {
namespace application {

// 纯 C++ 回调接口，供业务层在工作线程调用；
// UI 侧由 RunBridge 桥接为 Qt 信号（QueuedConnection）投递主线程。
class RunProgressCallback {
public:
    virtual ~RunProgressCallback() = default;

    virtual void onImageDone(int done, int total,
                             const model::ImageEntry& img,
                             const model::ClassificationResult& result) = 0;
    virtual void onError(const model::ImageEntry& img, model::ErrorCode ec) = 0;
    virtual void onFinished(const model::RunReport& report) = 0;
};

}  // namespace application
}  // namespace qf