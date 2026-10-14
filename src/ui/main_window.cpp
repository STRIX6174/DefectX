#include "ui/main_window.h"
#include "application/config_manager.h"
#include "application/log_manager.h"
#include "model/defect_type.h"

#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QFormLayout>
#include <QGroupBox>
#include <QPainter>
#include <QFileInfo>
#include <QMessageBox>
#include <QThread>
#include <QApplication>
#include <QFrame>

#include <filesystem>
#include <opencv2/imgcodecs.hpp>

namespace qf {
namespace ui {

// ============================================================
// ResultListModel
// ============================================================

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
    case Qt::DisplayRole: {
        auto fname = std::filesystem::path(item.entry.filePath).filename().string();
        return QString::fromStdString(fname);
    }
    case Qt::UserRole: {
        QString types;
        for (auto& d : item.result.defects) {
            if (!types.isEmpty()) types += ",";
            types += model::defectTypeAbbr(d.type);
        }
        return types;
    }
    case Qt::UserRole + 1:
        return static_cast<int>(item.entry.status);
    case Qt::UserRole + 2:
        return static_cast<int>(item.result.defects.size());
    case Qt::UserRole + 3: {
        double maxConf = 0.0;
        for (auto& d : item.result.defects)
            maxConf = std::max(maxConf, d.confidence);
        return maxConf;
    }
    case Qt::UserRole + 4:
        return QString::fromStdString(item.entry.filePath);
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
                                const model::InspectionResult& result) {
    int row = static_cast<int>(items_.size());
    beginInsertRows(QModelIndex{}, row, row);

    ResultItem item;
    item.entry  = entry;
    item.result = result;

    auto fname = std::filesystem::path(entry.filePath).string();
    QPixmap pm;
    if (QPixmapCache::find(QString::fromStdString(fname), &pm)) {
        item.thumbnail = pm;
    } else {
        cv::Mat mat = cv::imread(fname, cv::IMREAD_COLOR);
        if (!mat.empty()) {
            QImage qimg = RunBridge::toDisplayImage(entry, mat);
            pm = QPixmap::fromImage(qimg.scaled(120, 120, Qt::KeepAspectRatio, Qt::FastTransformation));
            item.thumbnail = pm;
            QPixmapCache::insert(QString::fromStdString(fname), pm);
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

void ResultFilterProxy::setShowDefective(bool show) {
    showDefective_ = show;
    invalidateFilter();
}

void ResultFilterProxy::setShowQcRejected(bool show) {
    showQcRejected_ = show;
    invalidateFilter();
}

void ResultFilterProxy::setShowPassed(bool show) {
    showPassed_ = show;
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
    auto* model = qobject_cast<ResultListModel*>(sourceModel());
    if (!model) return true;

    auto* item = model->itemAt(sourceRow);
    if (!item) return false;

    auto status = item->entry.status;
    switch (status) {
    case model::ImageStatus::Defective:
        if (!showDefective_) return false;
        break;
    case model::ImageStatus::Invalid_Qc:
        if (!showQcRejected_) return false;
        break;
    case model::ImageStatus::Pass:
        if (!showPassed_) return false;
        break;
    case model::ImageStatus::LoadFailed:
        if (!showFailed_) return false;
        break;
    default:
        break;
    }

    if (minConfidence_ > 0.0 && status == model::ImageStatus::Defective) {
        double maxConf = 0.0;
        for (auto& d : item->result.defects)
            maxConf = std::max(maxConf, d.confidence);
        if (maxConf < minConfidence_) return false;
    }

    if (!nameFilter_.isEmpty()) {
        auto fname = std::filesystem::path(item->entry.filePath).filename().string();
        if (!QString::fromStdString(fname).contains(nameFilter_, Qt::CaseInsensitive))
            return false;
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
        painter->drawText(rect, Qt::AlignCenter, "NO IMG");
    }

    int status = index.data(Qt::UserRole + 1).toInt();
    QColor badgeColor;
    QString badgeText;
    switch (static_cast<model::ImageStatus>(status)) {
    case model::ImageStatus::Defective:
        badgeColor = QColor(220, 50, 50);
        badgeText  = index.data(Qt::UserRole).toString() + "x"
                     + QString::number(index.data(Qt::UserRole + 2).toInt());
        break;
    case model::ImageStatus::Invalid_Qc:
        badgeColor = QColor(220, 180, 30);
        badgeText  = "QC";
        break;
    case model::ImageStatus::Pass:
        badgeColor = QColor(50, 180, 50);
        badgeText  = "OK";
        break;
    case model::ImageStatus::LoadFailed:
        badgeColor = QColor(120, 120, 120);
        badgeText  = "ERR";
        break;
    default:
        badgeColor = QColor(100, 100, 100);
        badgeText  = "...";
        break;
    }

    int badgeW = 28, badgeH = 16;
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

    QFileInfo fi(index.data(Qt::UserRole + 4).toString());
    QString fname = fi.fileName();
    if (fname.length() > 12) fname = fname.left(9) + "...";

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
    auto params = cm.defaults();
    pipeline_->setConfig(params);
    pipeline_->setSaveMarkedImages(true);

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
    setWindowTitle("QTest - Industrial Surface Defect Detection");
    resize(1200, 800);

    setupToolBar();
    setupFilterPanel();
    setupCentralArea();
    setupStatusBar();
}

void MainWindow::setupToolBar() {
    toolbar_ = addToolBar("Main");

    QAction* folderAction = toolbar_->addAction("Select Folder");
    connect(folderAction, &QAction::triggered, this, &MainWindow::onSelectFolder);

    startAction_ = toolbar_->addAction("Start");
    startAction_->setEnabled(false);
    connect(startAction_, &QAction::triggered, this, &MainWindow::onStart);

    stopAction_ = toolbar_->addAction("Stop");
    stopAction_->setEnabled(false);
    connect(stopAction_, &QAction::triggered, this, &MainWindow::onStop);

    toolbar_->addSeparator();
    folderLabel_ = new QLabel("No folder selected");
    toolbar_->addWidget(folderLabel_);
}

void MainWindow::setupFilterPanel() {
    filterDock_ = new QDockWidget("Filters", this);
    filterDock_->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable);

    QWidget* container = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(container);

    QGroupBox* statusGroup = new QGroupBox("Status");
    QVBoxLayout* statusLayout = new QVBoxLayout;
    chkDefective_ = new QCheckBox("Defect");
    chkDefective_->setChecked(true);
    chkQcReject_  = new QCheckBox("QC Reject");
    chkQcReject_->setChecked(true);
    chkPassed_    = new QCheckBox("Pass");
    chkPassed_->setChecked(true);
    chkFailed_    = new QCheckBox("Failed");
    chkFailed_->setChecked(true);
    statusLayout->addWidget(chkDefective_);
    statusLayout->addWidget(chkQcReject_);
    statusLayout->addWidget(chkPassed_);
    statusLayout->addWidget(chkFailed_);
    statusGroup->setLayout(statusLayout);
    layout->addWidget(statusGroup);

    QGroupBox* confGroup = new QGroupBox("Min Confidence");
    QVBoxLayout* confLayout = new QVBoxLayout;
    confSlider_ = new QSlider(Qt::Horizontal);
    confSlider_->setRange(0, 100);
    confSlider_->setValue(0);
    confLayout->addWidget(confSlider_);
    confGroup->setLayout(confLayout);
    layout->addWidget(confGroup);

    QGroupBox* searchGroup = new QGroupBox("Search");
    QVBoxLayout* searchLayout = new QVBoxLayout;
    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText("File name...");
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

    connect(chkDefective_, &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(chkQcReject_,  &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(chkPassed_,    &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(chkFailed_,    &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(confSlider_,   &QSlider::valueChanged, this, &MainWindow::onFilterChanged);
    connect(nameEdit_,     &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);

    QWidget* previewContainer = new QWidget;
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);

    previewView_ = new QGraphicsView;
    previewScene_ = new QGraphicsScene(this);
    previewView_->setScene(previewScene_);
    previewView_->setDragMode(QGraphicsView::ScrollHandDrag);
    previewView_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    previewLayout->addWidget(previewView_);

    splitter->addWidget(gridView_);
    splitter->addWidget(previewContainer);
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

    statusTotal_    = new QLabel("Total: 0");
    statusDefect_   = new QLabel("Defect: 0");
    statusPassed_   = new QLabel("Pass: 0");
    statusQcRej_    = new QLabel("QC Rej: 0");
    statusFailed_   = new QLabel("Failed: 0");
    statusMissRate_ = new QLabel("Miss: --");
    statusOverRate_ = new QLabel("Over: --");
    statusTime_     = new QLabel("Time: --");

    statusBar()->addWidget(progressBar_);
    statusBar()->addWidget(statusTotal_);
    statusBar()->addWidget(statusDefect_);
    statusBar()->addWidget(statusPassed_);
    statusBar()->addWidget(statusQcRej_);
    statusBar()->addWidget(statusFailed_);
    statusBar()->addWidget(statusMissRate_);
    statusBar()->addWidget(statusOverRate_);
    statusBar()->addPermanentWidget(statusTime_);
}

void MainWindow::onSelectFolder() {
    QString folder = QFileDialog::getExistingDirectory(this, "Select Image Folder");
    if (folder.isEmpty()) return;

    selectedFolder_ = folder;
    folderLabel_->setText(folder);
    startAction_->setEnabled(true);

    pipeline_->setImageFolder(selectedFolder_.toStdString());

    QString outDir = folder + "/_output";
    pipeline_->setOutputDir(outDir.toStdString());
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

    auto runner = pipeline_.get();
    auto token  = pipeline_->cancelToken();

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    workerThread_ = std::thread([runner, token]() {
        runner->run();
    });

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
                              const model::InspectionResult& result) {
    gridModel_->addResult(img, result);
    progressBar_->setMaximum(total);
    progressBar_->setValue(done);

    QString status = "Total: " + QString::number(done) + "/" + QString::number(total);
    statusTotal_->setText(status);
}

void MainWindow::onErrorOccurred(const model::ImageEntry& img, model::ErrorCode ec) {
    auto fname = std::filesystem::path(img.filePath).filename().string();
    application::LogManager::instance().warn("Error on " + fname + " code=" +
        std::to_string(static_cast<int>(ec)));
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
    gridFilter_->setShowDefective(chkDefective_->isChecked());
    gridFilter_->setShowQcRejected(chkQcReject_->isChecked());
    gridFilter_->setShowPassed(chkPassed_->isChecked());
    gridFilter_->setShowFailed(chkFailed_->isChecked());
    gridFilter_->setMinConfidence(confSlider_->value() / 100.0);
    gridFilter_->setNameFilter(nameEdit_->text());
}

void MainWindow::updateStatusBar(const model::RunReport& report) {
    statusTotal_->setText("Total: " + QString::number(report.totalImages));
    statusDefect_->setText("Defect: " + QString::number(report.defective));
    statusPassed_->setText("Pass: " + QString::number(report.passed));
    statusQcRej_->setText("QC Rej: " + QString::number(report.qcRejected));
    statusFailed_->setText("Failed: " + QString::number(report.failedCount));

    if (report.missRate.has_value())
        statusMissRate_->setText("Miss: " + QString::number(*report.missRate * 100, 'f', 1) + "%");
    else
        statusMissRate_->setText("Miss: --");

    if (report.overkillRate.has_value())
        statusOverRate_->setText("Over: " + QString::number(*report.overkillRate * 100, 'f', 1) + "%");
    else
        statusOverRate_->setText("Over: --");

    int secs = static_cast<int>(report.totalElapsedMs / 1000);
    int mins = secs / 60;
    secs = secs % 60;
    statusTime_->setText(QString("Time: %1m%2s").arg(mins).arg(secs, 2, 10, QChar('0')));
}

void MainWindow::showPreview(const ResultItem& item) {
    previewScene_->clear();

    cv::Mat mat = cv::imread(item.entry.filePath, cv::IMREAD_COLOR);
    if (mat.empty()) return;

    application::ResultMarker marker;
    cv::Mat marked = marker.drawDefectOverlay(mat, item.result);

    QImage qimg = RunBridge::toDisplayImage(item.entry, marked, &item.result);
    QPixmap pm = QPixmap::fromImage(qimg);

    previewScene_->addPixmap(pm);
    previewScene_->setSceneRect(pm.rect());
    previewView_->fitInView(previewScene_->sceneRect(), Qt::KeepAspectRatio);
}

void MainWindow::setUiEnabled(bool enabled) {
    startAction_->setEnabled(enabled && !selectedFolder_.isEmpty());
    stopAction_->setEnabled(!enabled);
    folderLabel_->setEnabled(enabled);
    progressBar_->setVisible(!enabled);
}

}
}