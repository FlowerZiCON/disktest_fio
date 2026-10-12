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
//  界面文案集中定义
//
//  改文案只改这一个文件，不用去 .cpp 里翻。
//  命名规则: k<区域><用途>，例如 kBtnStart 是"开始测试"按钮的文字。
//  含 %1 %2 的是 QString::arg() 占位符，改动时注意别把占位符删掉。
// ============================================================================

#include <QString>

// ---------------------------------------------------------------- 应用信息
// 版本号在 CMakeLists.txt 的 project(... VERSION x.y.z) 里改，
// 不要在这里写死——这里只是把编译期传进来的宏兜个底。
#ifndef QTFIOGUI_VERSION
#  define QTFIOGUI_VERSION "0.0.0"
#endif

/// 编译时间：只在 mainwindow.cpp 重新编译时才会更新。
#define QTFIOGUI_BUILD_DATE __DATE__
#define QTFIOGUI_BUILD_TIME __TIME__

inline const QString kAppName    = QStringLiteral("磁盘测试By MOONLIGHT");
inline const QString kAppVersion = QStringLiteral(QTFIOGUI_VERSION);
inline const QString kBuildTime  = QStringLiteral(QTFIOGUI_BUILD_DATE " " QTFIOGUI_BUILD_TIME);

/// —— 下面三项请改成你自己的信息 ——
inline const QString kAuthor  = QStringLiteral("MOONLIGHT");
inline const QString kContact = QStringLiteral("3607253395");

// ── 菜单 ──
inline const QString kMenuFile            = QStringLiteral("文件(&F)");
inline const QString kMenuOpenTestDir     = QStringLiteral("打开测试目录(&O)");
inline const QString kMenuCleanupFiles    = QStringLiteral("清理测试文件(&C)…");
inline const QString kMenuSaveData        = QStringLiteral("保存测试数据(&V)…");
inline const QString kMenuSaveDataTip     = QStringLiteral("把本次运行的参数和逐点采样数据存成 CSV");
inline const QString kMenuLoadData        = QStringLiteral("打开测试结果(&L)…");
inline const QString kMenuLoadDataTip     = QStringLiteral("在独立窗口里查看之前保存的测试结果（按数据自身的模式显示）");
inline const QString kMenuSaveDiagnostics = QStringLiteral("保存诊断输出(&S)…");
inline const QString kMenuQuit            = QStringLiteral("退出(&Q)");
inline const QString kMenuHelp            = QStringLiteral("帮助(&H)");
inline const QString kMenuAbout           = QStringLiteral("关于(&A)");

// ── 驱动器列表 ──
inline const QString kDriveTypeRemovable      = QStringLiteral("可移动磁盘");
inline const QString kDriveTypeFixed          = QStringLiteral("本地磁盘");
inline const QString kDriveTypeRemote         = QStringLiteral("网络驱动器");
inline const QString kDriveTypeCdrom          = QStringLiteral("光盘驱动器");
inline const QString kDriveTypeRamdisk        = QStringLiteral("内存盘");
inline const QString kDriveTypeUnknown        = QStringLiteral("未知设备");
inline const QString kVolumeRowLine           = QStringLiteral("%1：%2  %3/%4  使用量 %5%");
inline const QString kDriveTypeVolume         = QStringLiteral("卷");
inline const QString kGroupTargetDrive        = QStringLiteral("选择磁盘");
inline const QString kColLetter               = QStringLiteral("盘符");
inline const QString kColDevice               = QStringLiteral("设备名称");
inline const QString kColUsage                = QStringLiteral("使用量");
inline const QString kColUsedTotal            = QStringLiteral("已用 / 总空间");
inline const QString kDriveHintClick          = QStringLiteral("单击选中目标磁盘");
inline const QString kBtnRefresh              = QStringLiteral("刷新");
inline const QString kLabelTargetDriveColon   = QStringLiteral("目标磁盘：");
inline const QString kVolumeSummaryLine       = QStringLiteral("%1  %2  已用 %3%");
inline const QString kVolumeLabelFmt          = QStringLiteral("卷标: %1");
inline const QString kVolumeFsFmt             = QStringLiteral("文件系统: %1");
inline const QString kVolumeTypeFmt           = QStringLiteral("类型: %1");
inline const QString kSelfTestTargetMissing   = QStringLiteral("自检指定的目标分区 %1 不存在，已中止");
inline const QString kTargetRootChanged       = QStringLiteral("原来的目标分区 %1 已不存在，已改为 %2");
inline const QString kTargetNotSelected       = QStringLiteral("还没有选中目标分区。");
inline const QString kDiskSpaceUnselected     = QStringLiteral("未选择分区");
inline const QString kTargetFolderFmt         = QStringLiteral("目标：%1");
inline const QString kTargetPlanFmt           = QStringLiteral("\n将写入：%1 个文件 × %2（合计 %3）");
inline const QString kTargetNoSpaceForPlan    = QStringLiteral("\n剩余空间不足");
inline const QString kTargetSelfTestFillLimit = QStringLiteral("\n[自检] 填充上限 %1 MiB");
inline const QString kDiskSpaceAvailable      = QStringLiteral("可用 %1 / 共 %2");
inline const QString kDiskBarUsedPercent      = QStringLiteral("已用 %1%");

// ── 测试参数 ──
inline const QString kModeSeq            = QStringLiteral("顺序读写");
inline const QString kModeRand           = QStringLiteral("随机读写");
inline const QString kConfigDescribeFmt  = QStringLiteral("%1  %2 直接IO  bs=%3  iodepth=%4  校验=%5");
inline const QString kVerifyOn           = QStringLiteral("开");
inline const QString kVerifyOff          = QStringLiteral("关");
inline const QString kGroupFioEngine     = QStringLiteral("fio 引擎");
inline const QString kFioPathPlaceholder = QStringLiteral("fio.exe 的完整路径");
inline const QString kBtnBrowse          = QStringLiteral("浏览…");
inline const QString kFioEngineNotFound  = QStringLiteral("引擎: 未找到");
inline const QString kFioEngineVersion   = QStringLiteral("引擎: %1（%2）");
inline const QString kFioBundled        = QStringLiteral("自带");
inline const QString kFioFromSystem     = QStringLiteral("系统中");
inline const QString kGroupTestParams    = QStringLiteral("测试参数");
inline const QString kModeItemSeq        = QStringLiteral("顺序读写");
inline const QString kModeItemRand       = QStringLiteral("随机读写");
inline const QString kLabelRwMode        = QStringLiteral("读写模式");
inline const QString kLabelBlockSize     = QStringLiteral("块大小");
inline const QString kLabelIodepth       = QStringLiteral("队列深度");
inline const QString kIoModeDirectFmt    = QStringLiteral("%1 + 直接 I/O（绕过系统缓存）");
inline const QString kLabelRounds        = QStringLiteral("测试圈数");
inline const QString kRoundsSuffix       = QStringLiteral(" 圈");
inline const QString kRoundsTip          =
    QStringLiteral("整轮测试重复几遍。\n"
                   "每一圈都会重新删除测试文件、重新按当时的剩余空间规划，\n"
                   "相当于把整轮测试重跑一遍。\n"
                   "只勾选「读取」时不删文件，只是把校验重复几遍。");

// ── 运行日志（左侧下方） ──
inline const QString kGroupRunLog        = QStringLiteral("运行日志");
inline const QString kLogPlaceholder     = QStringLiteral("这里会按时间顺序记录每一圈、每个阶段做了什么；出错用红色显示。");
inline const QString kLogRoundsPlanned   = QStringLiteral("计划测试 %1 圈");
inline const QString kLogRoundStart      = QStringLiteral("第 %1/%2 圈开始");
inline const QString kLogRoundStartOne   = QStringLiteral("开始测试");
inline const QString kLogPhaseWrite      = QStringLiteral("开始写入");
inline const QString kLogPhaseRead       = QStringLiteral("开始读取");
inline const QString kLogStopRequested   = QStringLiteral("用户点击了停止");
inline const QString kLogCleanedOldFiles = QStringLiteral("已删除旧的测试文件 %1 个");
inline const QString kLogRoundCleanNext  = QStringLiteral("已删除上一圈的 %1 个测试文件，按当前剩余空间重新规划");
inline const QString kLogRoundSummary    = QStringLiteral("第 %1 圈结束：写入均速 %2，读取均速 %3");
inline const QString kLogRoundSummaryBad = QStringLiteral("第 %1 圈结束：发现异常（%2）");
inline const QString kLogAllRoundsDone   = QStringLiteral("全部 %1 圈测试结束");
inline const QString kLogOneRoundDone    = QStringLiteral("测试结束");
inline const QString kLogRunConclusion   = QStringLiteral("结论：%1");
inline const QString kLogErrorPrefix     = QStringLiteral("错误：%1");
inline const QString kLogWarnPrefix      = QStringLiteral("警告：%1");
inline const QString kIoModeTip          =
    QStringLiteral("IO 引擎固定为 windowsaio：Windows 上只有它支持 direct=1。\n"
                   "同步引擎（psync / sync）只能用缓冲 I/O，测到的是系统缓存而不是磁盘，故不提供。");
inline const QString kLabelIoMode        = QStringLiteral("IO 方式");
inline const QString kParamHint          =
    QStringLiteral("测试会在目标分区的 ml_disktest 目录里生成 testfile_* 文件，"
                   "每个不超过 2 GiB。测试文件不会自动删除，"
                   "可以从「文件 → 清理测试文件」删掉。");
inline const QString kBlockSizeSeq       = QStringLiteral("4 MiB");
inline const QString kBlockSizeRand      = QStringLiteral("4 KiB");

// ── 测试阶段 ──
inline const QString kPhaseRead        = QStringLiteral("读取阶段");
inline const QString kPhaseWrite       = QStringLiteral("写入阶段");
inline const QString kPhasePerf        = QStringLiteral("性能测试项");
inline const QString kGroupPhases      = QStringLiteral("测试阶段");
inline const QString kCheckWrite       = QStringLiteral("写入");
inline const QString kCheckRead        = QStringLiteral("读取");
inline const QString kCheckVerify      = QStringLiteral("校验");
inline const QString kBtnStart         = QStringLiteral("开始测试");
inline const QString kBtnStop          = QStringLiteral("停止");
inline const QString kReadNoFilesTip   =
    QStringLiteral("%1 里还没有测试文件。\n勾选「写入」或先跑一次写入，才能执行读取。");
inline const QString kReadAvailableTip = QStringLiteral("%1 里已有测试文件，可以只执行读取。");
inline const QString kReadNeedWriteTip = QStringLiteral("写入阶段结束后会生成测试文件，随后可以读回校验。");

// ── 状态与卡片 ──
inline const QString kCardReadNow    = QStringLiteral("当前读取");
inline const QString kCardWriteNow   = QStringLiteral("当前写入");
inline const QString kCardReadAvg    = QStringLiteral("平均读取");
inline const QString kCardWriteAvg   = QStringLiteral("平均写入");
inline const QString kCardElapsed    = QStringLiteral("已用时");
inline const QString kCardState      = QStringLiteral("状态");
inline const QString kStatePreparing = QStringLiteral("准备中");
inline const QString kStateWriting   = QStringLiteral("正在写入");
inline const QString kStateReading   = QStringLiteral("正在读取");
inline const QString kStateStopped   = QStringLiteral("已停止");

// ── 图表 ──
inline const QString kGroupChart     = QStringLiteral("实时速度");
inline const QString kLabelPlotStyle = QStringLiteral("绘图方式：");
inline const QString kPlotLine       = QStringLiteral("折线图");
inline const QString kPlotScatter    = QStringLiteral("散点图");
inline const QString kLabelDensity   = QStringLiteral("密度：");
inline const QString kDensityHigh    = QStringLiteral("高密度");
inline const QString kDensityLow     = QStringLiteral("低密度");
inline const QString kDensityLowTip  =
    QStringLiteral("折线图按数据量自动抽稀：\n"
                   "  < 128 GiB → 每 3 个点画 1 个\n"
                   "  128 GiB ~ 1 TiB → 每 5 个点画 1 个\n"
                   "  ≥ 1 TiB → 每 10 个点画 1 个\n"
                   "散点图和保存的 CSV 始终是全量数据。");
inline const QString kDensityHighTip = QStringLiteral("折线图绘制全部采样点");
inline const QString kSeriesRead     = QStringLiteral("读取速度");
inline const QString kSeriesWrite    = QStringLiteral("写入速度");
inline const QString kAxisCapacity   = QStringLiteral("容量（GiB）");
inline const QString kAxisSpeed      = QStringLiteral("速度（MiB/s）");

// ── 状态栏 ──
inline const QString kModeElevated           = QStringLiteral("提权模式");
inline const QString kModeNormal             = QStringLiteral("普通模式");
inline const QString kModeElevatedTip        = QStringLiteral("当前以管理员身份运行，可以访问需要提权的设备。");
inline const QString kModeNormalTip          = QStringLiteral("当前是普通权限。\n以管理员身份重新启动本程序即可进入提权模式。");
inline const QString kStatusSelectDriveHint  = QStringLiteral("请先在「选择磁盘」列表里选中要测试的分区");
inline const QString kStatusCleanedFiles     = QStringLiteral("已清理 %1 个测试文件");
inline const QString kStatusRunDataSaved     = QStringLiteral("已保存 %1 个采样点与本次参数到 %2");
inline const QString kStatusSamplesLoaded    = QStringLiteral("已加载 %1 个采样点");
inline const QString kStatusSkippedLines     = QStringLiteral("（跳过 %1 行无法解析的数据）");
inline const QString kStatusDiagnosticsSaved = QStringLiteral("诊断输出已保存到 %1");
inline const QString kStatusCleaningOldFiles = QStringLiteral("正在清理旧的测试文件…");
inline const QString kStatusWriting          = QStringLiteral("正在写入…");
inline const QString kStatusReading          = QStringLiteral("正在读取…");
inline const QString kStatusStopping         = QStringLiteral("正在停止…");

// ── 对话框与提示 ──
inline const QString kDlgChooseFioTitle       = QStringLiteral("选择 fio 可执行文件");
inline const QString kFilterExecutable        = QStringLiteral("可执行文件 (*.exe);;所有文件 (*)");
inline const QString kDlgCleanupTitle         = QStringLiteral("清理测试文件");
inline const QString kMsgConfirmCleanup       = QStringLiteral("将删除 %1 里的 %2 个测试文件，共 %3。\n\n确定继续吗？");
inline const QString kDlgSaveDataTitle        = QStringLiteral("保存测试数据");
/// 默认文件名：ml_disktest-可靠性测试-20261012-183000.csv
/// 段名直接用模式名，和时间戳一起构成"这份结果是什么、什么时候跑的"。
inline const QString kRunDataFileNameFmt      = QStringLiteral("ml_disktest-%1-%2.csv");
/// 文件名里的时间戳：可排序、不含冒号（Windows 文件名不允许冒号）
inline const QString kRunDataTimeStampFmt     = QStringLiteral("yyyyMMdd-HHmmss");
inline const QString kFilterCsv               = QStringLiteral("CSV 文件 (*.csv);;所有文件 (*)");
inline const QString kDlgLoadDataTitle        = QStringLiteral("打开测试结果");
inline const QString kDlgSaveDiagnosticsTitle = QStringLiteral("保存 fio 诊断输出");
inline const QString kFilterTextLog           = QStringLiteral("文本文件 (*.log *.txt);;所有文件 (*)");
inline const QString kAboutHtml               =
    QStringLiteral("<h3>%1</h3>"
                   "<table cellspacing='0' cellpadding='3'>"
                   "<tr><td><b>版本</b></td><td>%2</td></tr>"
                   "<tr><td><b>作者</b></td><td>%3</td></tr>"
                   "<tr><td><b>联系作者</b></td><td>%4</td></tr>"
                   "<tr><td><b>编译时间</b></td><td>%5</td></tr>"
                   "<tr><td><b>构建环境</b></td><td>Qt %6 · %7</td></tr>"
                   "</table>"
                   "<p>本工具基于fio，会在选中分区的 <code>%8</code> 目录里生成 "
                   "<code>%9_*</code> 文件进行测试，并读取校验。仅使用文件模式。"
                   "<p><b>感谢你使用本工具。</b>本工具尚在测试阶段，有不足的地方"
                   "请反馈。</p>"
                   // ── 开源软件声明：下面这段要改就整段改 ──
                   "<h4>开源软件声明</h4>"
                   "<p>本程序使用了以下开源软件，在此致谢。各软件的著作权归其各自"
                   "所有者，遵循其各自的许可协议：</p>"
                   "<table cellspacing='0' cellpadding='3'>"
                   "<tr><td><b>fio</b></td><td>GPL-2.0</td>"
                   "<td>Jens Axboe 等，<a href='https://github.com/axboe/fio'>"
                   "github.com/axboe/fio</a></td></tr>"
                   "<tr><td><b>Qt</b></td><td>LGPL-3.0</td>"
                   "<td>The Qt Company，<a href='https://www.qt.io'>qt.io</a></td></tr>"
                   "</table>"
                   "<p>本程序仅调用 fio，未对 fio 做任何修改；"
                   "Qt 为动态链接，随程序一起分发的 Qt 运行库可自行替换。</p>"
                   "<p>完整许可条款随程序一起分发，见程序目录的 <code>licenses</code> 子目录："
                   "<code>fio-COPYING.txt</code>（fio）、<code>Qt-LGPLv3.txt</code>（Qt）。</p>");
inline const QString kDlgAboutTitle           = QStringLiteral("关于");
inline const QString kTextNoneInParens        = QStringLiteral("(无)");
inline const QString kMsgPhaseProblem         = QStringLiteral("%1 发现异常");
inline const QString kMsgTestStoppedSuffix    = QStringLiteral("\n\n测试已停止。");

// 打开「写入」时如果目标分区里已有测试文件，先问一句要不要覆盖
inline const QString kDlgOverwriteTitle = QStringLiteral("目标分区里已有测试文件");
inline const QString kMsgOverwriteBody  =
    QStringLiteral("目标分区 %1 的测试目录里已经有 %2 个测试文件，合计 %3。\n\n"
                   "继续写入会先删除这些旧文件，再重新完整写一遍。\n\n"
                   "确定继续吗？");
inline const QString kBtnContinue = QStringLiteral("继续");
inline const QString kBtnCancel   = QStringLiteral("取消");
inline const QString kBtnClose    = QStringLiteral("关闭");

// 中途停止时的提示与日志
inline const QString kLogStoppedWriteCleaned =
    QStringLiteral("[停止] 写入没有跑完，盘上是不完整的数据，已删除测试文件");
inline const QString kLogStoppedWriteCleanFailed =
    QStringLiteral("[停止] 写入没有跑完，但删除测试文件失败：%1");
inline const QString kLogOverwriteCancelled =
    QStringLiteral("[取消] 目标分区里已有测试文件，用户选择不覆盖");

// ── 错误信息 ──
inline const QString kErrNeedDrive              = QStringLiteral("请先在「选择磁盘」列表里选中要测试的分区。");
inline const QString kErrNoTestFolder           = QStringLiteral("无法确定测试目录。");
inline const QString kErrNeedOnePhase           = QStringLiteral("「写入」和「读取」至少要勾选一个。");
inline const QString kErrRoundsTooSmall    = QStringLiteral("测试圈数至少为 1。");
inline const QString kErrIodepthTooSmall        = QStringLiteral("队列深度至少为 1。");
inline const QString kErrTargetUnavailable      = QStringLiteral("目标分区不可用：%1");
inline const QString kErrReadCapacity           = QStringLiteral("无法读取 %1 的容量信息。");
inline const QString kErrNoDriveSelected        = QStringLiteral("没有选中分区");
inline const QString kErrNoDriveSelectedDetail  = QStringLiteral("请先在「选择磁盘」列表里选中一个分区。");
inline const QString kErrStopBeforeCleanup      = QStringLiteral("请先点「停止」再清理。");
inline const QString kErrBusy                   = QStringLiteral("正在测试中");
inline const QString kErrNoTestFiles            = QStringLiteral("没有测试文件");
inline const QString kErrNoTestFilesInFolder    = QStringLiteral("%1 里没有 testfile_* 文件。");
inline const QString kErrCleanupFailed          = QStringLiteral("清理失败");
inline const QString kErrNoDataToSave           = QStringLiteral("没有可保存的数据");
inline const QString kErrNoSamplesYet           = QStringLiteral("还没有跑过测试，或者没有采到任何采样点。");
inline const QString kErrLoadDataFailed         = QStringLiteral("打开测试结果失败");
/// 结果文件版本过旧：%1 = 文件路径，%2 = 文件自己的版本，%3 = 本程序要求的最低版本
inline const QString kErrRunDataTooOld =
    QStringLiteral("这份结果的版本过旧，本程序不再支持。\n\n"
                   "文件：%1\n"
                   "文件版本：%2\n"
                   "本程序要求：%3 及以上\n\n"
                   "请用当前版本重新跑一次测试并保存。");
/// 同上，但文件里根本没写版本（2.2.7 之前存的都没有）
inline const QString kErrRunDataTooOldNoVersion =
    QStringLiteral("这份结果的版本过旧，本程序不再支持。\n\n"
                   "文件：%1\n"
                   "文件版本：未记录（%2 之前的版本存的文件都不写版本号）\n\n"
                   "请用当前版本重新跑一次测试并保存。");
inline const QString kMsgBadCsvHeader           =
    QStringLiteral("%1\n第一行应当是 \"# %2\"，并且含 [samples] 数据段。");
inline const QString kErrBusyLoading            = QStringLiteral("正在测试中，请先点「停止」再加载历史数据。");
inline const QString kErrNoContentToSave        = QStringLiteral("没有可保存的内容");
inline const QString kErrNoOutputYet            = QStringLiteral("还没有跑过测试，或者 fio 没有产生输出。");
inline const QString kErrDeleteOldFiles         = QStringLiteral("无法删除旧文件：%1");
inline const QString kErrCreateFolder           = QStringLiteral("无法创建测试目录：%1");
inline const QString kMsgSelfTestTargetMismatch =
    QStringLiteral("自检指定的是 %1，但当前选中的是 %2。\n"
                   "已中止，没有写入任何数据。");
inline const QString kErrTargetMismatch         = QStringLiteral("目标分区不匹配");
inline const QString kErrBadParams              = QStringLiteral("参数有误");
inline const QString kErrFioNotFound            = QStringLiteral("找不到 fio");
inline const QString kErrSpecifyFioPath         = QStringLiteral("请先指定 fio.exe 的路径。");
inline const QString kErrNoTestFilesForRead     =
    QStringLiteral("%1 里没有 testfile_* 文件，无法单独执行读取。\n"
                   "请先勾选「写入」跑一次。");
inline const QString kErrReadCapacityFailed     = QStringLiteral("读取容量失败");
inline const QString kErrNoSpace                = QStringLiteral("空间不足");
inline const QString kErrNoWritableSpace        = QStringLiteral("目标分区剩余 %1，没有可写的空间。");
inline const QString kErrCleanupOldFailed       = QStringLiteral("清理旧文件失败");
inline const QString kErrCreateFolderFailed     = QStringLiteral("创建测试目录失败");
inline const QString kErrCreateJobFile          = QStringLiteral("无法创建 job 文件");
inline const QString kErrStartWritePhase        = QStringLiteral("启动写入阶段失败");
inline const QString kErrNoTestFilesAtReadStart = QStringLiteral("读取阶段开始时 %1 里没有 testfile_* 文件。");
inline const QString kErrStartReadPhase         = QStringLiteral("启动读取阶段失败");

// ── fio 运行与告警 ──
inline const QString kProblemOpenFailed          = QStringLiteral("fio 报告有文件打开失败（failed to open）");
inline const QString kProblemIoUnitError         = QStringLiteral("fio 报告 I/O 单元错误（io_u error）");
inline const QString kProblemPermissionDenied    = QStringLiteral("fio 报告权限不足（Permission denied）");
inline const QString kProblemNoSpaceLeft         = QStringLiteral("目标分区空间不足（No space left on device）");
inline const QString kProblemBadOption           = QStringLiteral("fio 拒绝了某个选项（Bad option）");
inline const QString kProblemJobGlobalDropped    = QStringLiteral("fio 丢弃了 job 里的全局选项（job global dropped）");
inline const QString kProblemCannotOpen          = QStringLiteral("fio 无法打开目标（cannot open）");
inline const QString kProblemBadMagicHeader      = QStringLiteral("★ 数据校验失败：块的校验头不匹配，磁盘上的内容与写入时不一致");
inline const QString kProblemVerifyFailed        = QStringLiteral("★ 数据校验失败（verify failed）");
inline const QString kProblemIllegalByteSequence = QStringLiteral("★ I/O 返回非法字节序列，通常是校验不一致");

// ── 校验失败的大字告警（测试阶段显示在右侧图表区上方）──
/// 唯一的一行：最要紧的一句。%1 = 已经发现的坏块数
inline const QString kVerifyBannerHead = QStringLiteral("⚠ 数据校验失败：已发现 %1 处不一致");
/// 第二行：只说结论和该做什么，不摆 fio 原话——偏移量在日志里有逐处明细。
inline const QString kVerifyBannerNote =
    QStringLiteral("该磁盘存在问题，请尽快备份数据并更换该盘。");
/// 日志里每发现一块坏块记一行：%1 = 第几块，%2 = fio 原话
inline const QString kLogVerifyFailure = QStringLiteral("校验失败第 %1 处：%2");
/// 阶段结束时把总数写进日志：%1 = 总块数
inline const QString kLogVerifySummary = QStringLiteral("★ 本阶段共发现 %1 处数据校验失败");
/// 逐处明细被截断时补一句：%1 = 记了多少条，%2 = 总共多少处
inline const QString kLogVerifyTruncated =
    QStringLiteral("（上面只列出前 %1 处，本次共 %2 处）");
inline const QString kLogVerifyNone    = QStringLiteral("数据校验通过：没有发现不一致的块");
/// 报告里的校验失败行：%1 = 第几处，%2 = fio 原话
inline const QString kReportVerifyLine = QStringLiteral("*** 数据校验失败第 %1 处: %2");
inline const QString kStatusVerifyFailed = QStringLiteral("★ 已发现 %1 处数据校验失败");
/// 自检报告里的校验失败统计
inline const QString kReportVerifyCount = QStringLiteral("  校验失败块数      : ");
inline const QString kReportVerifyFirst = QStringLiteral("  首处失败原话      : ");
inline const QString kReportVerifyWhere = QStringLiteral("      · ");
inline const QString kReportVerifyTruncated =
    QStringLiteral("      （上面只列出前 %1 处，本次共 %2 处）");
/// 开发排查：强制显示告警条时的标记（必须在界面和日志里说明这是伪造的）
inline const QString kLogDebugBannerForced =
    QStringLiteral("【开发排查】校验失败告警条是**强制显示**的示例，不是本次真实结果");
inline const QString kDebugBannerTitleSuffix = QStringLiteral("  [告警条为示例]");
inline const QString kErrNoTempDir               = QStringLiteral("找不到可写的临时目录。");
inline const QString kErrWriteJobFile            = QStringLiteral("无法写入 job 文件：\n%1\n%2");
inline const QString kErrPhaseRunning            = QStringLiteral("已经有一个阶段在运行。");
inline const QString kErrFioNotSpecified         = QStringLiteral("找不到 fio 可执行文件，请在界面上指定路径。");
inline const QString kFioWorkingDir              = QStringLiteral("  (工作目录: %1)");
inline const QString kErrFioStartFailed          = QStringLiteral("无法启动 fio：%1");
inline const QString kFioIocounterUnavailable    = QStringLiteral("提示：无法读取 fio 进程 I/O 计数，实时曲线将不可用。");
inline const QString kFioUserStop                = QStringLiteral("… 用户请求停止");
inline const QString kFioTestFinished            = QStringLiteral("… 测试已结束（%1），停止采样");
inline const QString kFioJobEnded                = QStringLiteral("fio 报告 job 已结束");
inline const QString kFioSummaryPrinted          = QStringLiteral("fio 已输出汇总");

// ── 自检报告 ──
inline const QString kReportDataSaved        = QStringLiteral("测试数据已保存: %1");
inline const QString kReportLoadData         = QStringLiteral("打开测试结果: %1");
inline const QString kReportLoadedConfig     = QStringLiteral("  模式=%1  块大小=%2  iodepth=%3  校验=%4");
inline const QString kReportLoadedMode       = QStringLiteral("  这份数据是：%1");
inline const QString kReportTestStart        = QStringLiteral("=========== 测试开始 ===========");
inline const QString kReportConfig           = QStringLiteral("配置: %1");
inline const QString kReportTargetFolder     = QStringLiteral("目标目录: %1");
inline const QString kReportReadOnlyMode     = QStringLiteral("只读模式：使用已有的 %1 个测试文件（合计 %2）");
inline const QString kReportVolumeSpace      = QStringLiteral("分区容量 %1，剩余 %2，安全余量 %3");
inline const QString kReportWritePlan        = QStringLiteral("计划写入 %1 个文件 × %2 = %3（文件由 fio 边写边建，不预分配）");
inline const QString kReportAxisFixed        = QStringLiteral("横轴固定为 0 ~ %1（按测试容量，不再随时间扩展）");
inline const QString kReportWriteJobHeader   = QStringLiteral("-------------------- 写入 job --------------------");
inline const QString kReportReadPhaseFiles   = QStringLiteral("读取阶段：%1 个文件，合计 %2");
inline const QString kReportReadJobHeader    = QStringLiteral("-------------------- 读取 job --------------------");
inline const QString kReportWarningLine      = QStringLiteral("*** 警告: %1");
inline const QString kReportTestEnd          = QStringLiteral("=========== 测试结束 ===========");
inline const QString kReportTestPartition    = QStringLiteral("测试分区: %1");
inline const QString kReportRwMode           = QStringLiteral("读写模式: %1");
inline const QString kReportFioExitCode      = QStringLiteral("  fio 退出码: %1");
inline const QString kReportSampleWindow     = QStringLiteral("  采样点数: %1  窗口 %2 秒");
inline const QString kReportReadAvgMinMax    = QStringLiteral("  平均读取: %1   (min %2 / max %3)");
inline const QString kReportWriteAvgMinMax   = QStringLiteral("  平均写入: %1   (min %2 / max %3)");
inline const QString kReportNoSamples        = QStringLiteral("  注意: 本阶段没有可用采样点（只跑了一拍，首拍已被丢弃）");
inline const QString kReportWarning          = QStringLiteral("  警告: %1");
inline const QString kReportVerdictBad       = QStringLiteral("结论: 发现异常，请查看上面的警告");
inline const QString kReportVerdictOk        = QStringLiteral("结论: 正常");
inline const QString kReportVerdictStopped   = QStringLiteral("结论: 已手动停止");
inline const QString kSelfTestDataSaved      = QStringLiteral("[自检] 测试数据已保存: %1");
inline const QString kSelfTestSaveFailed     = QStringLiteral("[自检] 保存测试数据失败: %1");
inline const QString kReportError            = QStringLiteral("[错误] %1");
inline const QString kSelfTestLoadFailed     = QStringLiteral("[自检] 打开测试结果失败: %1");
inline const QString kSelfTestTargetNotFound = QStringLiteral("找不到目标分区：自检指定的是 %1。");
inline const QString kTextUnspecified        = QStringLiteral("(未指定)");
inline const QString kSelfTestPrefix         = QStringLiteral("[自检] ");
inline const QString kSelfTestEnumerated     = QStringLiteral("  枚举到: ");
inline const QString kSelfTestBegin          = QStringLiteral("[自检] 开始无人值守自检…");
inline const QString kSelfTestPlanLine       = QStringLiteral("    剩余 %1 → %2 个 × %3 = %4  %5");
inline const QString kSelfTestViolation      = QStringLiteral("*** 违规 ***");
inline const QString kSelfTestPlanInvalid    = QStringLiteral("    剩余 %1 → 计划无效（空间不足）");
inline const QString kSelfTestStepLine       = QStringLiteral("    %1 → 每 %2 个点画 1 个  %3");
inline const QString kPhaseNameWrite         = QStringLiteral("写入");
inline const QString kPhaseNameRead          = QStringLiteral("读取");
inline const QString kTextNone               = QStringLiteral("无");
inline const QString kTextNotObserved        = QStringLiteral("(没观察到)");

// ── 自检报告正文 ──
inline const QString kReportStepProbe128GiB  = QStringLiteral("128 GiB（下边界）");
inline const QString kReportStepProbe1TiB    = QStringLiteral("1 TiB（下边界）");
inline const QString kReportTitle            = QStringLiteral("==================== 硬盘可靠性测试工具 自检报告 ====================\n");
inline const QString kReportQtVersion        = QStringLiteral("Qt 版本            : ");
inline const QString kReportQpaPlatform      = QStringLiteral("QPA 平台插件        : ");
inline const QString kReportFioVersion       = QStringLiteral("fio 版本            : ");
inline const QString kReportTargetPartition  = QStringLiteral("目标分区            : ");
inline const QString kReportTestFolder       = QStringLiteral("测试目录            : ");
inline const QString kReportTestConfig       = QStringLiteral("测试配置            : ");
inline const QString kReportRounds           = QStringLiteral("测试圈数            : ");
inline const QString kReportDataSource       = QStringLiteral("数据来源            : 加载自 ");
inline const QString kReportNotRealtime      = QStringLiteral("（不是本次实时采集）\n");
inline const QString kReportStartupError     = QStringLiteral("启动/运行错误       : ");
inline const QString kReportVolumeList       = QStringLiteral("枚举到的卷          : ");
inline const QString kReportSuffixCount      = QStringLiteral(" 个\n");
inline const QString kReportPhaseList        = QStringLiteral("阶段                : ");
inline const QString kReportFillLimit        = QStringLiteral("填充上限(自检)      : ");
inline const QString kReportFilePlanHead     = QStringLiteral("文件计划            : ");
inline const QString kReportSuffixTimes      = QStringLiteral(" 个 × ");
inline const QString kReportExistingFiles    = QStringLiteral("已有测试文件        : ");
inline const QString kReportPhaseExitCode    = QStringLiteral("  fio 退出码        : ");
inline const QString kReportCancelledTag     = QStringLiteral("（已取消）");
inline const QString kReportPhaseSamples     = QStringLiteral("  采样点数          : ");
inline const QString kReportWindowPrefix     = QStringLiteral("  窗口 ");
inline const QString kReportSuffixSeconds    = QStringLiteral(" 秒\n");
inline const QString kReportReadMinAvgMax    = QStringLiteral("  读取 MiB/s        : min=%1 avg=%2 max=%3\n");
inline const QString kReportWriteMinAvgMax   = QStringLiteral("  写入 MiB/s        : min=%1 avg=%2 max=%3\n");
inline const QString kReportSummaryRead      = QStringLiteral("  fio 汇总 读取     : ");
inline const QString kReportSummaryWrite     = QStringLiteral("  fio 汇总 写入     : ");
inline const QString kReportPhaseWarnings    = QStringLiteral("  告警              : ");
inline const QString kReportPlotStyle        = QStringLiteral("绘图方式            : ");
inline const QString kReportSamplePoints     = QStringLiteral("采样点数            : ");
inline const QString kReportLineSeries       = QStringLiteral("  折线序列=");
inline const QString kReportScatterSeries    = QStringLiteral("  散点序列=");
inline const QString kReportSkippedFirst     = QStringLiteral("忽略的首拍          : ");
inline const QString kReportSkippedFirstNote = QStringLiteral(" 个（每个阶段的第 1 拍含 fio 启动开销，已丢弃）\n");
inline const QString kReportAxisHead         = QStringLiteral("横轴                : 固定 0 ~ ");
inline const QString kReportAxisMid          = QStringLiteral("（按测试容量）；曲线首点 x=");
inline const QString kReportAxisLastX        = QStringLiteral(" GiB，末点 x=");
inline const QString kReportAxisTimeFmt      = QStringLiteral("横轴                : 固定 0 ~ %1 秒（按时间）；曲线首点 x=%2 秒，末点 x=%3 秒\n");
inline const QString kReportCardCurrentRead  = QStringLiteral("结束时卡片          : 当前读=");
inline const QString kReportCardCurrentWrite = QStringLiteral("  当前写=");
inline const QString kReportCardAvgRead      = QStringLiteral("  平均读=");
inline const QString kReportCardAvgWrite     = QStringLiteral("  平均写=");
inline const QString kReportCardElapsed      = QStringLiteral("  已用时=");
inline const QString kReportEndChecks        = QStringLiteral("结束时勾选          : 写入=%1  读取=%2  校验=%3\n");
inline const QString kReportStateEnd         = QStringLiteral("状态标签            : 结束时=");
inline const QString kReportStateObserved    = QStringLiteral("   运行期间出现过=");
inline const QString kReportGuardRawDevice   = QStringLiteral("裸设备路径拦截      : ");
inline const QString kReportGuardSyncEngine  = QStringLiteral("同步引擎分类        : ");
inline const QString kReportGuardWindowIcon  = QStringLiteral("窗口图标            : ");
inline const QString kReportGuardAutoStep    = QStringLiteral("折线抽稀步长（自动分档）: ");
inline const QString kReportGuardExclusive   = QStringLiteral("单选互斥（绘图方式/密度）: ");
inline const QString kReportExclusiveNote    = QStringLiteral("（任何时刻恰好一个被选中）\n");
inline const QString kReportGuardFilePlan    = QStringLiteral("文件规划（每个 ≤2GiB）: ");
inline const QString kReportFilesPresent     = QStringLiteral("测试文件已生成      : ");
inline const QString kReportGuardLoadedData  = QStringLiteral("加载数据            : ");
inline const QString kReportLoadedDataNote   = QStringLiteral(" 个采样点）\n");
inline const QString kReportGuardResultWindow = QStringLiteral("结果窗口            : ");
inline const QString kReportResultWindowItem  = QStringLiteral("（独立窗口打开 OK，当前显示项 = %1）");
inline const QString kReportLoadedModeGuard   = QStringLiteral("结果数据模式判别    : ");
/// 启动默认值与 fio 查找规则的断言
inline const QString kReportGuardDefaults     = QStringLiteral("启动默认值          : ");
inline const QString kReportDefaultsDetail    =
    QStringLiteral("（可靠性 · 不选盘 · 顺序读写 · iodepth=%1 · 1 圈 · 写入/读取/校验全开 · 性能四项全勾 · 数据 %2 MiB）");
inline const QString kReportGuardFioLookup    = QStringLiteral("fio 查找规则        : ");
inline const QString kReportFioLookupDetail   =
    QStringLiteral("（只看 exe 目录 → 再走 PATH，不扫盘；当前 = %1）");
inline const QString kReportLoadedItemsFmt    = QStringLiteral("；分出的测试项 = %1 项");
inline const QString kReportGuardPerfItems    = QStringLiteral("性能四项读写值      : ");
inline const QString kSelfTestResultWindowFailed =
    QStringLiteral("[自检] 结果窗口打开失败（同一份 CSV 灌主窗口是成功的，说明问题在窗口这一侧）");
inline const QString kReportGuardProblems    = QStringLiteral("问题检查            : ");
inline const QString kReportNoProblems       = QStringLiteral("PASS（无告警、job err 全 0）");
inline const QString kReportRawOutput        = QStringLiteral("fio 原始输出        : ");
inline const QString kReportCsvOutput        = QStringLiteral("逐点采样 CSV        : ");
inline const QString kReportChartPng         = QStringLiteral("图表 PNG            : ");
inline const QString kReportWindowPng        = QStringLiteral("整窗 PNG            : ");
inline const QString kReportVerdict          = QStringLiteral("自检结论            : ");
inline const QString kReportStoppedTag       = QStringLiteral("（含手动停止）");

// ── 命令行帮助 ──
inline const QString kAppDescription = QStringLiteral("基于 fio 的硬盘可靠性测试工具（只做文件模式测试）");
inline const QString kClSelfTest     = QStringLiteral("无人值守自检：跑一轮完整测试并把报告写到 --outdir。");
inline const QString kClOutDir       = QStringLiteral("自检输出目录（默认 ./_selftest）");
inline const QString kClFioPath      = QStringLiteral("指定 fio.exe 的路径");
inline const QString kClTarget       = QStringLiteral("自检用的目标分区，例如 G:/");
inline const QString kClFillLimit    = QStringLiteral("自检时把“写满分区”的目标压到这么多 MiB（真写满大盘太慢）");
inline const QString kClMode         = QStringLiteral("自检用的读写模式：seq / rand");
inline const QString kClStopAfter    = QStringLiteral("自检时在第 N 秒触发一次「停止」，验证停止路径");
inline const QString kClScatter      = QStringLiteral("自检时把绘图方式切到散点图（默认折线图）");
inline const QString kClReadOnly     = QStringLiteral("自检时只跑读取阶段（验证「有文件才能只读」）");
inline const QString kClLowDensity   = QStringLiteral("自检时把折线图切到低密度（每 3 点画 1 点）");
inline const QString kClSaveData     = QStringLiteral("自检跑完后把测试数据保存到这个 CSV");
inline const QString kClLoadData     = QStringLiteral("自检直接加载这个 CSV 画图并出报告，不跑测试");

// ── 通用文本（跨区域共用） ──
inline const QString kTextUnknown    = QStringLiteral("未知");
inline const QString kTextSaveFailed = QStringLiteral("保存失败");
// 下面几个是非 ASCII 但非汉字的符号，统一放这里免得散在代码里
inline const QString kTextWarningCount = QStringLiteral("%1 × %2");  ///< 告警重复次数后缀
inline const QString kTextJoinDot      = QStringLiteral(" · ");       ///< 一句话里各段之间的分隔
inline const QString kTextTitleColon   = QStringLiteral("%1：%2");   ///< "标题：内容"（全角冒号）
inline const QString kTextArrow        = QStringLiteral(" → ");       ///< 阶段/状态的箭头连接
inline const QString kTextParenOpen    = QStringLiteral("（");        ///< 报告里的左括号

// 阶段标题：多圈时带上圈号，例如 "--- 第 2 圈 · 写入阶段 ---"
inline const QString kPhaseHeadFmt     = QStringLiteral("--- %1 ---");
inline const QString kRoundTagFmt      = QStringLiteral("第 %1 圈 · ");
// ── 模式与性能测试 ──
inline const QString kModeNameReliability = QStringLiteral("可靠性测试");
inline const QString kModeNamePerformance = QStringLiteral("性能测试");
inline const QString kLabelTestMode       = QStringLiteral("测试模式");
inline const QString kModeReliabilityTip  = QStringLiteral("把选中分区写满，再读回逐块校验，用来发现坏块和静默数据损坏。");
inline const QString kModePerformanceTip  = QStringLiteral("先建好测试文件，再依次跑四个测试项，每项固定 10 秒，测读/写吞吐。");
// 四个测试项
inline const QString kPerfSeqQ8T1         = QStringLiteral("顺序读写 Q8T1");
inline const QString kPerfSeqQ1T1         = QStringLiteral("顺序读写 Q1T1");
inline const QString kPerfRandQ32T1       = QStringLiteral("随机读写 Q32T1");
inline const QString kPerfRandQ1T1        = QStringLiteral("随机读写 Q1T1");
inline const QString kGroupPerfItems      = QStringLiteral("测试项（每项固定 %1 秒）");
inline const QString kLabelPerfSize       = QStringLiteral("测试数据大小");
inline const QString kPerfSizeTip         = QStringLiteral("测试文件会按这个大小事先建好（固定单文件，不分片）。");
inline const QString kPerfColRead         = QStringLiteral("读取 (MiB/s)");
inline const QString kPerfColWrite        = QStringLiteral("写入 (MiB/s)");
inline const QString kPerfHeaderHint      = QStringLiteral("勾选要跑的项目，结果会填到右边");
inline const QString kWindowTitleFmt      = QStringLiteral("%1 %2 -%3");
inline const QString kChartPlaceholder    = QStringLiteral("请先点击「开始测试」");
inline const QString kChartTitleTime      = QStringLiteral("时间（秒）");
inline const QString kChartTitleCapacity  = QStringLiteral("测试容量（GiB）");

// ── 测试结果窗口（打开历史 CSV 用，完全不碰主界面）──
inline const QString kResultWindowTitleFmt = QStringLiteral("%1 - %2");
inline const QString kResultModeLabelFmt   = QStringLiteral("测试模式：%1");
inline const QString kResultSourceFmt      = QStringLiteral("数据来源：%1");
inline const QString kResultSummaryTarget  = QStringLiteral("目标分区");
inline const QString kResultSummaryMode    = QStringLiteral("读写模式");
inline const QString kResultSummaryBlock   = QStringLiteral("块大小");
inline const QString kResultSummaryRounds  = QStringLiteral("测试圈数");
inline const QString kResultSummarySamples = QStringLiteral("采样点数");
inline const QString kResultSummaryElapsed = QStringLiteral("已用时");
inline const QString kResultSummaryAvgRead = QStringLiteral("平均读取");
inline const QString kResultSummaryAvgWrite= QStringLiteral("平均写入");
/// 性能结果的均速：统计的是**整轮采样**（顺序项 + 随机项都在里面），
/// 所以带上"顺序"二字——性能测试各项的读写模式由测试项决定，
/// 笼统写"平均读取"会让人以为整轮都是顺序的。
inline const QString kResultSummarySeqRead = QStringLiteral("平均顺序读取");
inline const QString kResultSummarySeqWrite= QStringLiteral("平均顺序写入");
inline const QString kResultSummaryData    = QStringLiteral("测试数据");
inline const QString kResultSummaryPlan    = QStringLiteral("文件计划");
inline const QString kResultNoSamples      = QStringLiteral("这份文件里没有任何采样点。");
inline const QString kResultPerfHint       =
    QStringLiteral("四条曲线分别对应四个测试项。点某一行下面的数字格子，"
                   "图表里就只显示那一项的读写曲线；再点一次恢复显示全部。");
inline const QString kResultReliabilityHint =
    QStringLiteral("曲线横轴是已处理容量，红色为读取、蓝色为写入。");
inline const QString kResultLegacyModeNote = QStringLiteral("（老文件未记录测试模式，按可靠性数据渲染）");
// 结果窗口里的坏块提示**直接复用实时告警条那两句**
// （kVerifyBannerHead + kVerifyBannerNote）：同一件事不该有两套措辞。
inline const QString kResultShowAll        = QStringLiteral("显示全部项");
inline const QString kResultShowOneFmt     = QStringLiteral("只显示：%1");
inline const QString kResultPerfItemFmt    = QStringLiteral("%1 读 %2 / 写 %3");
inline const QString kResultPerfNoItemNote =
    QStringLiteral("这份性能数据没有记录每一项的归属（老格式），只能画一条读/写合计曲线。"
                   "重新跑一次性能测试并保存，就能按测试项分别回看。");
inline const QString kLogPerfItemStart    = QStringLiteral("%1 开始（每趟 %2 秒，读写各一趟）");
inline const QString kLogPerfItemDone     = QStringLiteral("%1 完成：读取 %2，写入 %3");
inline const QString kLogPerfAllDone      = QStringLiteral("全部 %1 项测试结束");
inline const QString kLogPerfCleanup      = QStringLiteral("性能测试结束，已删除测试文件");
inline const QString kStatusFinishedFinal = QStringLiteral("测试结束");
inline const QString kStatusStoppedFinal  = QStringLiteral("已停止（测试文件已清理）");
inline const QString kTargetPerfPlanFmt  = QStringLiteral("\n测试数据: 单个文件 %1");
inline const QString kStatusPerfPass     = QStringLiteral("正在测试：%1 · %2");
inline const QString kLogPerfPassStart    = QStringLiteral("%1 · %2 开始（%3 秒）");
inline const QString kLogDroppedTail      = QStringLiteral("丢弃本阶段最后一拍（必然横跨收尾，速率偏低）：%1");
inline const QString kMsgFat32TooBigTitle = QStringLiteral("测试数据太大");
inline const QString kErrFat32TooBig     =
    QStringLiteral("目标分区 %1 是 FAT32 格式，单个文件最大只能到 4 GiB。\n\n"
                   "测试数据大小选了 %2，单文件放不下。\n\n"
                   "请改成 4 GiB 及以下，或者换一个 NTFS / exFAT 分区。");
inline const QString kLogPreallocating    = QStringLiteral("正在预建测试文件：%1 个 × %2");
inline const QString kLogPreallocated     = QStringLiteral("测试文件已就绪，合计 %1");
inline const QString kErrPerfNoItem       = QStringLiteral("请至少勾选一个测试项。");
inline const QString kErrPerfDataSize     = QStringLiteral("测试数据大小必须大于 0。");
inline const QString kModeGroupTitle      = QStringLiteral("测试模式");
inline const QString kLogPreparingFiles   = QStringLiteral("准备中：正在预建测试文件 %1/%2");
inline const QString kLogPerfPassDone     = QStringLiteral("%1 %2 完成：%3");
inline const QString kDlgTraceTitle       = QStringLiteral("%1 · %2（%3）");
inline const QString kTraceShownTip       = QStringLiteral("右边图表显示的是 %1 的读写曲线");
inline const QString kTraceNoData         = QStringLiteral("这一项还没有数据。");
inline const QString kTraceBusyTip        = QStringLiteral("测试进行中，结束后才能查看明细。");
inline const QString kTraceClickTip       = QStringLiteral("点击在右边图表里显示这一项的读写曲线");
inline const QString kErrPerfNoSpace      = QStringLiteral("目标分区剩余 %1，放不下 %2 的测试数据。");
inline const QString kErrPreallocFailed   = QStringLiteral("预建测试文件失败");
inline const QString kLogPerfAllPlanned   = QStringLiteral("共 %1 项，预计 %2 秒");
inline const QString kErrCreateJobFailed  = QStringLiteral("无法创建 job 文件");