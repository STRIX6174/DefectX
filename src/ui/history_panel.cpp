#include "ui/history_panel.h"

#include "ui/p5_theme.h"
#include "model/defect_class.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qf {
namespace ui {

namespace {

QString classText(const std::optional<model::DefectClass>& cls) {
    if (!cls) return QStringLiteral("—");
    return QString::fromUtf8(model::defectClassAbbr(*cls)) + " · " +
           QString::fromUtf8(model::defectClassChinese(*cls));
}

QTableWidgetItem* makeItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

}  // namespace

HistoryPanel::HistoryPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* title = new QLabel(tr("历史运行查询"));
    title->setStyleSheet("color:#FFFFFF; font-weight:bold; font-size:14px;");
    root->addWidget(title);

    pathLabel_ = new QLabel(tr("数据库：未配置"));
    pathLabel_->setStyleSheet(QString("color:%1;").arg(p5::kTextDim));
    pathLabel_->setWordWrap(true);
    root->addWidget(pathLabel_);

    auto* actionRow = new QHBoxLayout;
    auto* refreshBtn = new QPushButton(tr("刷新"));
    connect(refreshBtn, &QPushButton::clicked, this, &HistoryPanel::onRefreshClicked);
    actionRow->addWidget(refreshBtn);
    statusLabel_ = new QLabel(tr("尚未查询"));
    statusLabel_->setStyleSheet(QString("color:%1;").arg(p5::kTextDim));
    actionRow->addWidget(statusLabel_, 1);
    root->addLayout(actionRow);

    runsTable_ = new QTableWidget(0, 9);
    runsTable_->setHorizontalHeaderLabels(
        {tr("运行"), tr("开始时间"), tr("模式"), tr("总数"), tr("成功"), tr("失败"),
         tr("平均 ms"), tr("P95 ms"), tr("评估指标")});
    runsTable_->verticalHeader()->setVisible(false);
    runsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    runsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    runsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    runsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    runsTable_->horizontalHeader()->setStretchLastSection(true);
    connect(runsTable_, &QTableWidget::itemSelectionChanged,
            this, &HistoryPanel::onRunSelectionChanged);

    detailTable_ = new QTableWidget(0, 6);
    detailTable_->setHorizontalHeaderLabels(
        {tr("文件"), tr("真值"), tr("预测"), tr("置信度"), tr("耗时 ms"), tr("状态")});
    detailTable_->verticalHeader()->setVisible(false);
    detailTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    detailTable_->horizontalHeader()->setStretchLastSection(true);

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(runsTable_);
    splitter->addWidget(detailTable_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);
}

void HistoryPanel::setDatabasePath(const QString& path) {
    if (dbPath_ == path) return;
    dbPath_ = path;
    store_.close();
    opened_ = false;
    pathLabel_->setText(tr("数据库：%1").arg(path.isEmpty() ? tr("未配置") : path));
    refresh();
}

void HistoryPanel::onRefreshClicked() {
    refresh();
}

void HistoryPanel::refresh() {
    if (dbPath_.isEmpty() || dbPath_ == "未配置") {
        statusLabel_->setText(tr("未配置数据库路径（在配置面板设备页设置）"));
        return;
    }
    if (!opened_) {
        if (!store_.open(dbPath_.toStdString())) {
            statusLabel_->setText(tr("打开失败：%1")
                                      .arg(QString::fromStdString(store_.lastError())));
            return;
        }
        opened_ = true;
    }

    auto runs = store_.listRuns(200);
    fillRuns(runs);
    statusLabel_->setText(tr("共 %1 次运行（最近在前）").arg(runs.size()));
}

void HistoryPanel::fillRuns(const std::vector<infrastructure::RunRecord>& runs) {
    runsTable_->setRowCount(0);
    runsTable_->setRowCount(static_cast<int>(runs.size()));

    for (int row = 0; row < static_cast<int>(runs.size()); ++row) {
        const auto& rec = runs[row];
        auto* idItem = makeItem(QString::number(rec.runId));
        idItem->setData(Qt::UserRole, static_cast<qlonglong>(rec.runId));
        runsTable_->setItem(row, 0, idItem);
        runsTable_->setItem(row, 1, makeItem(QString::fromStdString(rec.startedAt)));
        runsTable_->setItem(row, 2, makeItem(QString::fromStdString(rec.mode)));
        runsTable_->setItem(row, 3, makeItem(QString::number(rec.totalImages)));
        runsTable_->setItem(row, 4, makeItem(QString::number(rec.successCount)));
        runsTable_->setItem(row, 5, makeItem(QString::number(rec.failedCount)));
        runsTable_->setItem(row, 6, makeItem(QString::number(rec.avgCycleMs, 'f', 1)));
        runsTable_->setItem(row, 7, makeItem(QString::number(rec.p95CycleMs, 'f', 1)));

        QString metrics = QStringLiteral("—");
        if (rec.hasMetrics) {
            metrics = tr("Acc %1 · F1 %2（标注 %3 / 未知 %4）")
                          .arg(rec.accuracy, 0, 'f', 3)
                          .arg(rec.macroF1, 0, 'f', 3)
                          .arg(rec.labeledCount)
                          .arg(rec.unknownCount);
        }
        runsTable_->setItem(row, 8, makeItem(metrics));
    }

    if (!runs.empty()) {
        runsTable_->selectRow(0);
    } else {
        detailTable_->setRowCount(0);
    }
}

void HistoryPanel::onRunSelectionChanged() {
    const auto selected = runsTable_->selectedItems();
    if (selected.isEmpty()) {
        detailTable_->setRowCount(0);
        return;
    }
    const int row = selected.first()->row();
    QTableWidgetItem* idItem = runsTable_->item(row, 0);
    if (!idItem) return;
    fillMeasurements(static_cast<int64_t>(idItem->data(Qt::UserRole).toLongLong()));
}

void HistoryPanel::fillMeasurements(int64_t runId) {
    const auto rows = store_.listMeasurements(runId, 2000);
    detailTable_->setRowCount(static_cast<int>(rows.size()));

    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& rec = rows[i];
        auto* fileItem = makeItem(QString::fromStdString(rec.fileName));
        fileItem->setToolTip(QString::fromStdString(rec.relPath));
        detailTable_->setItem(i, 0, fileItem);
        detailTable_->setItem(i, 1, makeItem(classText(rec.groundTruth)));
        detailTable_->setItem(i, 2, makeItem(classText(rec.predicted)));
        detailTable_->setItem(i, 3, makeItem(QString::number(rec.confidence, 'f', 3)));
        detailTable_->setItem(i, 4, makeItem(QString::number(rec.cycleTimeMs, 'f', 1)));
        detailTable_->setItem(i, 5, makeItem(QString::fromStdString(rec.status)));
    }
}

}  // namespace ui
}  // namespace qf
