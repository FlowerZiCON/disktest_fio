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

#include <QString>
#include <QStringList>
#include <QVector>

/// 硬盘测试工具的参数模型，两种模式共用。
///
/// **可靠性测试**：把选中分区写满（每个文件 ≤ 2 GiB），再读回逐块校验，
///                用来发现坏块和静默数据损坏。
/// **性能测试**：预先建好指定大小的测试文件，然后依次跑四个测试项，
///              每项固定 10 秒，测出读/写吞吐。
///
/// 两种模式都只在普通文件上做 I/O，不碰物理磁盘或裸设备。
class FioConfig
{
public:
    /// 两个大模式。
    enum class TestMode {
        Reliability,   ///< 可靠性测试：写满分区 → 读回逐块校验
        Performance    ///< 性能测试：固定 10 秒一项，测四项吞吐
    };

    /// 性能模式的四个测试项（对应结果表里的四行）。
    enum class PerfItem {
        SeqQ8T1,    ///< 顺序读写 Q8T1：4M，队列 8
        SeqQ1T1,    ///< 顺序读写 Q1T1：4M，队列 1
        RandQ32T1,  ///< 随机读写 Q32T1：4K，队列 32
        RandQ1T1    ///< 随机读写 Q1T1：4K，队列 1
    };

    /// 两个读写模式（可靠性测试用）：都是"先写满、再读回"。
    enum class RwMode {
        SequentialWriteRead,   ///< 顺序读写：bs=4M，顺序写满 → 顺序读回
        RandomWriteRead        ///< 随机读写：bs=4K，随机写满 → 随机读回
    };

    /// 文件规划：N 个等大文件，每个都不超过上限。
    struct FilePlan
    {
        int    fileCount  = 0;
        qint64 fileSize   = 0;   ///< 每个文件的字节数（≤ maxFileSize()）
        qint64 totalBytes = 0;
        bool   isValid() const { return fileCount > 0 && fileSize > 0; }
    };

    // ---- 性能模式的静态描述表 ----
    static QVector<PerfItem> allPerfItems();
    static QString perfItemName(PerfItem item);        ///< "顺序读写 Q8T1"
    static QString perfItemKey(PerfItem item);         ///< "seq_q8t1"
    static int     perfItemIodepth(PerfItem item);     ///< 8 / 1 / 32 / 1
    static bool    perfItemIsRandom(PerfItem item);
    static QString perfItemBlockSize(PerfItem item);   ///< "4m" / "4k"
    static PerfItem perfItemFromKey(const QString &key);

    /// 性能测试每一项固定跑多少秒。
    /// 采样窗口长度：图表和统计只取这么多秒。
    static int perfRuntimeSeconds() { return 10; }
    /// 采样窗口前面额外跳过的秒数：fio 启动 / 预热那段不要。
    /// 所以 fio 实际跑的是 perfRuntimeSeconds() + perfHeadSkipSeconds() + 1 秒。
    static double perfHeadSkipSeconds() { return 1.0; }
    /// 一趟的墙钟耗时（含 fio 启动与收尾），只用于界面上的时间估算。
    static int perfPassWallSeconds() { return perfRuntimeSeconds() + 2; }
    /// 性能测试可选的数据大小：128 MiB ~ 32 GiB 的常见档位。
    static QVector<qint64> perfSizeChoices();
    static QString perfSizeText(qint64 bytes);

    QString targetRoot;                              ///< 形如 "G:/"
    TestMode testMode   = TestMode::Reliability;

    // ---- 可靠性测试 ----
    RwMode  mode        = RwMode::SequentialWriteRead;
    int     iodepth     = 16;
    int     rounds      = 1;                         ///< 整轮测试重复几遍（1 = 只跑一遍）
    bool    doWrite     = true;                      ///< 写入阶段
    bool    doRead      = true;                      ///< 读取阶段
    bool    verify      = true;                      ///< 逐块 crc32c 校验

    // ---- 性能测试 ----
    qint64              perfDataBytes = qint64(1024) * 1024 * 1024;   ///< 测试数据大小，默认 1 GiB
    QVector<PerfItem>   perfItems;                   ///< 勾选要跑的测试项
    bool                perfItemChecked[4] = {true, true, true, true};

    /// 仅自检使用：把"写满整个分区"的目标压到这么多 MiB（0 = 不限制）。
    int     fillLimitMiB = 0;

    static QString folderName() { return QStringLiteral("ml_disktest"); }
    static QString fileBaseName() { return QStringLiteral("testfile"); }
    /// 单个测试文件的大小上限：2 GiB（FAT32 单文件 4 GiB 上限的一半）。
    static qint64  maxFileSize() { return qint64(2) * 1024 * 1024 * 1024; }
    /// 固定的 IO 引擎。Windows 上只有 windowsaio 支持直接 I/O。
    static QString ioEngine() { return QStringLiteral("windowsaio"); }
    static bool    directIo() { return true; }

    bool    isSequential() const;
    QString blockSize() const;                       ///< "4m" / "4k"
    QString writeRw() const;                         ///< "write" / "randwrite"
    QString readRw() const;                          ///< "read" / "randread"
    QString modeText() const;
    QString testModeText() const;                    ///< "可靠性测试" / "性能测试"

    QString testFolder() const;                      ///< <root>/ml_disktest
    QString testFilePattern() const;                 ///< testfile_*

    /// 卷上留出的安全余量：2%，夹在 256 MiB ~ 2 GiB 之间。
    static qint64 reserveBytes(quint64 volumeTotal);
    /// 按卷的容量与剩余空间规划"写满"（可靠性测试）。
    FilePlan planFiles(quint64 volumeTotal, quint64 volumeFree) const;
    /// 按指定字节数规划文件（性能测试：用户选的数据大小）。
    static FilePlan planFilesForSize(qint64 bytes);

    /// 可靠性测试：写入阶段的 job。
    QString writeJobFileText(const FilePlan &plan) const;
    /// 可靠性测试：读取阶段（校验）的 job。
    QString readJobFileText(int fileCount, qint64 totalBytes) const;
    /// 性能测试：某一个测试项的 job（固定跑 perfRuntimeSeconds() 秒）。
    /// writePass = true 生成写入趟，false 生成读取趟。两趟各跑 perfRuntimeSeconds() 秒。
    QString perfJobFileText(PerfItem item, bool writePass, int fileCount, qint64 fileSize) const;

    QStringList commonExtraArguments() const;
    QStringList writeExtraArguments() const;
    QStringList readExtraArguments() const;

    bool    validate(QString *error) const;
    QString describe() const;

    static QString modeKey(RwMode mode);
    static RwMode  modeFromKey(const QString &key);

    /// 是否是裸设备 / 物理磁盘路径（本工具只做文件模式，这类路径一律拒绝）。
    static bool looksLikeRawDevice(const QString &text);
    /// Windows 上同步 IO 引擎（psync / sync / vsync / mmap）不支持 direct=1。
    static bool engineRequiresBufferedIo(const QString &engine);
};
