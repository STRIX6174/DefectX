#pragma once

#include <QMainWindow>
#include <QListView>
#include <QDockWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSlider>
#include <QLineEdit>
#include <QProgressBar>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QPixmapCache>
#include <QComboBox>

#include <thread>

#include "application/pipeline_runner.h"
#include "ui/run_bridge.h"
#include "model/run_report.h"

#include <vector>
#include <memory>

namespace qf {
namespace ui {

struct ResultItem {
    model::ImageEntry     entry;
    model::InspectionResult result;
    QPixmap               thumbnail;
};

class ResultListModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit ResultListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void addResult(const model::ImageEntry& entry, const model::InspectionResult& result);
    void clear();
    const ResultItem* itemAt(int row) const;

private:
    std::vector<ResultItem> items_;
};

class ResultFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ResultFilterProxy(QObject* parent = nullptr);

    void setShowDefective(bool show);
    void setShowQcRejected(bool show);
    void setShowPassed(bool show);
    void setShowFailed(bool show);
    void setMinConfidence(double minConf);
    void setNameFilter(const QString& filter);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    bool showDefective_   = true;
    bool showQcRejected_  = true;
    bool showPassed_      = true;
    bool showFailed_      = true;
    double minConfidence_ = 0.0;
    QString nameFilter_;
    ResultListModel* sourceModel_ = nullptr;
};

class ThumbnailDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ThumbnailDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

private:
    static constexpr int kThumbSize = 120;
    static constexpr int kGap = 4;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onSelectFolder();
    void onStart();
    void onStop();
    void onImageDone(int done, int total,
                     const model::ImageEntry& img,
                     const model::InspectionResult& result);
    void onErrorOccurred(const model::ImageEntry& img, model::ErrorCode ec);
    void onFinished(const model::RunReport& report);
    void onGridClicked(const QModelIndex& index);
    void onFilterChanged();
    void onPrevDefect();
    void onNextDefect();

private:
    void setupUi();
    void setupToolBar();
    void setupFilterPanel();
    void setupCentralArea();
    void setupStatusBar();
    void updateStatusBar(const model::RunReport& report);
    void showPreview(const ResultItem& item);
    void updateDefectDisplay();
    void setUiEnabled(bool enabled);

    QToolBar*   toolbar_       = nullptr;
    QAction*    startAction_   = nullptr;
    QAction*    stopAction_    = nullptr;
    QLabel*     folderLabel_   = nullptr;

    QDockWidget* filterDock_   = nullptr;
    QCheckBox*   chkDefective_ = nullptr;
    QCheckBox*   chkQcReject_  = nullptr;
    QCheckBox*   chkPassed_    = nullptr;
    QCheckBox*   chkFailed_    = nullptr;
    QSlider*     confSlider_   = nullptr;
    QLineEdit*   nameEdit_     = nullptr;

    QListView*          gridView_   = nullptr;
    ResultListModel*    gridModel_  = nullptr;
    ResultFilterProxy*  gridFilter_ = nullptr;

    QGraphicsView*  defectZoomView_  = nullptr;
    QGraphicsScene* defectZoomScene_ = nullptr;
    QGraphicsView*  overviewView_    = nullptr;
    QGraphicsScene* overviewScene_   = nullptr;

    QPushButton*   prevDefectBtn_   = nullptr;
    QPushButton*   nextDefectBtn_   = nullptr;
    QLabel*        defectNavLabel_  = nullptr;

    QString                          previewImagePath_;
    std::vector<model::DefectRecord> previewDefects_;
    int                              previewDefectIdx_ = 0;

    QLabel*    statusTotal_    = nullptr;
    QLabel*    statusDefect_   = nullptr;
    QLabel*    statusPassed_   = nullptr;
    QLabel*    statusQcRej_    = nullptr;
    QLabel*    statusFailed_   = nullptr;
    QLabel*    statusMissRate_ = nullptr;
    QLabel*    statusOverRate_ = nullptr;
    QLabel*    statusTime_     = nullptr;
    QProgressBar* progressBar_ = nullptr;

    std::unique_ptr<application::PipelineRunner> pipeline_;
    std::shared_ptr<RunBridge> bridge_;
    std::thread            workerThread_;

    QString selectedFolder_;
    QString configPath_;
    QString gtPath_;

    std::vector<std::string> pendingImageFiles_;
};

}
}