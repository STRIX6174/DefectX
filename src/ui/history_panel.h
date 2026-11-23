#pragma once

#include <QWidget>

#include "infrastructure/result_store.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace qf {
namespace ui {

// 职责3：历史查询面板（拉取式，主动查 SQLite）。
// 数据源：runs / measurements（run_metrics 通过 runs 左连接一并展示）。
class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(QWidget* parent = nullptr);

    void setDatabasePath(const QString& path);
    QString databasePath() const { return dbPath_; }

public slots:
    void refresh();

private slots:
    void onRunSelectionChanged();
    void onRefreshClicked();

private:
    void fillRuns(const std::vector<infrastructure::RunRecord>& runs);
    void fillMeasurements(int64_t runId);

    infrastructure::ResultStore store_;
    QString dbPath_;
    bool opened_ = false;

    QLabel* pathLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTableWidget* runsTable_ = nullptr;
    QTableWidget* detailTable_ = nullptr;
};

}  // namespace ui
}  // namespace qf
