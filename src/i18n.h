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

// ============================================================================
//  界面语言（简体中文 / 繁體中文 / English）
//
//  译文不是编进代码的：每一条界面文案在 src/uitext.def 里带一个**常量名**
//  （kBtnStart 之类），译文放在 resources/i18n/en.tsv、zh_TW.tsv 里，
//  一行一条：`kBtnStart<TAB>Start Test`。查不到的键退回 .def 里的中文原文，
//  所以少翻一条不会把界面变成空白。
//
//  语言在**建第一个窗口之前**定下来（见 main.cpp）：界面文案是搭窗口时取的，
//  换语言靠"存下选择 + 用 --lang 重新启动"，而不是原地重译控件——
//  后者要把整个窗口拆了重建，运行中的测试会跟着一起丢。
// ============================================================================

#include <QString>
#include <QStringList>

namespace i18n {

enum class Language {
    SimplifiedChinese,   ///< 简体中文（源码里的原文就是它，不需要译文表）
    TraditionalChinese,  ///< 繁體中文（zh_TW）
    English,             ///< English
};

/// 当前界面语言。
Language current();
/// 设定当前界面语言。**只在建窗口之前调用有意义**。
void setCurrent(Language language);

/// 解析语言标记："zh_CN" / "zh-Hans" / "cn" / "zh_TW" / "zh-Hant" / "tw" / "en" / "en_US"。
/// 认得出来返回 true 并写进 *out；认不出来返回 false（不修改 *out）。
bool languageFromTag(const QString &tag, Language *out);
/// 语言的规范标记（写进设置、传给 --lang 用）："zh_CN" / "zh_TW" / "en"。
QString tag(Language language);
/// 语言自己的名字（永远用该语言书写）："简体中文" / "繁體中文" / "English"。
/// 菜单里用它，这样界面语言看不懂时也能找到自己的语言。
QString nativeName(Language language);

/// 按系统 UI 语言猜一个：中文按简繁分流，其它一律英文。
Language systemLanguage();

/// 查一条译文。查不到（或当前就是简体中文）返回 sourceUtf8 原文。
QString text(const char *key, const char *sourceUtf8);

/// 从设置里读上次选的语言标记（没存过返回空串）。
QString savedChoice();
/// 把语言存进设置（只存这一个键——测试参数依然每次启动回默认值）。
///
/// 存在 **exe 旁边的 `disktest.ini`**：安装包装出来的版本能写（安装脚本给
/// `{app}` 加了 users-modify，同一处权限也是「保存测试数据」把 CSV 存到程序目录
/// 的前提），绿色版放在用户自己的目录里也能写，整个文件夹拷走设置跟着走。
/// 只有那个目录**真写不进去**时（绿色版手工解压到 Program Files、Linux 装到
/// /usr/bin 之类）才退回用户目录（%APPDATA%\disktest\ 或 ~/.config/...）。
void saveChoice(Language language);
/// 排障用：语言设置实际落在哪个文件、那里能不能写（--i18n-check 会打出来）。
QString settingsDescription();

/// 译文的加载状态：空 = 还没找到文件，用中文原文顶着。
/// 只给 --i18n-check 和启动时的自检报告用。
struct LoadReport
{
    QStringList loadedFiles;   ///< 实际读到的文件（含来源标记）
    QStringList warnings;      ///< 解析时发现的问题（重复键、格式不对、读不到文件）
};
const LoadReport &loadReport();

/// --i18n-check：检查译文覆盖率、多余的键、占位符是否和中文原文对得上。
/// 通过返回 true；报告（含每一条问题）写进 *report。
bool check(QString *report);

} // namespace i18n
