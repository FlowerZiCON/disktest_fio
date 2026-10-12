// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MOONLIGHT
//
// 本程序是自由软件：你可以按自由软件基金会发布的 GNU 通用公共许可证
// （第 3 版或你选择的任何更高版本）条款重新分发和/或修改它。
//
// 本程序按"希望有用"分发，但**没有任何担保**，甚至不含适销性或特定用途
// 适用性的默示担保。详见 GNU 通用公共许可证。
//
// 你应当已随本程序收到一份 GNU 通用公共许可证副本（根目录 LICENSE）。
// 如果没有，见 <https://www.gnu.org/licenses/>。

#include "perfcell.h"

#include "uitext.h"

#include <QFont>
#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainterPath>

namespace {
const QColor kBarTop(0x9d, 0xe0, 0x6a);      // 绿色渐变的上端
const QColor kBarBottom(0x6c, 0xc9, 0x3a);   // 下端
const QColor kBorder(0x9a, 0x9a, 0x9a);
const QColor kText(0x1a, 0x1a, 0x1a);
} // namespace

PerfCell::PerfCell(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(42);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

QSize PerfCell::sizeHint() const
{
    return QSize(120, 44);
}

QSize PerfCell::minimumSizeHint() const
{
    return QSize(80, 42);
}

void PerfCell::setValue(double mbps)
{
    m_value = mbps;
    update();
}

void PerfCell::setScaleMax(double max)
{
    m_scaleMax = qMax(1.0, max);
    update();
}

void PerfCell::clear()
{
    m_value = -1.0;
    m_scaleMax = 1.0;
    update();
}

void PerfCell::setClickable(bool clickable)
{
    m_clickable = clickable;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(clickable ? kTraceClickTip : kTraceBusyTip);
}

void PerfCell::mousePressEvent(QMouseEvent *event)
{
    if (m_clickable && event->button() == Qt::LeftButton) {
        emit activated();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PerfCell::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // 白底
    painter.setPen(QPen(kBorder, 1.0));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(box, 3.0, 3.0);

    // 绿色条：按 值/满量程 的比例
    if (m_value > 0.0) {
        const double ratio = qBound(0.0, m_value / m_scaleMax, 1.0);
        QRectF bar = box.adjusted(1.0, 1.0, -1.0, -1.0);
        bar.setWidth(qMax(2.0, bar.width() * ratio));

        QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
        gradient.setColorAt(0.0, kBarTop);
        gradient.setColorAt(1.0, kBarBottom);

        QPainterPath path;
        path.addRoundedRect(bar, 3.0, 3.0);
        painter.fillPath(path, gradient);
    }

    // 数字：大号加粗，右对齐
    QFont font = painter.font();
    font.setPointSizeF(qMax(13.0, font.pointSizeF() + 7.0));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(kText);

    const QString text = m_value >= 0.0
                             ? QString::number(m_value, 'f', 2)
                             : QStringLiteral("--");
    painter.drawText(box.adjusted(6.0, 0.0, -10.0, 0.0),
                     Qt::AlignRight | Qt::AlignVCenter, text);
}
