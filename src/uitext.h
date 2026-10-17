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
//  界面文案：改文案看 uitext.def
//
//  uitext.def 里一行一条文案；这个头文件把它们变成**函数**，
//  所以用法和以前的常量完全一样：`label->setText(kBtnStart)`。
//
//  为什么是函数而不是常量：文案要跟着当前界面语言变，而常量在
//  main() 之前就初始化完了——那时候还没读到 --lang，也没读到设置。
//  变成函数之后，每次取值都走一遍当前语言的译文表，
//  于是"先定语言、再建窗口"就够了，不需要 Qt 的 retranslateUi 那一套。
//
//  含 UI_TEXT_RAW 的是不翻译的（版本号、URL、日期格式……）。
// ============================================================================

#include <QString>
#include <QVector>

#include "i18n.h"

// ---------------------------------------------------------------- 应用信息
// 版本号在 CMakeLists.txt 的 project(... VERSION x.y.z) 里改，
// 不要在这里写死——这里只是把编译期传进来的宏兜个底。
#ifndef DISKTEST_VERSION
#  define DISKTEST_VERSION "0.0.0"
#endif

/// 编译时间：只在编译到这个头文件的那些 .cpp 重新编译时才会更新。
#define DISKTEST_BUILD_DATE __DATE__
#define DISKTEST_BUILD_TIME __TIME__

// 这份二进制是给哪个平台编的。显示在"关于"里的版本号后面 ——
// 现在同时会有 Windows / Linux-amd64 / Linux-arm64 几种包，
// 报问题、或者对着 release 页挑包的时候，一眼就能看出手上这份是什么。
//
// 用**编译期**宏而不是 QSysInfo 的运行时值：这里要回答的是"我这个包是什么"，
// 不是"CPU 现在跑在什么模式"。32 位包跑在 64 位系统上、x64 包跑在
// ARM Windows 的模拟层里，运行时值都会和实际的包对不上。
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_ARM_64)
#  define DISKTEST_ARCH "win-arm64"
#elif defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
#  define DISKTEST_ARCH "win64"
#elif defined(Q_OS_WIN)
#  define DISKTEST_ARCH "win32"
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_ARM_64)
#  define DISKTEST_ARCH "Linux-arm64"
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
#  define DISKTEST_ARCH "Linux-amd64"
#elif defined(Q_OS_LINUX)
#  define DISKTEST_ARCH "Linux-i386"
#else
#  define DISKTEST_ARCH "unknown"
#endif

namespace ui {

/// 一条可翻译文案：键（就是 uitext.def 里的常量名）+ 中文原文。
struct TextEntry
{
    const char *key;
    const char *source;
};

} // namespace ui

// UI_TEXT      会翻译：按当前语言查译文表，查不到退回中文原文
// UI_TEXT_RAW  不翻译：版本号、编译时间、URL、日期格式、文件名模式这类
//              必须逐字一致的东西；它们不进目录，也不要求翻译
#define UI_TEXT(name, source) \
    inline QString name() { return i18n::text(#name, source); }
#define UI_TEXT_RAW(name, value) \
    inline QString name() { return QStringLiteral(value); }

#include "uitext.def"

#undef UI_TEXT
#undef UI_TEXT_RAW

// ---------------------------------------------------------------- 平台别名
//
// 有 8 句话在 Windows 和 Linux 上写法不同（fio.exe 还是 fio、提权还是 root、
// 文件对话框过滤……）。译文表是按**键**查的，一个键只能有一条译文，
// 所以 .def 里把它们拆成 kXxx_Win / kXxx_Posix 两个键，这里按平台把真正的
// 名字接上——调用处照旧写 kXxx()，不用关心平台。
#ifdef Q_OS_WIN
inline QString kFioPathPlaceholder()  { return kFioPathPlaceholder_Win(); }
inline QString kIoModeTip()           { return kIoModeTip_Win(); }
inline QString kModeElevatedTip()     { return kModeElevatedTip_Win(); }
inline QString kModeNormalTip()       { return kModeNormalTip_Win(); }
inline QString kFilterExecutable()    { return kFilterExecutable_Win(); }
inline QString kErrSpecifyFioPath()   { return kErrSpecifyFioPath_Win(); }
inline QString kClFioPath()           { return kClFioPath_Win(); }
inline QString kClTarget()            { return kClTarget_Win(); }
#else
inline QString kFioPathPlaceholder()  { return kFioPathPlaceholder_Posix(); }
inline QString kIoModeTip()           { return kIoModeTip_Posix(); }
inline QString kModeElevatedTip()     { return kModeElevatedTip_Posix(); }
inline QString kModeNormalTip()       { return kModeNormalTip_Posix(); }
inline QString kFilterExecutable()    { return kFilterExecutable_Posix(); }
inline QString kErrSpecifyFioPath()   { return kErrSpecifyFioPath_Posix(); }
inline QString kClFioPath()           { return kClFioPath_Posix(); }
inline QString kClTarget()            { return kClTarget_Posix(); }
#endif

namespace ui {

/// 全部可翻译文案的目录（只有 UI_TEXT，不含 UI_TEXT_RAW）。
/// --i18n-check 用它算译文覆盖率和占位符对不对得上。
inline const QVector<TextEntry> &catalog()
{
    static const QVector<TextEntry> entries = [] {
        QVector<TextEntry> list;
        list.reserve(512);
#define UI_TEXT(name, source) list.append(TextEntry{#name, source});
#define UI_TEXT_RAW(name, value)
#include "uitext.def"
#undef UI_TEXT
#undef UI_TEXT_RAW
        return list;
    }();
    return entries;
}

} // namespace ui
