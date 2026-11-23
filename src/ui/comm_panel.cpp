#include "ui/comm_panel.h"

#include "ui/p5_theme.h"
#include "application/pipeline_runner.h"
#include "infrastructure/comm/modbus_common.h"
#include "model/defect_class.h"

#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace qf {
namespace ui {

namespace {

const char* registerName(int index) {
    using namespace infrastructure::registers;
    switch (index) {
    case kBatchSequence:  return "批次序号";
    case kRunStatus:      return "运行状态(0正常/1部分)";
    case kTotalImages:    return "总图片数";
    case kSuccessCount:   return "成功数";
    case kFailedCount:    return "失败数";
    case kAccuracy:       return "Accuracy ×1000";
    case kMacroPrecision: return "Macro Precision ×1000";
    case kMacroRecall:    return "Macro Recall ×1000";
    case kMacroF1:        return "Macro F1 ×1000";
    case kAvgCycleTime:   return "平均耗时 ms";
    case kP95CycleTime:   return "P95 耗时 ms";
    default:              return "六类得分";
    }
}

QLabel* makeDimLabel(const QString& text) {
    auto* label = new QLabel(text);
    label->setStyleSheet(QString("color:%1;").arg(p5::kTextDim));
    return label;
}

}  // namespace

CommPanel::CommPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* title = new QLabel(tr("PLC 通讯状态（从站）"));
    title->setStyleSheet("color:#FFFFFF; font-weight:bold; font-size:14px;");
    root->addWidget(title);

    // 连接指示灯 + 状态文本
    auto* stateRow = new QHBoxLayout;
    light_ = new QLabel;
    light_->setFixedSize(12, 12);
    stateRow->addWidget(light_);
    stateLabel_ = new QLabel(tr("未启动"));
    stateRow->addWidget(stateLabel_, 1);
    root->addLayout(stateRow);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    int row = 0;
    const auto addRow = [&](const QString& key, QLabel*& value) {
        grid->addWidget(makeDimLabel(key), row, 0);
        value = new QLabel("—");
        grid->addWidget(value, row, 1);
        ++row;
    };
    addRow(tr("监听端点"), endpointLabel_);
    addRow(tr("主站请求"), requestLabel_);
    addRow(tr("最近轮询"), pollLabel_);
    addRow(tr("当前批次"), batchLabel_);
    addRow(tr("最后错误"), errorLabel_);
    errorLabel_->setStyleSheet(QString("color:%1;").arg(p5::kFail));
    root->addLayout(grid);

    // 只读配置摘要 + 跳转配置
    configSummary_ = makeDimLabel(tr("通信配置：未加载"));
    configSummary_->setWordWrap(true);
    root->addWidget(configSummary_);

    auto* configRow = new QHBoxLayout;
    configRow->addStretch(1);
    auto* editBtn = new QPushButton(tr("修改配置"));
    editBtn->setToolTip(tr("参数编辑统一在配置面板，本面板只读"));
    connect(editBtn, &QPushButton::clicked, this, &CommPanel::configEditRequested);
    configRow->addWidget(editBtn);
    root->addLayout(configRow);

    // 诊断操作
    auto* diagRow = new QHBoxLayout;
    auto* testBtn = new QPushButton(tr("测试监听"));
    auto* regBtn = new QPushButton(tr("查看寄存器镜像"));
    auto* clearBtn = new QPushButton(tr("清空日志"));
    diagRow->addWidget(testBtn);
    diagRow->addWidget(regBtn);
    diagRow->addWidget(clearBtn);
    diagRow->addStretch(1);
    root->addLayout(diagRow);

    diagLabel_ = makeDimLabel(QString());
    diagLabel_->setWordWrap(true);
    root->addWidget(diagLabel_);

    registerView_ = new QPlainTextEdit;
    registerView_->setReadOnly(true);
    registerView_->setMaximumHeight(150);
    registerView_->setVisible(false);
    registerView_->setStyleSheet("font-family: Consolas, monospace;");
    root->addWidget(registerView_);

    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(500);
    logView_->setStyleSheet("font-family: Consolas, monospace;");
    logView_->setPlaceholderText(tr("通信日志（主站请求帧 / 本机响应帧 / 异常）"));
    root->addWidget(logView_, 1);

    connect(testBtn, &QPushButton::clicked, this, &CommPanel::onTestListen);
    connect(regBtn, &QPushButton::clicked, this, &CommPanel::onToggleRegisters);
    connect(clearBtn, &QPushButton::clicked, this, &CommPanel::onClearLog);

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &CommPanel::pollStatus);
    timer_->start();

    applyStatusLight(false, false);
}

CommPanel::~CommPanel() {
    timer_->stop();
}

void CommPanel::bindPipeline(application::PipelineRunner* runner) {
    pipeline_ = runner;
    pollStatus();
}

void CommPanel::setParams(const model::PipelineParams& params) {
    const auto& comm = params.infrastructure.comm;
    if (!comm.enable) {
        configSummary_->setText(tr("通信配置：未启用（enable=false）"));
        return;
    }
    QString text = tr("通信配置：%1 · ").arg(QString::fromStdString(comm.transport));
    if (comm.transport == "rtu") {
        text += tr("端口 %1 · 波特率 %2 · 从站 %3")
                    .arg(QString::fromStdString(comm.rtu.port))
                    .arg(comm.rtu.baud)
                    .arg(comm.rtu.slaveId);
    } else {
        text += tr("端口 %1 · unit_id %2").arg(comm.tcp.port).arg(comm.tcp.unitId);
    }
    text += comm.sendClassBreakdown ? tr(" · 含六类得分段") : tr(" · 不含六类得分段");
    configSummary_->setText(text);
}

void CommPanel::pollStatus() {
    if (!pipeline_) return;

    appendLogLines(pipeline_->takeCommLog());

    const auto status = pipeline_->commStatus();
    applyStatusLight(status.open, status.hasPeer);

    if (status.open) {
        stateLabel_->setText(status.hasPeer ? tr("主站已连接，轮询中")
                                            : tr("从站监听中，等待主站"));
    } else if (!status.lastError.empty()) {
        stateLabel_->setText(tr("未监听（%1）").arg(QString::fromStdString(status.lastError)));
    } else {
        stateLabel_->setText(tr("未启动"));
    }

    endpointLabel_->setText(status.endpoint.empty() ? "—"
                                                    : QString::fromStdString(status.endpoint));
    requestLabel_->setText(QString::number(status.requestCount));
    pollLabel_->setText(status.lastPollAt.empty() ? "—"
                                                  : QString::fromStdString(status.lastPollAt));
    batchLabel_->setText(status.publishCount == 0
                             ? "—"
                             : tr("#%1%2")
                                   .arg(status.batchSequence)
                                   .arg(status.isPartial ? tr("（部分/中止）") : QString()));
    errorLabel_->setText(status.lastError.empty() ? "—"
                                                  : QString::fromStdString(status.lastError));

    if (registerView_->isVisible()) fillRegisterView();
}

void CommPanel::onTestListen() {
    if (!pipeline_) {
        diagLabel_->setText(tr("诊断：流水线未就绪"));
        return;
    }
    std::string error;
    if (pipeline_->testCommChannel(error)) {
        diagLabel_->setStyleSheet(QString("color:%1;").arg(p5::kSuccess));
        diagLabel_->setText(tr("诊断：从站通道已就绪，等待主站连接"));
    } else {
        diagLabel_->setStyleSheet(QString("color:%1;").arg(p5::kFail));
        diagLabel_->setText(tr("诊断：监听失败 — %1").arg(QString::fromStdString(error)));
    }
    pollStatus();
}

void CommPanel::onToggleRegisters() {
    const bool visible = !registerView_->isVisible();
    registerView_->setVisible(visible);
    if (visible) fillRegisterView();
}

void CommPanel::onClearLog() {
    if (pipeline_) pipeline_->takeCommLog();  // 丢弃积压日志
    logView_->clear();
}

void CommPanel::appendLogLines(const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
        logView_->appendPlainText(QString::fromStdString(line));
    }
}

void CommPanel::fillRegisterView() {
    if (!pipeline_) return;
    const auto image = pipeline_->commRegisterImage();

    QString text = tr("保持寄存器镜像（0 起地址，共 %1 个）\n")
                       .arg(infrastructure::registers::kCount);
    for (int i = 0; i < static_cast<int>(image.size()); ++i) {
        QString name;
        if (i >= infrastructure::registers::kClassScoreBase) {
            int classIdx = i - infrastructure::registers::kClassScoreBase;
            name = tr("六类得分 %1").arg(QString::fromUtf8(
                model::defectClassAbbr(model::AllDefectClasses[classIdx])));
        } else {
            name = QString::fromUtf8(registerName(i));
        }
        text += QString("%1  %2 = %3\n")
                    .arg(i, 2)
                    .arg(name.leftJustified(28, ' '))
                    .arg(image[i]);
    }
    registerView_->setPlainText(text);
}

void CommPanel::applyStatusLight(bool open, bool hasPeer) {
    const char* color = p5::kGray;
    if (open) color = hasPeer ? p5::kSuccess : p5::kRed;
    light_->setStyleSheet(
        QString("background:%1; border-radius:6px;").arg(color));
}

}  // namespace ui
}  // namespace qf
