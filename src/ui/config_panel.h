#pragma once

#include <QWidget>

#include <array>

#include "model/pipeline_params.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace qf {
namespace ui {

// 职责4：参数配置面板（检测 / 标定 / 通信 / 设备 四个 tab）。
// 铁律：参数编辑唯一入口；只编辑内存 PipelineParams，保存/加载交给 application。
class ConfigPanel : public QWidget {
    Q_OBJECT
public:
    explicit ConfigPanel(QWidget* parent = nullptr);

    void setParams(const model::PipelineParams& params);
    void setConfigPath(const QString& path);
    QString configPath() const;
    void setInputDir(const QString& dir);

signals:
    void saveRequested(const model::PipelineParams& params, const QString& path);
    void reloadRequested(const QString& path);

private slots:
    void onSave();
    void onReload();
    void onBrowseInputDir();
    void onBrowseDatabase();

private:
    // 每类只创建该类实际用到的算子参数控件（threshold 归「标定」页）。
    struct AlgorithmWidgets {
        QComboBox* metric = nullptr;
        QSpinBox* cannyLow = nullptr;
        QSpinBox* cannyHigh = nullptr;
        QSpinBox* blockSize = nullptr;
        QSpinBox* cValue = nullptr;
        QDoubleSpinBox* histDiff = nullptr;
        QSpinBox* tophatKernel = nullptr;
        QSpinBox* blobCountMin = nullptr;
        QSpinBox* closeKernel = nullptr;
        QDoubleSpinBox* aspectRatio = nullptr;
        QSpinBox* minLength = nullptr;
    };

    void buildDetectionTab(QWidget* tab);
    void buildCalibrationTab(QWidget* tab);
    void buildCommTab(QWidget* tab);
    void buildDeviceTab(QWidget* tab);
    void loadAlgorithmWidgets();
    model::PipelineParams gather() const;

    model::PipelineParams params_;
    QString path_;

    // 检测参数
    QCheckBox* denoiseEnable_ = nullptr;
    QComboBox* denoiseMethod_ = nullptr;
    QSpinBox* denoiseKernel_ = nullptr;
    QCheckBox* contrastEnable_ = nullptr;
    QComboBox* contrastMethod_ = nullptr;
    QDoubleSpinBox* contrastClip_ = nullptr;
    QCheckBox* sharpenEnable_ = nullptr;
    QComboBox* sharpenMethod_ = nullptr;
    QDoubleSpinBox* sharpenSigma_ = nullptr;
    QComboBox* decisionMode_ = nullptr;
    QSpinBox* gateTimeout_ = nullptr;
    QSpinBox* gateThreads_ = nullptr;
    std::array<AlgorithmWidgets, 6> algWidgets_{};

    // 标定参数
    std::array<QDoubleSpinBox*, 6> thresholds_{};
    QDoubleSpinBox* minConfidence_ = nullptr;

    // 通信参数
    QCheckBox* commEnable_ = nullptr;
    QComboBox* transport_ = nullptr;
    QCheckBox* sendBreakdown_ = nullptr;
    QSpinBox* tcpPort_ = nullptr;
    QLineEdit* rtuPort_ = nullptr;
    QSpinBox* rtuBaud_ = nullptr;

    // 设备参数
    QSpinBox* tcpUnitId_ = nullptr;
    QSpinBox* rtuSlaveId_ = nullptr;
    QCheckBox* dbEnable_ = nullptr;
    QLineEdit* dbPath_ = nullptr;
    QLineEdit* inputDir_ = nullptr;

    QLineEdit* configPathEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

}  // namespace ui
}  // namespace qf
