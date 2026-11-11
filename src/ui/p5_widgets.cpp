#include "ui/p5_widgets.h"
#include "ui/p5_theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QtGlobal>

namespace qf {
namespace ui {

namespace {
constexpr qreal kCardChamfer = 8.0;   // 卡片斜切角
constexpr qreal kBtnChamfer  = 4.0;   // 按钮斜切角
constexpr qreal kBarChamfer  = 4.0;   // 得分条斜切角
}

// ============================================================
// ChamferButton
// ============================================================
ChamferButton::ChamferButton(const QString& text, bool primary, QWidget* parent)
    : QWidget(parent), text_(text), primary_(primary) {
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ChamferButton::setText(const QString& text) {
    text_ = text;
    updateGeometry();
    update();
}

QSize ChamferButton::sizeHint() const {
    QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(text_) + 36, 30);
}

void ChamferButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor fill, border;
    if (primary_) {
        if (!isEnabled()) {
            fill = QColor("#2A2A36");
            border = fill;
        } else if (pressed_) {
            fill = QColor(p5::kRedDeep);
            border = fill;
        } else if (hovered_) {
            fill = QColor(p5::kRedBright);
            border = fill;
        } else {
            fill = QColor(p5::kRed);
            border = fill;
        }
    } else {
        fill = pressed_ ? QColor("#2A2A36") : QColor(p5::kPanel);
        border = hovered_ ? QColor(p5::kRed) : QColor(p5::kBorder);
    }

    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path = chamferRect(r, kBtnChamfer);
    p.fillPath(path, fill);
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    p.setPen(isEnabled() ? QColor(p5::kText) : QColor("#5A5A6A"));
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.drawText(rect(), Qt::AlignCenter, text_);
}

void ChamferButton::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void ChamferButton::leaveEvent(QEvent* event) {
    hovered_ = false;
    pressed_ = false;
    update();
    QWidget::leaveEvent(event);
}

void ChamferButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressed_ = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void ChamferButton::mouseReleaseEvent(QMouseEvent* event) {
    bool wasPressed = pressed_;
    pressed_ = false;
    update();
    if (wasPressed && event->button() == Qt::LeftButton &&
        rect().contains(event->pos())) {
        emit clicked();
    }
    QWidget::mouseReleaseEvent(event);
}

// ============================================================
// ScoreBar
// ============================================================
ScoreBar::ScoreBar(model::DefectClass dc, QWidget* parent)
    : QWidget(parent), dc_(dc) {}

void ScoreBar::setScore(double score) {
    double v = qBound(0.0, score, 1.0);
    if (qFuzzyCompare(v, score_)) return;
    score_ = v;
    update();
}

void ScoreBar::setHighlight(bool on) {
    if (highlight_ == on) return;
    highlight_ = on;
    update();
}

QSize ScoreBar::sizeHint() const {
    return QSize(200, 26);
}

void ScoreBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor c = defectClassColor(dc_);
    if (!highlight_) c.setAlpha(128);

    // 左侧：中文名 + 缩写
    QFont nameFont = font();
    nameFont.setPointSize(9);
    nameFont.setBold(highlight_);
    p.setFont(nameFont);
    p.setPen(c);
    QRect nameRect(0, 0, 120, height());
    QString label = QString("%1 %2")
                        .arg(QString::fromUtf8(model::defectClassChinese(dc_)),
                             QString::fromUtf8(model::defectClassAbbr(dc_)));
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, label);

    // 中间：斜切胶囊条
    int valueX = width() - 44;
    QRect barRect(126, height() / 2 - 7, valueX - 126 - 8, 14);
    QPainterPath bg = chamferRect(QRectF(barRect), kBarChamfer);
    p.fillPath(bg, QColor("#2A2A34"));

    double ratio = qBound(0.0, score_, 1.0);
    int fillW = static_cast<int>(barRect.width() * ratio);
    if (fillW > 0) {
        QRect fillRect = barRect.adjusted(0, 0, -(barRect.width() - fillW), 0);
        QPainterPath fg = chamferRect(QRectF(fillRect), kBarChamfer);
        p.fillPath(fg, c);
    }

    // 右侧：数值（0~1）
    QFont vFont = font();
    vFont.setPointSize(9);
    vFont.setBold(highlight_);
    p.setFont(vFont);
    p.setPen(highlight_ ? QColor(p5::kText) : QColor(p5::kTextDim));
    QRect valRect(valueX, 0, 44, height());
    p.drawText(valRect, Qt::AlignRight | Qt::AlignVCenter,
               QString::number(score_, 'f', 2));
}

// ============================================================
// ClassLegend
// ============================================================
ClassLegend::ClassLegend(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
}

void ClassLegend::setSelectedIndex(int idx) {
    if (idx < -1 || idx >= static_cast<int>(model::AllDefectClasses.size()))
        idx = -1;
    if (selectedIndex_ == idx) return;
    selectedIndex_ = idx;
    update();
    emit selectionChanged(selectedIndex_);
}

void ClassLegend::setCount(int classIndex, int count) {
    if (classIndex < 0 || classIndex >= static_cast<int>(counts_.size())) return;
    counts_[classIndex] = count;
    update();
}

int ClassLegend::rowHeight() const {
    QFontMetrics fm(font());
    return qMax(30, fm.height() + 12);
}

QSize ClassLegend::sizeHint() const {
    return QSize(180, rowHeight() * static_cast<int>(model::AllDefectClasses.size()) + 8);
}

QRect ClassLegend::rowRect(int i) const {
    return QRect(0, i * rowHeight() + 4, width(), rowHeight());
}

int ClassLegend::rowAt(int y) const {
    int idx = (y - 4) / rowHeight();
    if (idx < 0 || idx >= static_cast<int>(model::AllDefectClasses.size())) return -1;
    return idx;
}

void ClassLegend::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    for (int i = 0; i < static_cast<int>(model::AllDefectClasses.size()); ++i) {
        auto dc = model::AllDefectClasses[i];
        QRect r = rowRect(i);
        bool sel = (selectedIndex_ == i);

        // 选中高亮底
        if (sel) {
            QRectF hl = QRectF(r).adjusted(2, 2, -2, 2);
            p.fillPath(chamferRect(hl, 4), QColor("#1C1C24"));
            p.setPen(QPen(defectClassColor(dc), 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(chamferRect(hl, 4));
        }

        // 色点（小斜切方块）
        QRect dot(r.left() + 12, r.center().y() - 5, 10, 10);
        p.fillPath(chamferRect(QRectF(dot), 2), defectClassColor(dc));

        // 中文名 + 缩写
        QFont f = font();
        f.setPointSize(9);
        f.setBold(sel);
        p.setFont(f);
        p.setPen(sel ? QColor(p5::kText) : QColor(p5::kTextDim));
        QString label = QString("%1 %2")
                            .arg(QString::fromUtf8(model::defectClassChinese(dc)),
                                 QString::fromUtf8(model::defectClassAbbr(dc)));
        p.drawText(QRect(r.left() + 30, r.top(), r.width() - 80, r.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, label);

        // 计数
        p.setPen(QColor(p5::kTextDim));
        p.drawText(QRect(r.right() - 44, r.top(), 32, r.height()),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(counts_[i]));
    }
}

void ClassLegend::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    int idx = rowAt(event->pos().y());
    if (idx < 0) return;
    setSelectedIndex(selectedIndex_ == idx ? -1 : idx);
}

// ============================================================
// ImagePreview
// ============================================================
ImagePreview::ImagePreview(QWidget* parent) : QWidget(parent) {
    setMinimumSize(160, 160);
}

void ImagePreview::setPixmap(const QPixmap& pm) {
    pixmap_ = pm;
    update();
}

void ImagePreview::clear() {
    pixmap_ = QPixmap();
    update();
}

void ImagePreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(p5::kBackground));
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (pixmap_.isNull()) {
        p.setPen(QColor(p5::kGray));
        p.drawText(rect(), Qt::AlignCenter, tr("无图像"));
        return;
    }

    QSize target = size() - QSize(20, 20);
    QPixmap pm = pixmap_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    int x = (width() - pm.width()) / 2;
    int y = (height() - pm.height()) / 2;

    // 斜切边框
    QRectF fr(x - 5, y - 5, pm.width() + 10, pm.height() + 10);
    p.setPen(QPen(QColor(p5::kBorder), 1));
    p.setBrush(QColor(p5::kPanel));
    p.drawPath(chamferRect(fr, kCardChamfer));

    p.drawPixmap(x, y, pm);
}

}  // namespace ui
}  // namespace qf