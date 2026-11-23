#pragma once

#include <QWidget>

#include <vector>

#include "model/pipeline_params.h"

class QLabel;
class QPlainTextEdit;
class QTimer;

namespace qf {
namespace application { class PipelineRunner; }

namespace ui {

// 职责2：通讯状态面板（只读展示 + 诊断）。
// 铁律：只读摘要，参数修改跳转 ConfigPanel；状态用 QTimer 轮询（待确认项 B.5）。
class CommPanel : public QWidget {
    Q_OBJECT
public:
    explicit CommPanel(QWidget* parent = nullptr);
    ~CommPanel() override;

    void bindPipeline(application::PipelineRunner* runner);
    void setParams(const model::PipelineParams& params);

signals:
    void configEditRequested();

private slots:
    void pollStatus();
    void onTestListen();
    void onToggleRegisters();
    void onClearLog();

private:
    void appendLogLines(const std::vector<std::string>& lines);
    void fillRegisterView();
    void applyStatusLight(bool open, bool hasPeer);

    application::PipelineRunner* pipeline_ = nullptr;

    QLabel* light_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* endpointLabel_ = nullptr;
    QLabel* requestLabel_ = nullptr;
    QLabel* pollLabel_ = nullptr;
    QLabel* batchLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QLabel* configSummary_ = nullptr;
    QLabel* diagLabel_ = nullptr;

    QPlainTextEdit* registerView_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QTimer* timer_ = nullptr;
};

}  // namespace ui
}  // namespace qf
