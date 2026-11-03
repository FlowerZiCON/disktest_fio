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

#include "theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QPalette>
#include <QStyleHints>

namespace theme {

ThemeNotifier *ThemeNotifier::instance()
{
    static ThemeNotifier notifier;
    return &notifier;
}

bool isDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Windows 11 / macOS / Linux 上 Qt 会把系统的深浅色报在这里。
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark)
        return true;
    if (scheme == Qt::ColorScheme::Light)
        return false;
#endif
    // Qt 6.2 或系统没给出明确答案时，按窗口底色明度兜底：
    // 深色主题的 Window 一定是暗的，浅色一定是亮的。
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString appStyleSheet()
{
    if (!isDark())
        return QString();

    // 这段样式表只做深色下的“兜底”，不改变浅色外观。
    //
    // 起因：Qt 6.8 的 Windows 11 原生样式在深色系统上仍把 QPushButton 和
    // QTreeView/表头画成浅色，而它们的 palette 明明是深色。给这些控件显式
    // 指定底色/文字色后，绘制就不再依赖原生样式的内部判断。
    //
    // 颜色与 Qt 深色调色板保持一致：按钮 #3c3c3c、窗口 #1e1e1e、
    // 列表底 #2d2d2d、边框 #5a5a5a、选中蓝 #1f5c99。
    //
    // 只在 Windows 上覆盖：这个白底是 Windows 11 原生样式的问题；
    // Linux / macOS 的原生深色样式本身会跟调色板走，强行覆盖反而会
    // 和系统主题的灰度对不上。
#ifdef Q_OS_WIN
    return QStringLiteral(
        "QPushButton {"
        "  background-color:#3c3c3c; color:#ffffff;"
        "  border:1px solid #5a5a5a; border-radius:5px; padding:4px 12px;"
        "}"
        "QPushButton:hover { background-color:#4a4a4a; }"
        "QPushButton:pressed { background-color:#2f2f2f; }"
        "QPushButton:checked { background-color:#1f5c99; border-color:#2f7fd0; }"
        "QPushButton:focus { border-color:#2f7fd0; }"
        "QPushButton:disabled {"
        "  background-color:#303030; color:#8a8a8a; border-color:#3f3f3f;"
        "}"
        "QToolButton { background-color:transparent; color:#ffffff; }"
        "QTreeWidget, QTreeView {"
        "  background-color:#2d2d2d; alternate-background-color:#343434; color:#ffffff;"
        "}"
        "QTreeWidget::item:selected, QTreeView::item:selected {"
        "  background:#1f5c99; color:#ffffff;"
        "}"
        "QTableWidget, QTableView {"
        "  background-color:#2d2d2d; alternate-background-color:#343434; color:#ffffff;"
        "  gridline-color:#4a4a4a; selection-background-color:#1f5c99;"
        "  selection-color:#ffffff;"
        "}"
        "QHeaderView::section {"
        "  background-color:#333333; color:#e8e8e8; border:0;"
        "  border-right:1px solid #4a4a4a; border-bottom:1px solid #4a4a4a;"
        "  padding:4px;"
        "}"
        "QTableCornerButton::section {"
        "  background-color:#333333; border:0;"
        "  border-right:1px solid #4a4a4a; border-bottom:1px solid #4a4a4a;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color:#2d2d2d; color:#ffffff;"
        "  selection-background-color:#1f5c99; selection-color:#ffffff;"
        "  border:1px solid #5a5a5a;"
        "}"
        "QToolTip {"
        "  background-color:#2b2b2b; color:#f0f0f0; border:1px solid #5a5a5a;"
        "}");
#else
    return QString();
#endif
}

void apply(QApplication *app)
{
    if (!app)
        return;

    // 只在第一次调用时挂监听。之后系统每切一次深浅色，Qt 会发
    // colorSchemeChanged；这里先把旧样式表整个撤掉，让 QStyleSheetStyle
    // 缓存的旧调色板释放，再按新主题装回去（浅色时就是空串）。
    //
    // 只改样式表还不够：窗口里许多文字色是 setStyleSheet 写死在控件上的，
    // 所以换完样式表后广播 ThemeNotifier::changed()，由各窗口重新套色。
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                         ThemeNotifier::instance(), [](Qt::ColorScheme) {
            auto *application =
                qobject_cast<QApplication *>(QCoreApplication::instance());
            if (!application)
                return;
            application->setStyleSheet(QString());
            application->setStyleSheet(appStyleSheet());
            emit ThemeNotifier::instance()->changed();
        });
#endif
    }

    app->setStyleSheet(appStyleSheet());
}

void restyleLabels(QWidget *root)
{
    if (!root)
        return;

    QList<QLabel *> labels = root->findChildren<QLabel *>();
    if (auto *self = qobject_cast<QLabel *>(root))
        labels.prepend(self);

    for (QLabel *label : labels) {
        const QString tone = label->property("themeTone").toString();
        if (tone.isEmpty())
            continue;
        const QColor color = toneColor(tone);
        if (!color.isValid())
            continue;
        const QString extra = label->property("themeExtra").toString();
        label->setStyleSheet(QStringLiteral("color:%1;%2").arg(color.name(), extra));
    }
}

QColor toneColor(const QString &tone)
{
    if (tone == QLatin1String("muted"))
        return mutedText();
    if (tone == QLatin1String("secondary"))
        return secondaryText();
    if (tone == QLatin1String("success"))
        return successText();
    if (tone == QLatin1String("warning"))
        return warningText();
    if (tone == QLatin1String("danger"))
        return dangerText();
    if (tone == QLatin1String("read"))
        return readValue();
    if (tone == QLatin1String("write"))
        return writeValue();
    if (tone == QLatin1String("neutral"))
        return neutralValue();
    if (tone == QLatin1String("bannerNote"))
        return bannerNoteText();
    return QColor();
}

QString driveTreeStyleSheet()
{
    if (isDark()) {
        // 深色下原来的浅蓝选中底会亮得刺眼，换成系统高亮蓝 + 白字。
        return QStringLiteral(
            "QTreeWidget::item:selected { background:#1f5c99; color:#ffffff; }"
            "QTreeWidget::item:selected:active { background:#2a6fb5; color:#ffffff; }");
    }
    return QStringLiteral(
        "QTreeWidget::item:selected { background:#cce8ff; color:#10314f; }"
        "QTreeWidget::item:selected:active { background:#b6dcff; color:#10314f; }");
}

QColor mutedText()
{
    return isDark() ? QColor(0xa0, 0xa0, 0xa0) : QColor(0x66, 0x66, 0x66);
}

QColor secondaryText()
{
    return isDark() ? QColor(0xd6, 0xd6, 0xd6) : QColor(0x33, 0x33, 0x33);
}

QColor successText()
{
    return isDark() ? QColor(0x4e, 0xc7, 0x77) : QColor(0x1a, 0x7f, 0x37);
}

QColor warningText()
{
    return isDark() ? QColor(0xe0, 0xb3, 0x41) : QColor(0x8a, 0x6d, 0x00);
}

QColor dangerText()
{
    return isDark() ? QColor(0xff, 0x6b, 0x6b) : QColor(0xd3, 0x20, 0x20);
}

QColor readValue()
{
    return isDark() ? QColor(0xff, 0x6b, 0x6b) : QColor(0xdc, 0x1e, 0x1e);
}

QColor writeValue()
{
    return isDark() ? QColor(0x6e, 0xa8, 0xff) : QColor(0x1e, 0x46, 0xdc);
}

QColor neutralValue()
{
    return isDark() ? QColor(0xd6, 0xd6, 0xd6) : QColor(0x3c, 0x3c, 0x3c);
}

QColor bannerBackground()
{
    return isDark() ? QColor(0x3a, 0x17, 0x17) : QColor(0xff, 0xf2, 0xf0);
}

QColor bannerNoteText()
{
    return isDark() ? QColor(0xff, 0xb4, 0xb4) : QColor(0x8a, 0x10, 0x10);
}

QColor progressTrack()
{
    return isDark() ? QColor(0x3c, 0x3c, 0x3c) : QColor(0xec, 0xec, 0xec);
}

QColor progressBorder()
{
    return isDark() ? QColor(0x5a, 0x5a, 0x5a) : QColor(0xb0, 0xb0, 0xb0);
}

QColor usageTextColor(double percent)
{
    if (percent >= 95.0)
        return isDark() ? QColor(0xff, 0x7b, 0x72) : QColor(0xc8, 0x28, 0x28);
    if (percent >= 85.0)
        return isDark() ? QColor(0xf0, 0xa7, 0x5a) : QColor(0xb5, 0x75, 0x0f);
    if (percent >= 70.0)
        return isDark() ? QColor(0xe8, 0xc3, 0x3a) : QColor(0x8a, 0x6d, 0x00);
    return isDark() ? QColor(0x5f, 0xd4, 0x7a) : QColor(0x2c, 0x7a, 0x37);
}

QString usageChunkColor(double percent)
{
    if (percent >= 95.0)
        return QStringLiteral("#d9483c");
    if (percent >= 85.0)
        return QStringLiteral("#e8933a");
    if (percent >= 70.0)
        return QStringLiteral("#e8c33a");
    return QStringLiteral("#3fa34d");
}

QColor unavailableText()
{
    return isDark() ? QColor(0x8a, 0x8a, 0x8a) : QColor(0x96, 0x96, 0x96);
}

QColor usageCriticalText()
{
    return isDark() ? QColor(0xff, 0x7b, 0x72) : QColor(0xc8, 0x28, 0x28);
}

QColor usageWarningText()
{
    return isDark() ? QColor(0xf0, 0xa7, 0x5a) : QColor(0xc8, 0x82, 0x14);
}

} // namespace theme
