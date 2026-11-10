#pragma once

#include <QColor>
#include <QRectF>
#include <QPainterPath>
#include <QString>

#include "model/defect_class.h"

namespace qf {
namespace ui {

// ============================================================
// P5 基础色板（外壳 / chrome 专用）
// ============================================================
namespace p5 {
inline constexpr const char* kBackground = "#0A0A0D";  // 主背景（近纯黑）
inline constexpr const char* kPanel      = "#14141A";  // 面板/卡片背景
inline constexpr const char* kBorder     = "#3A3A44";  // 细描边
inline constexpr const char* kRed        = "#E60012";  // P5 血红（主强调）
inline constexpr const char* kRedDeep    = "#B3000F";  // 深血红（按压/渐变深端）
inline constexpr const char* kRedBright  = "#FF1A2C";  // 亮血红（悬浮）
inline constexpr const char* kText       = "#FFFFFF";  // 主文字
inline constexpr const char* kTextDim    = "#B8B8C0";  // 次要文字
inline constexpr const char* kGray       = "#6B6B78";  // 中性灰（无法判定）
inline constexpr const char* kSuccess    = "#2ED573";  // 成功绿
inline constexpr const char* kFail       = "#FF5C5C";  // 失败红
}  // namespace p5

// ============================================================
// 六类专属色板（数据层专用，与外壳 P5 血红刻意区分）
// ============================================================
inline QColor defectClassColor(model::DefectClass c) {
    switch (c) {
    case model::DefectClass::Crazing:       return QColor("#F5A623");  // 琥珀
    case model::DefectClass::Inclusion:     return QColor("#22D3EE");  // 青
    case model::DefectClass::Patches:       return QColor("#B26BFF");  // 紫
    case model::DefectClass::PittedSurface: return QColor("#FF3D81");  // 品红
    case model::DefectClass::Scratches:     return QColor("#3DDC97");  // 绿
    case model::DefectClass::RolledInScale: return QColor("#4F8CFF");  // 蓝
    }
    return QColor(p5::kGray);
}

inline QColor defectClassColor(int idx) {
    if (idx < 0 || idx >= static_cast<int>(model::AllDefectClasses.size()))
        return QColor(p5::kGray);
    return defectClassColor(model::AllDefectClasses[idx]);
}

// ============================================================
// 斜切矩形路径（四角切掉 c 像素，P5 标志性几何）
// ============================================================
inline QPainterPath chamferRect(const QRectF& r, qreal c) {
    QPainterPath p;
    p.moveTo(r.left() + c, r.top());
    p.lineTo(r.right() - c, r.top());
    p.lineTo(r.right(), r.top() + c);
    p.lineTo(r.right(), r.bottom() - c);
    p.lineTo(r.right() - c, r.bottom());
    p.lineTo(r.left() + c, r.bottom());
    p.lineTo(r.left(), r.bottom() - c);
    p.lineTo(r.left(), r.top() + c);
    p.closeSubpath();
    return p;
}

// ============================================================
// 全局 QSS（红黑主题，斜切角由 QPainter 补足）
// ============================================================
inline QString p5StyleSheet() {
    return QStringLiteral(R"(
QMainWindow { background: #0A0A0D; color: #FFFFFF;
              font-family: "Microsoft YaHei", "Segoe UI"; }

QWidget#topbar { background: #14141A; border-bottom: 1px solid #3A3A44; }
QWidget#panel  { background: #14141A; }

QLabel { background: transparent; color: #FFFFFF; }

QListView { background: #0A0A0D; border: none; }

QProgressBar { background: #2A2A34; border: 1px solid #3A3A44; text-align: center;
               color: #FFFFFF; }
QProgressBar::chunk { background: #E60012; }

QScrollBar:vertical { background: #0A0A0D; width: 10px; }
QScrollBar::handle:vertical { background: #3A3A44; min-height: 20px; }
QScrollBar::handle:vertical:hover { background: #E60012; }
QScrollBar:horizontal { background: #0A0A0D; height: 10px; }
QScrollBar::handle:horizontal { background: #3A3A44; min-width: 20px; }
QScrollBar::handle:horizontal:hover { background: #E60012; }

QSplitter { background: #0A0A0D; }
QSplitter::handle { background: #1C1C24; }
QSplitter::handle:hover { background: #E60012; }

QStatusBar { background: #14141A; color: #B8B8C0; border-top: 1px solid #3A3A44; }
)");
}

}  // namespace ui
}  // namespace qf