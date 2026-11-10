#include "ui/main_window.h"
#include "ui/p5_widgets.h"
#include "ui/p5_theme.h"
#include "application/config_manager.h"
#include "application/log_manager.h"
#include "model/defect_class.h"

#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStatusBar>
#include <QPainter>
#include <QFileInfo>
#include <QThread>
#include <QPropertyAnimation>
#include <QEasingCurve>

#include <filesystem>
#include <algorithm>

#include <opencv2/imgcodecs.hpp>

namespace qf {
namespace ui {

// ============================================================
// ResultListModel
// ============================================================
namespace {
constexpr int RolePredAbbr = Qt::UserRole;       // 预测类别缩写，或 "?"（无法判定）
constexpr int RoleStatus   = Qt::UserRole + 1;   // ImageStatus
constexpr int RoleConf     = Qt::UserRole + 2;   // 最高置信度 0~1
constexpr int RolePath     = Qt::UserRole + 3;   // 完整路径
constexpr int RolePredIdx  = Qt::UserRole + 4;   // 预测类别索引，-1 表示无法判定
}

ResultListModel::ResultListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int ResultListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(items_.size());
}

QVariant ResultListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(items_.size()))
        return {};

    const auto& item = items_[index.row()];

    switch (role) {
    case Qt::DisplayRole:
        return QFileInfo(QString::fromStdString(item.entry.filePath)).fileName();
    case RolePredAbbr: {
        if (item.entry.status == model::ImageStatus::LoadFailed) return QString("ERR");
        if (item.result.predicted.has_value()) {
            return QString::fromUtf8(model::defectClassAbbr(*item.result.predicted));
        }
        return QString("?");
    }
    case RoleStatus:
        return static_cast<int>(item.entry.status);
    case RoleConf:
        return item.result.confidence;
    case RolePath:
        return QString::fromStdString(item.entry.filePath);
    case RolePredIdx:
        return item.result.predicted.has_value()
                   ? model::defectClassIndex(*item.result.predicted)
                   : -1;
    case Qt::DecorationRole:
        return item.thumbnail;
    }
    return {};
}

Qt::ItemFlags ResultListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void ResultListModel::addResult(const model::ImageEntry& entry,
                                const model::ClassificationResult& result) {
    int row = static_cast<int>(items_.size());
    beginInsertRows(QModelIndex{}, row, row);

    ResultItem item;
    item.entry  = entry;
    item.result = result;

    QString path = QString::fromStdString(entry.filePath);
    if (!QPixmapCache::find(path, &item.thumbnail)) {
        cv::Mat mat = cv::imread(entry.filePath, cv::IMREAD_COLOR);
        if (!mat.empty()) {
            QImage qimg = RunBridge::toDisplayImage(mat);
            item.thumbnail = QPixmap::fromImage(
                qimg.scaled(120, 120, Qt::KeepAspectRatio, Qt::FastTransformation));
            QPixmapCache::insert(path, item.thumbnail);
        }
    }

    items_.push_back(std::move(item));
    endInsertRows();
}

void ResultListModel::clear() {
    beginResetModel();
    items_.clear();
    endResetModel();
    QPixmapCache::clear();
}

const ResultItem* ResultListModel::itemAt(int row) const {
    if (row < 0 || row >= static_cast<int>(items_.size())) return nullptr;
    return &items_[row];
}

// ============================================================
// ResultFilterProxy
// ============================================================
ResultFilterProxy::ResultFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent) {}

void ResultFilterProxy::setSelectedClass(int classIndex) {
    selectedClass_ = classIndex;
    invalidateFilter();
}

bool ResultFilterProxy::filterAcceptsRow(int sourceRow,
                                         const QModelIndex& sourceParent) const {
    Q_UNUSED(sourceParent);
    auto* model = qobject_cast<ResultListModel*>(sourceModel());
    if (!model) return true;

    auto* item = model->itemAt(sourceRow);
    if (!item) return false;

    int predIdx = item->result.predicted.has_value()
                      ? model::defectClassIndex(*item->result.predicted)
                      : -1;

    // 六类图例单选："只看某类"（失败图无类别，选中类别时被过滤）
    if (selectedClass_ >= 0 && predIdx != selectedClass_) {
        return false;
    }

    return true;
}

// ============================================================
// ThumbnailDelegate（P5 斜切卡片 + 类别色角标）
// ============================================================
ThumbnailDelegate::ThumbnailDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void ThumbnailDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF card = QRectF(option.rect).adjusted(kGap, kGap, -kGap, -kGap);

    int predIdx = index.data(RolePredIdx).toInt();
    int status = index.data(RoleStatus).toInt();
    bool failed = (static_cast<model::ImageStatus>(status) == model::ImageStatus::LoadFailed);
    QColor accent = failed ? QColor(p5::kGray)
                   : (predIdx >= 0 ? defectClassColor(predIdx) : QColor(p5::kGray));

    bool hovered = option.state & QStyle::State_MouseOver;
    bool selected = option.state & QStyle::State_Selected;

    // 卡片底 + 描边（悬浮 = 类别色，选中 = P5 血红）
    QPainterPath cardPath = chamferRect(card, 8.0);
    painter->fillPath(cardPath, QColor(p5::kPanel));
    QPen borderPen(selected ? QColor(p5::kRed)
                   : hovered ? accent : QColor(p5::kBorder),
                   selected ? 2 : 1);
    painter->setPen(borderPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(cardPath);

    // 缩略图区
    QRectF imgArea = card.adjusted(5, 5, -5, -5);
    imgArea.setBottom(card.bottom() - 18);

    QPixmap thumb = index.data(Qt::DecorationRole).value<QPixmap>();
    if (!thumb.isNull()) {
        QPixmap scaled = thumb.scaled(imgArea.size().toSize(),
                                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = static_cast<int>(imgArea.x() + (imgArea.width() - scaled.width()) / 2);
        int y = static_cast<int>(imgArea.y() + (imgArea.height() - scaled.height()) / 2);
        painter->drawPixmap(x, y, scaled);
    } else {
        painter->fillRect(imgArea, QColor("#1E1E24"));
        painter->setPen(QColor(p5::kGray));
        painter->drawText(imgArea, Qt::AlignCenter, tr("NO IMG"));
    }

    // 角标（斜切胶囊，底色 = 类别专属色）
    QString badgeText;
    if (failed) {
        badgeText = tr("ERR");
    } else if (predIdx >= 0) {
        double conf = index.data(RoleConf).toDouble();
        badgeText = index.data(RolePredAbbr).toString() + " "
                    + QString::number(static_cast<int>(conf * 100)) + "%";
    } else {
        badgeText = tr("?");
    }

    QFontMetrics fm(painter->font());
    int badgeW = fm.horizontalAdvance(badgeText) + 16;
    int badgeH = 18;
    QRectF badgeRect(card.right() - badgeW - 3, card.top() + 3, badgeW, badgeH);
    QPainterPath badgePath = chamferRect(badgeRect, 4.0);
    painter->fillPath(badgePath, accent);
    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setPixelSize(9);
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(badgeRect, Qt::AlignCenter, badgeText);

    // 文件名（卡片底部，次要文字）
    QString fname = index.data(Qt::DisplayRole).toString();
    if (fname.length() > 13) fname = fname.left(10) + "...";
    QRectF textRect(card.left(), card.bottom() - 16, card.width(), 14);
    painter->setPen(QColor(p5::kTextDim));
    f.setPixelSize(9);
    f.setBold(false);
    painter->setFont(f);
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, fname);

    painter->restore();
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem&,
                                  const QModelIndex&) const {
    return QSize(kThumbSize + kGap * 2 + 8, kThumbSize + kGap * 2 + 40);
}

// ============================================================
// MainWindow
// ============================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    application::LogManager::instance().setLogFile("qtest.log");

    pipeline_ = std::make_unique<application::PipelineRunner>();
    bridge_   = std::make_shared<RunBridge>();
    pipeline_->setProgressCallback(bridge_);
    pipeline_->setCancelToken(std::make_shared<application::CancelToken>());

    application::ConfigManager cm;
    pipeline_->setConfig(cm.defaults());

    setupUi();

    connect(bridge_.get(), &RunBridge::imageDone,
            this, &MainWindow::onImageDone, Qt::QueuedConnection);
    connect(bridge_.get(), &RunBridge::errorOccurred,
            this, &MainWindow::onErrorOccurred, Qt::QueuedConnection);
    connect(bridge_.get(), &RunBridge::finished,
            this, &MainWindow::onFinished, Qt::QueuedConnection);
}

MainWindow::~MainWindow() {
    auto token = pipeline_->cancelToken();
    if (token) token->cancel();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void MainWindow::setupUi() {
    setWindowTitle(tr("DefectX - Surface Defect Classification"));
    resize(1360, 880);
    setStyleSheet(p5StyleSheet());

    auto* root = new QWidget;
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildToolBar());
    rootLayout->addWidget(buildCentral(), 1);

    setCentralWidget(root);
    setupStatusBar();
}

QWidget* MainWindow::buildToolBar() {
    auto* bar = new QWidget;
    bar->setObjectName("topbar");
    bar->setAttribute(Qt::WA_StyledBackground, true);

    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(16, 10, 16, 10);
    lay->setSpacing(10);

    brandLabel_ = new QLabel("DefectX");
    brandLabel_->setStyleSheet("font-size:20px; font-weight:bold; color:#FFFFFF;");
    lay->addWidget(brandLabel_);

    folderLabel_ = new QLabel(tr("未选择文件夹"));
    folderLabel_->setStyleSheet("color:#B8B8C0;");
    lay->addWidget(folderLabel_, 1);

    selectBtn_ = new ChamferButton(tr("选择文件夹"), false);
    startBtn_  = new ChamferButton(tr("启动"), true);
    stopBtn_   = new ChamferButton(tr("中止"), false);
    startBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);

    connect(selectBtn_, &ChamferButton::clicked, this, &MainWindow::onSelectFolder);
    connect(startBtn_,  &ChamferButton::clicked, this, &MainWindow::onStart);
    connect(stopBtn_,   &ChamferButton::clicked, this, &MainWindow::onStop);

    lay->addWidget(selectBtn_);
    lay->addWidget(startBtn_);
    lay->addWidget(stopBtn_);

    return bar;
}

QWidget* MainWindow::buildFilterPanel() {
    auto* panel = new QWidget;
    panel->setObjectName("panel");
    panel->setAttribute(Qt::WA_StyledBackground, true);

    auto* lay = new QVBoxLayout(panel);
    lay->setContentsMargins(14, 14, 14, 14);
    lay->setSpacing(10);

    auto* title = new QLabel(tr("筛选 / 只看某类"));
    title->setStyleSheet("color:#FFFFFF; font-weight:bold; font-size:14px;");
    lay->addWidget(title);

    legend_ = new ClassLegend;
    connect(legend_, &ClassLegend::selectionChanged, this, &MainWindow::onFilterChanged);
    lay->addWidget(legend_);

    lay->addStretch();
    return panel;
}

QWidget* MainWindow::buildDetailPanel() {
    auto* panel = new QWidget;
    panel->setObjectName("panel");
    panel->setAttribute(Qt::WA_StyledBackground, true);

    auto* lay = new QVBoxLayout(panel);
    lay->setContentsMargins(14, 14, 14, 14);
    lay->setSpacing(10);

    predictionLabel_ = new QLabel(tr("未选择图片"));
    predictionLabel_->setStyleSheet("color:#FFFFFF; font-weight:bold; font-size:14px;");
    lay->addWidget(predictionLabel_);

    imageView_ = new ImagePreview;
    imageView_->setMinimumHeight(220);
    lay->addWidget(imageView_, 1);

    auto* scoreTitle = new QLabel(tr("六类置信度"));
    scoreTitle->setStyleSheet("color:#FFFFFF; font-weight:bold; font-size:13px;");
    lay->addWidget(scoreTitle);

    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        auto* bar = new ScoreBar(model::AllDefectClasses[i]);
        scoreBars_[i] = bar;
        lay->addWidget(bar);
    }

    return panel;
}

QWidget* MainWindow::buildCentral() {
    auto* splitter = new QSplitter(Qt::Horizontal);

    gridView_ = new QListView;
    gridView_->setViewMode(QListView::IconMode);
    gridView_->setIconSize(QSize(120, 120));
    gridView_->setGridSize(QSize(144, 176));
    gridView_->setResizeMode(QListView::Adjust);
    gridView_->setMovement(QListView::Static);
    gridView_->setSelectionMode(QAbstractItemView::SingleSelection);
    gridView_->setUniformItemSizes(true);
    gridView_->setItemDelegate(new ThumbnailDelegate(this));
    gridView_->setFrameShape(QFrame::NoFrame);

    gridModel_ = new ResultListModel(this);
    gridFilter_ = new ResultFilterProxy(this);
    gridFilter_->setSourceModel(gridModel_);
    gridView_->setModel(gridFilter_);

    connect(gridView_, &QListView::clicked, this, &MainWindow::onGridClicked);

    QWidget* filterPanel = buildFilterPanel();
    QWidget* detailPanel = buildDetailPanel();

    splitter->addWidget(filterPanel);
    splitter->addWidget(gridView_);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 2);
    splitter->setSizes({210, 560, 360});

    return splitter;
}

void MainWindow::setupStatusBar() {
    progressBar_ = new QProgressBar;
    progressBar_->setMaximumWidth(200);
    progressBar_->setMaximum(100);
    progressBar_->setValue(0);
    progressBar_->setVisible(false);

    statusTotal_   = new QLabel(tr("总图 0"));
    statusSuccess_ = new QLabel(tr("成功 0"));
    statusFailed_  = new QLabel(tr("失败 0"));
    statusAvg_     = new QLabel(tr("平均 -- ms"));
    statusP95_     = new QLabel(tr("P95 -- ms"));

    statusBar()->addWidget(progressBar_);
    statusBar()->addWidget(statusTotal_);
    statusBar()->addWidget(statusSuccess_);
    statusBar()->addWidget(statusFailed_);
    statusBar()->addWidget(statusAvg_);
    statusBar()->addWidget(statusP95_);
}

void MainWindow::onSelectFolder() {
    QString folder = QFileDialog::getExistingDirectory(this, tr("选择图片文件夹"));
    if (folder.isEmpty()) return;

    selectedFolder_ = folder;
    folderLabel_->setText(folder);
    startBtn_->setEnabled(true);
    pipeline_->setImageFolder(selectedFolder_.toStdString());

    application::LogManager::instance().info("Selected folder: " + selectedFolder_.toStdString());
}

void MainWindow::onStart() {
    setUiEnabled(false);
    gridModel_->clear();
    classCounts_.fill(0);
    for (int i = 0; i < static_cast<int>(model::AllDefectClasses.size()); ++i) {
        legend_->setCount(i, 0);
    }
    progressBar_->setVisible(true);
    progressBar_->setValue(0);

    pipeline_->setCancelToken(std::make_shared<application::CancelToken>());

    disconnect(bridge_.get(), nullptr, this, nullptr);
    connect(bridge_.get(), &RunBridge::imageDone,
            this, &MainWindow::onImageDone, Qt::QueuedConnection);
    connect(bridge_.get(), &RunBridge::errorOccurred,
            this, &MainWindow::onErrorOccurred, Qt::QueuedConnection);
    connect(bridge_.get(), &RunBridge::finished,
            this, &MainWindow::onFinished, Qt::QueuedConnection);

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    auto runner = pipeline_.get();
    workerThread_ = std::thread([runner]() { runner->run(); });

    application::LogManager::instance().info("Pipeline started");
}

void MainWindow::onStop() {
    auto token = pipeline_->cancelToken();
    if (token) token->cancel();
    stopBtn_->setEnabled(false);
    application::LogManager::instance().info("Pipeline stop requested");
}

void MainWindow::onImageDone(int done, int total,
                             const model::ImageEntry& img,
                             const model::ClassificationResult& result) {
    gridModel_->addResult(img, result);

    if (result.predicted.has_value()) {
        int idx = model::defectClassIndex(*result.predicted);
        if (idx >= 0 && idx < static_cast<int>(classCounts_.size())) {
            classCounts_[idx]++;
            legend_->setCount(idx, classCounts_[idx]);
        }
    }

    progressBar_->setMaximum(total);
    progressBar_->setValue(done);
    statusTotal_->setText(tr("总图 %1/%2").arg(done).arg(total));
}

void MainWindow::onErrorOccurred(const model::ImageEntry& img, model::ErrorCode ec) {
    auto fname = std::filesystem::path(img.filePath).filename().string();
    application::LogManager::instance().warn(
        "Error on " + fname + " code=" + std::to_string(static_cast<int>(ec)));
}

void MainWindow::onFinished(const model::RunReport& report) {
    setUiEnabled(true);
    updateStatusBar(report);
    application::LogManager::instance().info("Pipeline finished, displayed in UI");
}

void MainWindow::onGridClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    QModelIndex srcIndex = gridFilter_->mapToSource(index);
    auto* item = gridModel_->itemAt(srcIndex.row());
    if (item) showPreview(*item);
}

void MainWindow::onFilterChanged() {
    gridFilter_->setSelectedClass(legend_->selectedIndex());
}

void MainWindow::updateStatusBar(const model::RunReport& report) {
    statusTotal_->setText(tr("总图 %1").arg(report.totalImages));
    statusSuccess_->setText(tr("成功 %1").arg(report.successCount));
    statusFailed_->setText(tr("失败 %1").arg(report.failedCount));
    statusAvg_->setText(tr("平均 %1 ms").arg(QString::number(report.avgCycleTimeMs, 'f', 1)));
    statusP95_->setText(tr("P95 %1 ms").arg(QString::number(report.p95CycleTimeMs, 'f', 1)));

    statusSuccess_->setStyleSheet(
        QString("color:%1;").arg(report.successCount > 0 ? p5::kSuccess : p5::kGray));
    statusFailed_->setStyleSheet(
        QString("color:%1;").arg(report.failedCount > 0 ? p5::kFail : p5::kGray));
}

void MainWindow::showPreview(const ResultItem& item) {
    cv::Mat mat = cv::imread(item.entry.filePath, cv::IMREAD_COLOR);
    if (!mat.empty()) {
        QImage qimg = RunBridge::toDisplayImage(mat);
        imageView_->setPixmap(QPixmap::fromImage(qimg));
    } else {
        imageView_->clear();
    }

    if (item.entry.status == model::ImageStatus::LoadFailed) {
        predictionLabel_->setText(tr("加载失败"));
        predictionLabel_->setStyleSheet("color:#FF5C5C; font-weight:bold; font-size:14px;");
    } else if (item.result.predicted.has_value()) {
        auto dc = *item.result.predicted;
        predictionLabel_->setText(
            tr("%1 (%2) — 置信度 %3%")
                .arg(QString::fromUtf8(model::defectClassChinese(dc)),
                     QString::fromUtf8(model::defectClassAbbr(dc)),
                     QString::number(static_cast<int>(item.result.confidence * 100))));
        predictionLabel_->setStyleSheet(
            QString("color:%1; font-weight:bold; font-size:14px;")
                .arg(defectClassColor(dc).name()));
    } else {
        predictionLabel_->setText(
            tr("无法判定（最高置信度 %1%）")
                .arg(QString::number(static_cast<int>(item.result.confidence * 100))));
        predictionLabel_->setStyleSheet("color:#B8B8C0; font-weight:bold; font-size:14px;");
    }

    // 固定类别顺序展示；预测类别高亮；得分条 0→目标值 300ms 填充
    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        auto dc = model::AllDefectClasses[i];
        bool highlight = item.result.predicted.has_value() &&
                         *item.result.predicted == dc;
        scoreBars_[i]->setHighlight(highlight);

        auto* anim = new QPropertyAnimation(scoreBars_[i], "score", this);
        anim->setDuration(300);
        anim->setStartValue(scoreBars_[i]->score());
        anim->setEndValue(item.result.scores[i]);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void MainWindow::setUiEnabled(bool enabled) {
    startBtn_->setEnabled(enabled && !selectedFolder_.isEmpty());
    stopBtn_->setEnabled(!enabled);
    selectBtn_->setEnabled(enabled);
    legend_->setEnabled(enabled);
    progressBar_->setVisible(!enabled);
}

}  // namespace ui
}  // namespace qf