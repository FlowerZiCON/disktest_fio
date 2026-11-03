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

#pragma once

#include <QColor>
#include <QObject>
#include <QString>

class QApplication;
class QWidget;

/// 深色 / 浅色主题的**唯一**判断入口与配色表。
///
/// 为什么要有这一层：界面里不少文字/边框是直接写死的浅色系（#333、#666、
/// #ececec……），深色下不是看不清就是刺眼；而 Qt 6.8 的 Windows 11 原生样式
/// 在深色模式下仍会把 QPushButton / QTreeView 画成浅色底。这里集中判断
/// 当前是不是深色，并给出对应的颜色与少量样式表，避免每个控件各猜一套。
namespace theme {

/// 系统深浅色发生切换时发信号。窗口收到后把自己的显式颜色重新套一遍。
class ThemeNotifier : public QObject
{
    Q_OBJECT
public:
    static ThemeNotifier *instance();

signals:
    void changed();
};

/// 当前系统是否为深色。Windows 11 上跟随「设置 → 个性化 → 颜色」；
/// Linux 上按应用调色板的明度兜底（Qt 6.2 没有 colorScheme API）。
bool isDark();

/// 应用启动时调用一次：深色下装一份应用级样式表，专门补 QPushButton、
/// 驱动器列表、表头这些 Qt 原生样式没有跟随深色的控件。浅色下不做任何事，
/// 保持原来的原生外观。必须在建窗口之前调用，之后创建的对话框也会生效。
///
/// 同时会监听系统的深浅色切换：切换时换样式表，并让所有窗口重刷显式颜色。
void apply(QApplication *app);

/// 深色下用的应用级样式表；浅色返回空串。
QString appStyleSheet();

/// 驱动器列表（两层树）的选中色。浅色是原来的浅蓝，深色换成深蓝底白字。
QString driveTreeStyleSheet();

/// 给控件打上 `themeTone` 动态属性后，切换主题时用这个函数统一重刷文字色。
/// 属性 `themeExtra` 里可以带额外样式（例如 `font-weight:bold;`）。
void restyleLabels(QWidget *root);

/// 把 tone 名字映射成当前主题下的颜色；未知 tone 返回无效颜色。
QColor toneColor(const QString &tone);

// ---- 文字 ----
QColor mutedText();       ///< 次要说明（原 #666）
QColor secondaryText();   ///< 普通正文（原 #333）
QColor successText();     ///< 成功 / 管理员模式（原 #1a7f37）
QColor warningText();     ///< 警告（原 #8a6d00）
QColor dangerText();      ///< 错误 / 校验失败（原 #d32020）

// ---- 卡片 ----
QColor readValue();       ///< 读取速度（红）
QColor writeValue();      ///< 写入速度（蓝）
QColor neutralValue();    ///< 已用时 / 状态（灰）

// ---- 告警条 ----
QColor bannerBackground();  ///< 校验失败告警条的底色
QColor bannerNoteText();    ///< 告警条第二行说明文字

// ---- 磁盘占用条 ----
QColor progressTrack();     ///< 未填充部分
QColor progressBorder();
QColor usageTextColor(double percent);
QString usageChunkColor(double percent);

// ---- 驱动器列表里的强调色 ----
QColor unavailableText();     ///< 测不了的分区（整行灰掉）
QColor usageCriticalText();   ///< 使用量 ≥ 95%
QColor usageWarningText();    ///< 使用量 ≥ 85%

} // namespace theme
