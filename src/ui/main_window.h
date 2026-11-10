#pragma once

#include <QMainWindow>
#include <QListView>
#include <QSlider>
#include <QLineEdit>
#include <QProgressBar>
#include <QLabel>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QPixmapCache>

#include <array>
#include <thread>
#include <vector>
#include <memory>

#include "application/pipeline_runner.h"
#include "ui/run_bridge.h"
#include "model/run_report.h"
#include "model/classification_result.h"

namespace qf {
namespace ui {

class ChamferButton;
class ScoreBar;
class ClassLegend;
class ImagePreview;

struct ResultItem {
    model::ImageEntry            entry;
    model::ClassificationResult  result;
    QPixmap                      thumbnail;
};

class ResultListModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit ResultListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void addResult(const model::ImageEntry& entry, const model::ClassificationResult& result);
    void clear();
    const ResultItem* itemAt(int row) const;

private:
    std::vector<ResultItem> items_;
};

class ResultFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ResultFilterProxy(QObject* parent = nullptr);

    void setSelectedClass(int classIndex);  // -1 = 全部
    void setMinConfidence(double minConf);
    void setNameFilter(const QString& filter);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    int selectedClass_ = -1;
    double minConfidence_ = 0.0;
    QString nameFilter_;
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
    static constexpr int kGap = 8;
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
                     const model::ClassificationResult& result);
    void onErrorOccurred(const model::ImageEntry& img, model::ErrorCode ec);
    void onFinished(const model::RunReport& report);
    void onGridClicked(const QModelIndex& index);
    void onFilterChanged();

private:
    void setupUi();
    QWidget* buildToolBar();
    QWidget* buildFilterPanel();
    QWidget* buildDetailPanel();
    QWidget* buildCentral();
    void setupStatusBar();
    void updateStatusBar(const model::RunReport& report);
    void showPreview(const ResultItem& item);
    void setUiEnabled(bool enabled);

    ChamferButton* selectBtn_ = nullptr;
    ChamferButton* startBtn_  = nullptr;
    ChamferButton* stopBtn_   = nullptr;
    QLabel* brandLabel_       = nullptr;
    QLabel* folderLabel_      = nullptr;

    ClassLegend* legend_      = nullptr;
    QSlider*     confSlider_  = nullptr;
    QLineEdit*   nameEdit_    = nullptr;

    QListView*          gridView_  = nullptr;
    ResultListModel*    gridModel_ = nullptr;
    ResultFilterProxy*  gridFilter_ = nullptr;

    ImagePreview*  imageView_       = nullptr;
    QLabel*        predictionLabel_ = nullptr;
    std::array<ScoreBar*, 6> scoreBars_{};
    std::array<int, 6> classCounts_{};

    QLabel*    statusTotal_   = nullptr;
    QLabel*    statusSuccess_ = nullptr;
    QLabel*    statusFailed_  = nullptr;
    QLabel*    statusAvg_     = nullptr;
    QLabel*    statusP95_     = nullptr;
    QProgressBar* progressBar_ = nullptr;

    std::unique_ptr<application::PipelineRunner> pipeline_;
    std::shared_ptr<RunBridge> bridge_;
    std::thread workerThread_;

    QString selectedFolder_;
};

}  // namespace ui
}  // namespace qf