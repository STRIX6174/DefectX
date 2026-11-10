#include "ui/main_window.h"
#include "application/config_manager.h"
#include "application/log_manager.h"
#include "model/defect_class.h"

#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QGroupBox>
#include <QPainter>
#include <QFileInfo>
#include <QThread>

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

void ResultFilterProxy::setShowClassified(bool show) {
    showClassified_ = show;
    invalidateFilter();
}
void ResultFilterProxy::setShowUncertain(bool show) {
    showUncertain_ = show;
    invalidateFilter();
}
void ResultFilterProxy::setShowFailed(bool show) {
    showFailed_ = show;
    invalidateFilter();
}
void ResultFilterProxy::setMinConfidence(double minConf) {
    minConfidence_ = minConf;
    invalidateFilter();
}
void ResultFilterProxy::setNameFilter(const QString& filter) {
    nameFilter_ = filter;
    invalidateFilter();
}

bool ResultFilterProxy::filterAcceptsRow(int sourceRow,
                                         const QModelIndex& sourceParent) const {
    Q_UNUSED(sourceParent);
    auto* model = qobject_cast<ResultListModel*>(sourceModel());
    if (!model) return true;

    auto* item = model->itemAt(sourceRow);
    if (!item) return false;

    auto status = item->entry.status;

    if (status == model::ImageStatus::LoadFailed) {
        if (!showFailed_) return false;
    } else {
        bool hasPred = item->result.predicted.has_value();
        if (hasPred) {
            if (!showClassified_) return false;
        } else {
            if (!showUncertain_) return false;
        }

        if (minConfidence_ > 0.0 && item->result.confidence < minConfidence_) {
            return false;
        }
    }

    if (!nameFilter_.isEmpty()) {
        auto fname = QFileInfo(QString::fromStdString(item->entry.filePath)).fileName();
        if (!fname.contains(nameFilter_, Qt::CaseInsensitive)) return false;
    }

    return true;
}

// ============================================================
// ThumbnailDelegate
// ============================================================
ThumbnailDelegate::ThumbnailDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void ThumbnailDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect rect = option.rect.adjusted(kGap, kGap, -kGap, -kGap);

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, QColor(51, 153, 255, 80));
    }

    QPixmap thumb = index.data(Qt::DecorationRole).value<QPixmap>();
    if (!thumb.isNull()) {
        QPixmap scaled = thumb.scaled(rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = rect.x() + (rect.width() - scaled.width()) / 2;
        int y = rect.y() + (rect.height() - scaled.height()) / 2;
        painter->drawPixmap(x, y, scaled);
    } else {
        painter->fillRect(rect, QColor(60, 60, 60));
        painter->setPen(Qt::gray);
        painter->drawText(rect, Qt::AlignCenter, tr("NO IMG"));
    }

    int status = index.data(RoleStatus).toInt();
    int predIdx = index.data(RolePredIdx).toInt();
    double conf = index.data(RoleConf).toDouble();

    QColor badgeColor;
    QString badgeText;
    if (static_cast<model::ImageStatus>(status) == model::ImageStatus::LoadFailed) {
        badgeColor = QColor(120, 120, 120);
        badgeText  = tr("ERR");
    } else if (predIdx >= 0) {
        badgeColor = QColor(220, 120, 30);
        badgeText  = index.data(RolePredAbbr).toString() + " "
                     + QString::number(static_cast<int>(conf * 100)) + "%";
    } else {
        badgeColor = QColor(90, 90, 90);
        badgeText  = tr("?");
    }

    QFontMetrics fm(painter->font());
    int badgeW = fm.horizontalAdvance(badgeText) + 12;
    int badgeH = 18;
    QRect badgeRect(rect.right() - badgeW - 2, rect.bottom() - badgeH - 2, badgeW, badgeH);
    painter->setBrush(badgeColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badgeRect, 3, 3);
    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setPixelSize(9);
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(badgeRect, Qt::AlignCenter, badgeText);

    QString fname = index.data(Qt::DisplayRole).toString();
    if (fname.length() > 14) fname = fname.left(11) + "...";

    QRect textRect(rect.x(), rect.bottom() + 2, rect.width(), 16);
    painter->setPen(Qt::white);
    f.setPixelSize(9);
    f.setBold(false);
    painter->setFont(f);
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, fname);

    painter->restore();
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem&,
                                  const QModelIndex&) const {
    return QSize(kThumbSize + kGap * 2, kThumbSize + 24);
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
    resize(1200, 800);

    setupToolBar();
    setupFilterPanel();
    setupCentralArea();
    setupStatusBar();
}

void MainWindow::setupToolBar() {
    toolbar_ = addToolBar(tr("Main"));
    toolbar_->setMovable(false);

    QAction* folderAction = toolbar_->addAction(tr("Select Folder"));
    connect(folderAction, &QAction::triggered, this, &MainWindow::onSelectFolder);

    startAction_ = toolbar_->addAction(tr("Start"));
    startAction_->setEnabled(false);
    connect(startAction_, &QAction::triggered, this, &MainWindow::onStart);

    stopAction_ = toolbar_->addAction(tr("Stop"));
    stopAction_->setEnabled(false);
    connect(stopAction_, &QAction::triggered, this, &MainWindow::onStop);

    toolbar_->addSeparator();
    folderLabel_ = new QLabel(tr("No folder selected"));
    toolbar_->addWidget(folderLabel_);
}

void MainWindow::setupFilterPanel() {
    filterDock_ = new QDockWidget(tr("Filters"), this);
    filterDock_->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable);

    QWidget* container = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(container);

    QGroupBox* statusGroup = new QGroupBox(tr("Result"));
    QVBoxLayout* statusLayout = new QVBoxLayout;
    chkClassified_ = new QCheckBox(tr("Classified (with label)"));
    chkClassified_->setChecked(true);
    chkUncertain_  = new QCheckBox(tr("Uncertain (low confidence)"));
    chkUncertain_->setChecked(true);
    chkFailed_     = new QCheckBox(tr("Failed"));
    chkFailed_->setChecked(true);
    statusLayout->addWidget(chkClassified_);
    statusLayout->addWidget(chkUncertain_);
    statusLayout->addWidget(chkFailed_);
    statusGroup->setLayout(statusLayout);
    layout->addWidget(statusGroup);

    QGroupBox* confGroup = new QGroupBox(tr("Min Confidence"));
    QVBoxLayout* confLayout = new QVBoxLayout;
    confSlider_ = new QSlider(Qt::Horizontal);
    confSlider_->setRange(0, 100);
    confSlider_->setValue(0);
    confLayout->addWidget(confSlider_);
    confGroup->setLayout(confLayout);
    layout->addWidget(confGroup);

    QGroupBox* searchGroup = new QGroupBox(tr("Search"));
    QVBoxLayout* searchLayout = new QVBoxLayout;
    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText(tr("File name..."));
    searchLayout->addWidget(nameEdit_);
    searchGroup->setLayout(searchLayout);
    layout->addWidget(searchGroup);

    layout->addStretch();
    filterDock_->setWidget(container);
    addDockWidget(Qt::LeftDockWidgetArea, filterDock_);
}

void MainWindow::setupCentralArea() {
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    gridView_ = new QListView;
    gridView_->setViewMode(QListView::IconMode);
    gridView_->setIconSize(QSize(120, 120));
    gridView_->setGridSize(QSize(140, 150));
    gridView_->setResizeMode(QListView::Adjust);
    gridView_->setMovement(QListView::Static);
    gridView_->setSelectionMode(QAbstractItemView::SingleSelection);
    gridView_->setUniformItemSizes(true);
    gridView_->setItemDelegate(new ThumbnailDelegate(this));

    gridModel_ = new ResultListModel(this);
    gridFilter_ = new ResultFilterProxy(this);
    gridFilter_->setSourceModel(gridModel_);
    gridView_->setModel(gridFilter_);

    connect(gridView_, &QListView::clicked, this, &MainWindow::onGridClicked);
    connect(chkClassified_, &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(chkUncertain_,  &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(chkFailed_,     &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(confSlider_,    &QSlider::valueChanged, this, &MainWindow::onFilterChanged);
    connect(nameEdit_,      &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);

    // 右侧详情面板
    QWidget* detailContainer = new QWidget;
    QVBoxLayout* detailLayout = new QVBoxLayout(detailContainer);
    detailLayout->setContentsMargins(4, 4, 4, 4);

    predictionLabel_ = new QLabel(tr("No image selected"));
    predictionLabel_->setStyleSheet("font-weight: bold; font-size: 14px;");
    detailLayout->addWidget(predictionLabel_);

    imageView_ = new QGraphicsView;
    imageScene_ = new QGraphicsScene(this);
    imageView_->setScene(imageScene_);
    imageView_->setDragMode(QGraphicsView::ScrollHandDrag);
    imageView_->setStyleSheet("QGraphicsView { border: 1px solid #555; background: #1e1e1e; }");
    detailLayout->addWidget(imageView_, 1);

    // 六类置信度条
    QGroupBox* scoreGroup = new QGroupBox(tr("Six-class confidence"));
    QVBoxLayout* scoreLayout = new QVBoxLayout(scoreGroup);

    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        auto dc = model::AllDefectClasses[i];

        QHBoxLayout* rowLayout = new QHBoxLayout;
        QLabel* nameLabel = new QLabel(
            QString("%1 (%2)").arg(QString::fromUtf8(model::defectClassChinese(dc)),
                                   QString::fromUtf8(model::defectClassAbbr(dc))));
        nameLabel->setFixedWidth(140);
        nameLabel->setStyleSheet("color: #ddd;");

        QProgressBar* bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setFormat("%p%");
        bar->setTextVisible(true);
        bar->setMaximumHeight(18);

        rowLayout->addWidget(nameLabel);
        rowLayout->addWidget(bar, 1);

        scoreNameLabels_[i] = nameLabel;
        scoreBars_[i] = bar;
        scoreLayout->addLayout(rowLayout);
    }
    detailLayout->addWidget(scoreGroup);

    splitter->addWidget(gridView_);
    splitter->addWidget(detailContainer);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    setCentralWidget(splitter);
}

void MainWindow::setupStatusBar() {
    statusBar()->setStyleSheet("QStatusBar { background: #2b2b2b; color: #ddd; }");

    progressBar_ = new QProgressBar;
    progressBar_->setMaximumWidth(200);
    progressBar_->setMaximum(100);
    progressBar_->setValue(0);
    progressBar_->setVisible(false);

    statusTotal_   = new QLabel(tr("Total: 0"));
    statusSuccess_ = new QLabel(tr("Success: 0"));
    statusFailed_  = new QLabel(tr("Failed: 0"));
    statusAvg_     = new QLabel(tr("Avg: -- ms"));
    statusP95_     = new QLabel(tr("P95: -- ms"));

    statusBar()->addWidget(progressBar_);
    statusBar()->addWidget(statusTotal_);
    statusBar()->addWidget(statusSuccess_);
    statusBar()->addWidget(statusFailed_);
    statusBar()->addWidget(statusAvg_);
    statusBar()->addWidget(statusP95_);
}

void MainWindow::onSelectFolder() {
    QString folder = QFileDialog::getExistingDirectory(this, tr("Select Image Folder"));
    if (folder.isEmpty()) return;

    selectedFolder_ = folder;
    folderLabel_->setText(folder);
    startAction_->setEnabled(true);
    pipeline_->setImageFolder(selectedFolder_.toStdString());

    application::LogManager::instance().info("Selected folder: " + selectedFolder_.toStdString());
}

void MainWindow::onStart() {
    setUiEnabled(false);
    gridModel_->clear();
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
    stopAction_->setEnabled(false);
    application::LogManager::instance().info("Pipeline stop requested");
}

void MainWindow::onImageDone(int done, int total,
                             const model::ImageEntry& img,
                             const model::ClassificationResult& result) {
    gridModel_->addResult(img, result);
    progressBar_->setMaximum(total);
    progressBar_->setValue(done);
    statusTotal_->setText(tr("Total: %1/%2").arg(done).arg(total));
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
    gridFilter_->setShowClassified(chkClassified_->isChecked());
    gridFilter_->setShowUncertain(chkUncertain_->isChecked());
    gridFilter_->setShowFailed(chkFailed_->isChecked());
    gridFilter_->setMinConfidence(confSlider_->value() / 100.0);
    gridFilter_->setNameFilter(nameEdit_->text());
}

void MainWindow::updateStatusBar(const model::RunReport& report) {
    statusTotal_->setText(tr("Total: %1").arg(report.totalImages));
    statusSuccess_->setText(tr("Success: %1").arg(report.successCount));
    statusFailed_->setText(tr("Failed: %1").arg(report.failedCount));
    statusAvg_->setText(tr("Avg: %1 ms").arg(QString::number(report.avgCycleTimeMs, 'f', 1)));
    statusP95_->setText(tr("P95: %1 ms").arg(QString::number(report.p95CycleTimeMs, 'f', 1)));
}

void MainWindow::showPreview(const ResultItem& item) {
    imageScene_->clear();

    cv::Mat mat = cv::imread(item.entry.filePath, cv::IMREAD_COLOR);
    if (!mat.empty()) {
        QImage qimg = RunBridge::toDisplayImage(mat);
        QPixmap pm = QPixmap::fromImage(qimg);
        imageScene_->addPixmap(pm);
        imageScene_->setSceneRect(pm.rect());
        imageView_->fitInView(imageScene_->sceneRect(), Qt::KeepAspectRatio);
    }

    if (item.entry.status == model::ImageStatus::LoadFailed) {
        predictionLabel_->setText(tr("Load failed"));
        predictionLabel_->setStyleSheet("color: #ff6666; font-weight: bold; font-size: 14px;");
    } else if (item.result.predicted.has_value()) {
        auto dc = *item.result.predicted;
        predictionLabel_->setText(
            tr("Prediction: %1 (%2) - %3%")
                .arg(QString::fromUtf8(model::defectClassChinese(dc)),
                     QString::fromUtf8(model::defectClassAbbr(dc)),
                     QString::number(static_cast<int>(item.result.confidence * 100))));
        predictionLabel_->setStyleSheet("color: #66ccff; font-weight: bold; font-size: 14px;");
    } else {
        predictionLabel_->setText(
            tr("Unable to classify (max confidence %1%)")
                .arg(QString::number(static_cast<int>(item.result.confidence * 100))));
        predictionLabel_->setStyleSheet("color: #cccccc; font-weight: bold; font-size: 14px;");
    }

    for (size_t i = 0; i < model::AllDefectClasses.size(); ++i) {
        int pct = static_cast<int>(item.result.scores[i] * 100);
        scoreBars_[i]->setValue(pct);
    }
}

void MainWindow::setUiEnabled(bool enabled) {
    startAction_->setEnabled(enabled && !selectedFolder_.isEmpty());
    stopAction_->setEnabled(!enabled);
    folderLabel_->setEnabled(enabled);
    progressBar_->setVisible(!enabled);

    chkClassified_->setEnabled(enabled);
    chkUncertain_->setEnabled(enabled);
    chkFailed_->setEnabled(enabled);
    confSlider_->setEnabled(enabled);
    nameEdit_->setEnabled(enabled);
}

}  // namespace ui
}  // namespace qf