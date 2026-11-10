#pragma once

#include <QWidget>
#include <QColor>
#include <QPixmap>
#include <QString>
#include <array>

#include "model/defect_class.h"

class QEnterEvent;  // Qt6 enterEvent 参数类型（qwidget.h 已前向声明，此处冗余保险）

namespace qf {
namespace ui {

// ============================================================
// 斜切按钮（P5 主/次按钮，血红实底 / 描边两种）
// ============================================================
class ChamferButton : public QWidget {
    Q_OBJECT
public:
    explicit ChamferButton(const QString& text, bool primary, QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const { return text_; }

    QSize sizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QString text_;
    bool primary_;
    bool hovered_ = false;
    bool pressed_ = false;
};

// ============================================================
// 单条六类得分条（斜切胶囊 + 类别色，末端显示 0~1 数值）
// ============================================================
class ScoreBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double score READ score WRITE setScore)
public:
    explicit ScoreBar(model::DefectClass dc, QWidget* parent = nullptr);

    double score() const { return score_; }
    void setScore(double score);
    void setHighlight(bool on);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    model::DefectClass dc_;
    double score_ = 0.0;
    bool highlight_ = false;
};

// ============================================================
// 六类图例（单选："只看某类"，点击整行切换，再点取消）
// ============================================================
class ClassLegend : public QWidget {
    Q_OBJECT
public:
    explicit ClassLegend(QWidget* parent = nullptr);

    int selectedIndex() const { return selectedIndex_; }
    void setSelectedIndex(int idx);   // -1 = 显示全部
    void setCount(int classIndex, int count);

    QSize sizeHint() const override;

signals:
    void selectionChanged(int selectedIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    int rowHeight() const;
    QRect rowRect(int i) const;
    int rowAt(int y) const;

    int selectedIndex_ = -1;
    std::array<int, 6> counts_{};
};

// ============================================================
// 斜切边框的图像预览（简化自 QGraphicsView，纯绘制）
// ============================================================
class ImagePreview : public QWidget {
    Q_OBJECT
public:
    explicit ImagePreview(QWidget* parent = nullptr);

    void setPixmap(const QPixmap& pm);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap pixmap_;
};

}  // namespace ui
}  // namespace qf