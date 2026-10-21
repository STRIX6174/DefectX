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
#include <opencv2/imgproc.hpp>

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
        painter->drawText(rect, Qt::AlignCenter, tr("NO IMG"));
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
        badgeText  = tr("QC");
        break;
    case model::ImageStatus::Pass:
        badgeColor = QColor(50, 180, 50);
        badgeText  = tr("OK");
        break;
    case model::ImageStatus::LoadFailed:
        badgeColor = QColor(120, 120, 120);
        badgeText  = tr("ERR");
        break;
    default:
        badgeColor = QColor(100, 100, 100);
        badgeText  = tr("...");
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
    setWindowTitle(tr("QTest - Industrial Surface Defect Detection"));
    resize(1200, 800);

    setupToolBar();
    setupFilterPanel();
    setupCentralArea();
    setupStatusBar();
}

void MainWindow::setupToolBar() {
    toolbar_ = addToolBar(tr("Main"));

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

    QGroupBox* statusGroup = new QGroupBox(tr("Status"));
    QVBoxLayout* statusLayout = new QVBoxLayout;
    chkDefective_ = new QCheckBox(tr("Defect"));
    chkDefective_->setChecked(true);
    chkQcReject_  = new QCheckBox(tr("QC Reject"));
    chkQcReject_->setChecked(true);
    chkPassed_    = new QCheckBox(tr("Pass"));
    chkPassed_->setChecked(true);
    chkFailed_    = new QCheckBox(tr("Failed"));
    chkFailed_->setChecked(true);
    statusLayout->addWidget(chkDefective_);
    statusLayout->addWidget(chkQcReject_);
    statusLayout->addWidget(chkPassed_);
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

    connect(chkDefective_, &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(chkQcReject_,  &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(chkPassed_,    &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(chkFailed_,    &QCheckBox::toggled, this, &MainWindow::onFilterChanged);
    connect(confSlider_,   &QSlider::valueChanged, this, &MainWindow::onFilterChanged);
    connect(nameEdit_,     &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);

    QWidget* previewContainer = new QWidget;
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(4, 4, 4, 4);

    QHBoxLayout* navLayout = new QHBoxLayout;
    prevDefectBtn_ = new QPushButton(tr("<"));
    prevDefectBtn_->setFixedWidth(30);
    prevDefectBtn_->setEnabled(false);
    nextDefectBtn_ = new QPushButton(tr(">"));
    nextDefectBtn_->setFixedWidth(30);
    nextDefectBtn_->setEnabled(false);
    defectNavLabel_ = new QLabel(tr("No defects"));
    defectNavLabel_->setAlignment(Qt::AlignCenter);
    defectNavLabel_->setStyleSheet("color: #ccc; font-weight: bold;");
    navLayout->addWidget(prevDefectBtn_);
    navLayout->addWidget(defectNavLabel_, 1);
    navLayout->addWidget(nextDefectBtn_);
    previewLayout->addLayout(navLayout);

    connect(prevDefectBtn_, &QPushButton::clicked, this, &MainWindow::onPrevDefect);
    connect(nextDefectBtn_, &QPushButton::clicked, this, &MainWindow::onNextDefect);

    defectZoomView_ = new QGraphicsView;
    defectZoomScene_ = new QGraphicsScene(this);
    defectZoomView_->setScene(defectZoomScene_);
    defectZoomView_->setDragMode(QGraphicsView::ScrollHandDrag);
    defectZoomView_->setStyleSheet("QGraphicsView { border: 1px solid #555; background: #1e1e1e; }");
    previewLayout->addWidget(defectZoomView_, 1);

    QLabel* overviewLabel = new QLabel(tr("Full image overview:"));
    overviewLabel->setStyleSheet("color: #888; font-size: 10px; margin-top: 4px;");
    previewLayout->addWidget(overviewLabel);

    overviewView_ = new QGraphicsView;
    overviewScene_ = new QGraphicsScene(this);
    overviewView_->setScene(overviewScene_);
    overviewView_->setFixedHeight(170);
    overviewView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    overviewView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    overviewView_->setStyleSheet("QGraphicsView { border: 1px solid #555; background: #1e1e1e; }");
    previewLayout->addWidget(overviewView_);

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

    statusTotal_    = new QLabel(tr("Total: 0"));
    statusDefect_   = new QLabel(tr("Defect: 0"));
    statusPassed_   = new QLabel(tr("Pass: 0"));
    statusQcRej_    = new QLabel(tr("QC Rej: 0"));
    statusFailed_   = new QLabel(tr("Failed: 0"));
    statusMissRate_ = new QLabel(tr("Miss: --"));
    statusOverRate_ = new QLabel(tr("Over: --"));
    statusTime_     = new QLabel(tr("Time: --"));

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
    QString folder = QFileDialog::getExistingDirectory(this, tr("Select Image Folder"));
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

    statusTotal_->setText(tr("Total: %1/%2").arg(done).arg(total));
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
    statusTotal_->setText(tr("Total: %1").arg(report.totalImages));
    statusDefect_->setText(tr("Defect: %1").arg(report.defective));
    statusPassed_->setText(tr("Pass: %1").arg(report.passed));
    statusQcRej_->setText(tr("QC Rej: %1").arg(report.qcRejected));
    statusFailed_->setText(tr("Failed: %1").arg(report.failedCount));

    if (report.missRate.has_value())
        statusMissRate_->setText(tr("Miss: %1%").arg(QString::number(*report.missRate * 100, 'f', 1)));
    else
        statusMissRate_->setText(tr("Miss: --"));

    if (report.overkillRate.has_value())
        statusOverRate_->setText(tr("Over: %1%").arg(QString::number(*report.overkillRate * 100, 'f', 1)));
    else
        statusOverRate_->setText(tr("Over: --"));

    int secs = static_cast<int>(report.totalElapsedMs / 1000);
    int mins = secs / 60;
    secs = secs % 60;
    statusTime_->setText(tr("Time: %1m%2s").arg(mins).arg(secs, 2, 10, QChar('0')));
}

void MainWindow::showPreview(const ResultItem& item) {
    previewImagePath_ = QString::fromStdString(item.entry.filePath);
    previewDefects_   = item.result.defects;
    previewDefectIdx_ = 0;

    if (previewDefects_.empty()) {
        defectNavLabel_->setText(tr("No defects"));
        prevDefectBtn_->setEnabled(false);
        nextDefectBtn_->setEnabled(false);

        defectZoomScene_->clear();
        overviewScene_->clear();

        cv::Mat mat = cv::imread(item.entry.filePath, cv::IMREAD_COLOR);
        if (mat.empty()) return;

        QImage qimg = RunBridge::toDisplayImage(item.entry, mat);
        QPixmap pm = QPixmap::fromImage(qimg);
        defectZoomScene_->addPixmap(pm);
        defectZoomScene_->setSceneRect(pm.rect());
        defectZoomView_->fitInView(defectZoomScene_->sceneRect(), Qt::KeepAspectRatio);

        QPixmap ovPm = pm.scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        overviewScene_->addPixmap(ovPm);
        overviewScene_->setSceneRect(ovPm.rect());
        overviewView_->fitInView(overviewScene_->sceneRect(), Qt::KeepAspectRatio);
        return;
    }

    updateDefectDisplay();
}

void MainWindow::setUiEnabled(bool enabled) {
    startAction_->setEnabled(enabled && !selectedFolder_.isEmpty());
    stopAction_->setEnabled(!enabled);
    folderLabel_->setEnabled(enabled);
    progressBar_->setVisible(!enabled);
}

void MainWindow::updateDefectDisplay() {
    defectZoomScene_->clear();
    overviewScene_->clear();

    cv::Mat mat = cv::imread(previewImagePath_.toStdString(), cv::IMREAD_COLOR);
    if (mat.empty()) return;

    int n = static_cast<int>(previewDefects_.size());
    int idx = previewDefectIdx_;

    defectNavLabel_->setText(tr("Defect %1 / %2").arg(idx + 1).arg(n));
    prevDefectBtn_->setEnabled(idx > 0);
    nextDefectBtn_->setEnabled(idx < n - 1);

    const auto& def = previewDefects_[idx];

    int pad = std::max(15, std::max(def.roi.width, def.roi.height) / 2);
    int cx = def.roi.x + def.roi.width  / 2;
    int cy = def.roi.y + def.roi.height / 2;
    int x1 = std::max(0, cx - def.roi.width  / 2 - pad);
    int y1 = std::max(0, cy - def.roi.height / 2 - pad);
    int x2 = std::min(mat.cols, cx + def.roi.width  / 2 + pad);
    int y2 = std::min(mat.rows, cy + def.roi.height / 2 + pad);

    cv::Rect cropRect(x1, y1, x2 - x1, y2 - y1);
    if (cropRect.width < 4 || cropRect.height < 4) {
        cropRect = cv::Rect(0, 0, mat.cols, mat.rows);
    }

    cv::Mat crop = mat(cropRect).clone();

    int shiftX = def.roi.x - x1;
    int shiftY = def.roi.y - y1;
    cv::Rect shiftedRoi(shiftX, shiftY, def.roi.width, def.roi.height);
    cv::rectangle(crop, shiftedRoi, cv::Scalar(0, 0, 255), 2);

    std::string label = std::string(model::defectTypeName(def.type))
                      + "  " + std::to_string(static_cast<int>(def.confidence * 100)) + "%";
    int baseline = 0;
    cv::Size ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);
    cv::rectangle(crop,
                  cv::Point(shiftedRoi.x, shiftedRoi.y - ts.height - 6),
                  cv::Point(shiftedRoi.x + ts.width + 4, shiftedRoi.y),
                  cv::Scalar(0, 0, 255), cv::FILLED);
    cv::putText(crop, label,
                cv::Point(shiftedRoi.x + 2, shiftedRoi.y - 4),
                cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(255, 255, 255), 2);

    cv::cvtColor(crop, crop, cv::COLOR_BGR2RGB);
    QImage cropImg(crop.data, crop.cols, crop.rows,
                   static_cast<int>(crop.step), QImage::Format_RGB888);
    QPixmap cropPm = QPixmap::fromImage(cropImg.copy());
    QPixmap scaledCrop = cropPm.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    defectZoomScene_->addPixmap(scaledCrop);
    defectZoomScene_->setSceneRect(scaledCrop.rect());
    defectZoomView_->fitInView(defectZoomScene_->sceneRect(), Qt::KeepAspectRatio);

    cv::Mat overview = mat.clone();
    for (int i = 0; i < n; ++i) {
        const auto& d = previewDefects_[i];
        cv::Scalar color = (i == idx) ? cv::Scalar(0, 200, 255) : cv::Scalar(0, 255, 0);
        int thick = (i == idx) ? 3 : 1;
        cv::rectangle(overview,
                      cv::Rect(d.roi.x, d.roi.y, d.roi.width, d.roi.height),
                      color, thick);
        std::string num = std::to_string(i + 1);
        cv::putText(overview, num,
                    cv::Point(d.roi.x + 2, d.roi.y + 14),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
    }

    cv::cvtColor(overview, overview, cv::COLOR_BGR2RGB);
    QImage ovImg(overview.data, overview.cols, overview.rows,
                 static_cast<int>(overview.step), QImage::Format_RGB888);
    QPixmap ovPm = QPixmap::fromImage(ovImg.copy());
    overviewScene_->addPixmap(ovPm);
    overviewScene_->setSceneRect(ovPm.rect());
    overviewView_->fitInView(overviewScene_->sceneRect(), Qt::KeepAspectRatio);
}

void MainWindow::onPrevDefect() {
    if (previewDefectIdx_ > 0) {
        --previewDefectIdx_;
        updateDefectDisplay();
    }
}

void MainWindow::onNextDefect() {
    int n = static_cast<int>(previewDefects_.size());
    if (previewDefectIdx_ < n - 1) {
        ++previewDefectIdx_;
        updateDefectDisplay();
    }
}

}
}