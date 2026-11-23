#include "ui/config_panel.h"

#include "ui/p5_theme.h"
#include "model/defect_class.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace qf {
namespace ui {

namespace {

QSpinBox* makeSpin(int lo, int hi, int step, int value) {
    auto* spin = new QSpinBox;
    spin->setRange(lo, hi);
    spin->setSingleStep(step);
    spin->setValue(value);
    return spin;
}

QDoubleSpinBox* makeDSpin(double lo, double hi, int decimals, double step, double value) {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(lo, hi);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setValue(value);
    return spin;
}

QComboBox* makeCombo(const QStringList& items, const QString& current) {
    auto* combo = new QComboBox;
    combo->addItems(items);
    if (!current.isEmpty()) combo->setCurrentText(current);
    return combo;
}

QWidget* makePathRow(QLineEdit* edit, const QString& buttonText, QObject* receiver,
                     const char* slot) {
    auto* row = new QWidget;
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(edit, 1);
    auto* button = new QPushButton(buttonText);
    QObject::connect(button, SIGNAL(clicked()), receiver, slot);
    lay->addWidget(button);
    return row;
}

const model::AlgorithmParams& algorithmOrDefault(const model::PipelineParams& params,
                                                 model::DefectClass cls) {
    static const model::AlgorithmParams kFallback{};
    auto it = params.algorithms.find(cls);
    return (it != params.algorithms.end()) ? it->second : kFallback;
}

}  // namespace

ConfigPanel::ConfigPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* title = new QLabel(tr("参数配置（唯一编辑入口）"));
    title->setStyleSheet("color:#FFFFFF; font-weight:bold; font-size:14px;");
    root->addWidget(title);

    auto* tabs = new QTabWidget;
    auto* detectionTab = new QWidget;
    auto* calibrationTab = new QWidget;
    auto* commTab = new QWidget;
    auto* deviceTab = new QWidget;
    buildDetectionTab(detectionTab);
    buildCalibrationTab(calibrationTab);
    buildCommTab(commTab);
    buildDeviceTab(deviceTab);
    tabs->addTab(detectionTab, tr("检测"));
    tabs->addTab(calibrationTab, tr("标定"));
    tabs->addTab(commTab, tr("通信"));
    tabs->addTab(deviceTab, tr("设备"));
    root->addWidget(tabs, 1);

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(new QLabel(tr("配置文件")));
    configPathEdit_ = new QLineEdit;
    configPathEdit_->setText("config.json");
    pathRow->addWidget(configPathEdit_, 1);
    root->addLayout(pathRow);

    auto* actionRow = new QHBoxLayout;
    auto* saveBtn = new QPushButton(tr("保存并应用"));
    saveBtn->setStyleSheet(QString("background:%1; color:#FFFFFF; font-weight:bold;")
                               .arg(p5::kRed));
    auto* reloadBtn = new QPushButton(tr("重新载入"));
    connect(saveBtn, &QPushButton::clicked, this, &ConfigPanel::onSave);
    connect(reloadBtn, &QPushButton::clicked, this, &ConfigPanel::onReload);
    actionRow->addWidget(saveBtn);
    actionRow->addWidget(reloadBtn);
    statusLabel_ = new QLabel;
    statusLabel_->setStyleSheet(QString("color:%1;").arg(p5::kTextDim));
    actionRow->addWidget(statusLabel_, 1);
    root->addLayout(actionRow);
}

void ConfigPanel::buildDetectionTab(QWidget* tab) {
    auto* outer = new QVBoxLayout(tab);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget;
    auto* root = new QVBoxLayout(content);
    root->setSpacing(8);

    auto* preBox = new QGroupBox(tr("图像预处理"));
    auto* preForm = new QFormLayout(preBox);
    denoiseEnable_ = new QCheckBox(tr("启用"));
    denoiseMethod_ = makeCombo({"median", "gaussian", "bilateral"}, "median");
    denoiseKernel_ = makeSpin(1, 31, 2, 3);
    preForm->addRow(tr("降噪"), denoiseEnable_);
    preForm->addRow(tr("降噪方法"), denoiseMethod_);
    preForm->addRow(tr("降噪核大小"), denoiseKernel_);

    contrastEnable_ = new QCheckBox(tr("启用"));
    contrastMethod_ = makeCombo({"clahe", "equalize"}, "clahe");
    contrastClip_ = makeDSpin(0.1, 40.0, 2, 0.1, 2.0);
    preForm->addRow(tr("对比度"), contrastEnable_);
    preForm->addRow(tr("对比度方法"), contrastMethod_);
    preForm->addRow(tr("CLAHE clip"), contrastClip_);

    sharpenEnable_ = new QCheckBox(tr("启用"));
    sharpenMethod_ = makeCombo({"unsharp", "laplacian"}, "unsharp");
    sharpenSigma_ = makeDSpin(0.1, 10.0, 2, 0.1, 1.0);
    preForm->addRow(tr("锐化"), sharpenEnable_);
    preForm->addRow(tr("锐化方法"), sharpenMethod_);
    preForm->addRow(tr("锐化 sigma"), sharpenSigma_);
    root->addWidget(preBox);

    const QStringList metrics = {"edge_density", "area_ratio", "local_variance",
                                 "blob_count", "line_length", "texture_density"};

    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        const model::DefectClass cls = model::AllDefectClasses[i];
        const auto& ap = algorithmOrDefault(params_, cls);
        auto& widgets = algWidgets_[i];

        auto* box = new QGroupBox(QString("%1 %2")
                                      .arg(QString::fromUtf8(model::defectClassAbbr(cls)),
                                           QString::fromUtf8(model::defectClassChinese(cls))));
        auto* form = new QFormLayout(box);

        widgets.metric = makeCombo(metrics, QString::fromStdString(ap.metric));
        form->addRow(tr("度量"), widgets.metric);

        switch (cls) {
        case model::DefectClass::Crazing:
            widgets.cannyLow = makeSpin(0, 1000, 5, ap.cannyLow);
            widgets.cannyHigh = makeSpin(0, 1000, 5, ap.cannyHigh);
            form->addRow(tr("Canny 低阈值"), widgets.cannyLow);
            form->addRow(tr("Canny 高阈值"), widgets.cannyHigh);
            break;
        case model::DefectClass::Inclusion:
            widgets.blockSize = makeSpin(3, 255, 2, ap.blockSize);
            widgets.cValue = makeSpin(0, 50, 1, ap.cValue);
            form->addRow(tr("自适应块大小"), widgets.blockSize);
            form->addRow(tr("自适应 C 值"), widgets.cValue);
            break;
        case model::DefectClass::Patches:
            widgets.histDiff = makeDSpin(0.0, 1.0, 3, 0.01, ap.histDiff);
            form->addRow(tr("直方图差异阈值"), widgets.histDiff);
            break;
        case model::DefectClass::PittedSurface:
            widgets.tophatKernel = makeSpin(1, 99, 2, ap.tophatKernel);
            widgets.blobCountMin = makeSpin(0, 500, 1, ap.blobCountMin);
            form->addRow(tr("顶帽核大小"), widgets.tophatKernel);
            form->addRow(tr("最少斑点数量"), widgets.blobCountMin);
            break;
        case model::DefectClass::Scratches:
            widgets.cannyLow = makeSpin(0, 1000, 5, ap.cannyLow);
            widgets.cannyHigh = makeSpin(0, 1000, 5, ap.cannyHigh);
            widgets.aspectRatio = makeDSpin(1.0, 50.0, 2, 0.1, ap.aspectRatio);
            widgets.minLength = makeSpin(0, 5000, 5, ap.minLength);
            form->addRow(tr("Canny 低阈值"), widgets.cannyLow);
            form->addRow(tr("Canny 高阈值"), widgets.cannyHigh);
            form->addRow(tr("长宽比阈值"), widgets.aspectRatio);
            form->addRow(tr("最短长度"), widgets.minLength);
            break;
        case model::DefectClass::RolledInScale:
            widgets.blockSize = makeSpin(3, 255, 2, ap.blockSize);
            widgets.closeKernel = makeSpin(1, 99, 2, ap.closeKernel);
            form->addRow(tr("自适应块大小"), widgets.blockSize);
            form->addRow(tr("闭运算核大小"), widgets.closeKernel);
            break;
        }
        root->addWidget(box);
    }

    auto* decisionBox = new QGroupBox(tr("判定与调度"));
    auto* decisionForm = new QFormLayout(decisionBox);
    decisionMode_ = makeCombo({"max_confidence"}, "max_confidence");
    gateTimeout_ = makeSpin(100, 600000, 100, params_.gate.timeoutPerImageMs);
    gateThreads_ = makeSpin(1, 6, 1, params_.gate.parallelThreads);
    decisionForm->addRow(tr("判定模式"), decisionMode_);
    decisionForm->addRow(tr("单张超时 ms"), gateTimeout_);
    decisionForm->addRow(tr("并行线程数"), gateThreads_);
    root->addWidget(decisionBox);

    root->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void ConfigPanel::buildCalibrationTab(QWidget* tab) {
    auto* form = new QFormLayout(tab);
    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        auto* spin = makeDSpin(0.0, 100000.0, 4, 0.05, 0.0);
        thresholds_[i] = spin;
        form->addRow(tr("%1 置信度阈值 T_c")
                         .arg(QString::fromUtf8(model::defectClassChinese(model::AllDefectClasses[i]))),
                     spin);
    }
    minConfidence_ = makeDSpin(0.0, 1.0, 3, 0.01, 0.3);
    form->addRow(tr("最低置信度 min_confidence"), minConfidence_);
}

void ConfigPanel::buildCommTab(QWidget* tab) {
    auto* form = new QFormLayout(tab);
    commEnable_ = new QCheckBox(tr("启用通讯"));
    transport_ = makeCombo({"tcp", "rtu"}, "tcp");
    sendBreakdown_ = new QCheckBox(tr("传输六类得分段"));
    tcpPort_ = makeSpin(1, 65535, 1, 502);
    rtuPort_ = new QLineEdit("COM3");
    rtuBaud_ = makeSpin(1200, 921600, 1200, 115200);

    form->addRow(tr("通讯开关"), commEnable_);
    form->addRow(tr("传输通道"), transport_);
    form->addRow(tr("六类得分段"), sendBreakdown_);
    form->addRow(tr("TCP 端口"), tcpPort_);
    form->addRow(tr("RTU 串口"), rtuPort_);
    form->addRow(tr("RTU 波特率"), rtuBaud_);
}

void ConfigPanel::buildDeviceTab(QWidget* tab) {
    auto* form = new QFormLayout(tab);
    tcpUnitId_ = makeSpin(0, 255, 1, 1);
    rtuSlaveId_ = makeSpin(0, 247, 1, 1);
    dbEnable_ = new QCheckBox(tr("启用结果落库"));
    dbPath_ = new QLineEdit("results.db");
    inputDir_ = new QLineEdit;

    form->addRow(tr("TCP unit_id"), tcpUnitId_);
    form->addRow(tr("RTU 从站地址"), rtuSlaveId_);
    form->addRow(tr("数据库"), dbEnable_);
    form->addRow(tr("数据库路径"),
                 makePathRow(dbPath_, tr("浏览"), this, SLOT(onBrowseDatabase())));
    form->addRow(tr("输入目录"),
                 makePathRow(inputDir_, tr("浏览"), this, SLOT(onBrowseInputDir())));
}

void ConfigPanel::setParams(const model::PipelineParams& params) {
    params_ = params;

    denoiseEnable_->setChecked(params.preprocess.denoise.enable);
    denoiseMethod_->setCurrentText(QString::fromStdString(params.preprocess.denoise.method));
    denoiseKernel_->setValue(params.preprocess.denoise.kernel);

    contrastEnable_->setChecked(params.preprocess.contrast.enable);
    contrastMethod_->setCurrentText(QString::fromStdString(params.preprocess.contrast.method));
    contrastClip_->setValue(params.preprocess.contrast.clip);

    sharpenEnable_->setChecked(params.preprocess.sharpen.enable);
    sharpenMethod_->setCurrentText(QString::fromStdString(params.preprocess.sharpen.method));
    sharpenSigma_->setValue(params.preprocess.sharpen.sigma);

    decisionMode_->setCurrentText(QString::fromStdString(params.decision.mode));
    gateTimeout_->setValue(params.gate.timeoutPerImageMs);
    gateThreads_->setValue(params.gate.parallelThreads);

    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        thresholds_[i]->setValue(algorithmOrDefault(params, model::AllDefectClasses[i]).threshold);
    }
    minConfidence_->setValue(params.decision.minConfidence);

    const auto& comm = params.infrastructure.comm;
    commEnable_->setChecked(comm.enable);
    transport_->setCurrentText(QString::fromStdString(comm.transport));
    sendBreakdown_->setChecked(comm.sendClassBreakdown);
    tcpPort_->setValue(comm.tcp.port);
    rtuPort_->setText(QString::fromStdString(comm.rtu.port));
    rtuBaud_->setValue(comm.rtu.baud);

    tcpUnitId_->setValue(comm.tcp.unitId);
    rtuSlaveId_->setValue(comm.rtu.slaveId);
    dbEnable_->setChecked(params.infrastructure.database.enable);
    dbPath_->setText(QString::fromStdString(params.infrastructure.database.path));
    inputDir_->setText(QString::fromStdString(params.inputDir));

    loadAlgorithmWidgets();
}

void ConfigPanel::loadAlgorithmWidgets() {
    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        const auto& ap = algorithmOrDefault(params_, model::AllDefectClasses[i]);
        auto& widgets = algWidgets_[i];
        if (!widgets.metric) continue;
        widgets.metric->setCurrentText(QString::fromStdString(ap.metric));
        if (widgets.cannyLow) widgets.cannyLow->setValue(ap.cannyLow);
        if (widgets.cannyHigh) widgets.cannyHigh->setValue(ap.cannyHigh);
        if (widgets.blockSize) widgets.blockSize->setValue(ap.blockSize);
        if (widgets.cValue) widgets.cValue->setValue(ap.cValue);
        if (widgets.histDiff) widgets.histDiff->setValue(ap.histDiff);
        if (widgets.tophatKernel) widgets.tophatKernel->setValue(ap.tophatKernel);
        if (widgets.blobCountMin) widgets.blobCountMin->setValue(ap.blobCountMin);
        if (widgets.closeKernel) widgets.closeKernel->setValue(ap.closeKernel);
        if (widgets.aspectRatio) widgets.aspectRatio->setValue(ap.aspectRatio);
        if (widgets.minLength) widgets.minLength->setValue(ap.minLength);
    }
}

void ConfigPanel::setConfigPath(const QString& path) {
    path_ = path;
    configPathEdit_->setText(path);
}

QString ConfigPanel::configPath() const {
    return configPathEdit_->text().trimmed().isEmpty() ? path_ : configPathEdit_->text().trimmed();
}

void ConfigPanel::setInputDir(const QString& dir) {
    inputDir_->setText(dir);
}

model::PipelineParams ConfigPanel::gather() const {
    model::PipelineParams p = params_;

    p.preprocess.denoise.enable = denoiseEnable_->isChecked();
    p.preprocess.denoise.method = denoiseMethod_->currentText().toStdString();
    p.preprocess.denoise.kernel = denoiseKernel_->value();

    p.preprocess.contrast.enable = contrastEnable_->isChecked();
    p.preprocess.contrast.method = contrastMethod_->currentText().toStdString();
    p.preprocess.contrast.clip = contrastClip_->value();

    p.preprocess.sharpen.enable = sharpenEnable_->isChecked();
    p.preprocess.sharpen.method = sharpenMethod_->currentText().toStdString();
    p.preprocess.sharpen.sigma = sharpenSigma_->value();

    p.decision.mode = decisionMode_->currentText().toStdString();
    p.decision.minConfidence = minConfidence_->value();
    p.gate.timeoutPerImageMs = gateTimeout_->value();
    p.gate.parallelThreads = gateThreads_->value();

    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        const auto cls = model::AllDefectClasses[i];
        const auto& widgets = algWidgets_[i];
        auto& ap = p.algorithms[cls];  // 若基准缺失则按默认插入

        ap.threshold = thresholds_[i]->value();
        if (widgets.metric) ap.metric = widgets.metric->currentText().toStdString();
        if (widgets.cannyLow) ap.cannyLow = widgets.cannyLow->value();
        if (widgets.cannyHigh) ap.cannyHigh = widgets.cannyHigh->value();
        if (widgets.blockSize) ap.blockSize = widgets.blockSize->value();
        if (widgets.cValue) ap.cValue = widgets.cValue->value();
        if (widgets.histDiff) ap.histDiff = widgets.histDiff->value();
        if (widgets.tophatKernel) ap.tophatKernel = widgets.tophatKernel->value();
        if (widgets.blobCountMin) ap.blobCountMin = widgets.blobCountMin->value();
        if (widgets.closeKernel) ap.closeKernel = widgets.closeKernel->value();
        if (widgets.aspectRatio) ap.aspectRatio = widgets.aspectRatio->value();
        if (widgets.minLength) ap.minLength = widgets.minLength->value();
    }

    auto& comm = p.infrastructure.comm;
    comm.enable = commEnable_->isChecked();
    comm.transport = transport_->currentText().toStdString();
    comm.sendClassBreakdown = sendBreakdown_->isChecked();
    comm.tcp.port = tcpPort_->value();
    comm.rtu.port = rtuPort_->text().trimmed().toStdString();
    comm.rtu.baud = rtuBaud_->value();

    comm.tcp.unitId = tcpUnitId_->value();
    comm.rtu.slaveId = rtuSlaveId_->value();
    p.infrastructure.database.enable = dbEnable_->isChecked();
    p.infrastructure.database.path = dbPath_->text().trimmed().toStdString();
    p.inputDir = inputDir_->text().trimmed().toStdString();

    return p;
}

void ConfigPanel::onSave() {
    const QString path = configPath();
    path_ = path;
    statusLabel_->setStyleSheet(QString("color:%1;").arg(p5::kTextDim));
    statusLabel_->setText(tr("正在保存…"));
    emit saveRequested(gather(), path);
}

void ConfigPanel::onReload() {
    statusLabel_->setStyleSheet(QString("color:%1;").arg(p5::kTextDim));
    statusLabel_->setText(tr("正在重新载入…"));
    emit reloadRequested(configPath());
}

void ConfigPanel::onBrowseInputDir() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择输入目录"),
                                                          inputDir_->text());
    if (!dir.isEmpty()) inputDir_->setText(dir);
}

void ConfigPanel::onBrowseDatabase() {
    const QString file = QFileDialog::getSaveFileName(
        this, tr("选择/新建数据库文件"), dbPath_->text(), tr("SQLite 数据库 (*.db)"));
    if (!file.isEmpty()) dbPath_->setText(file);
}

}  // namespace ui
}  // namespace qf
