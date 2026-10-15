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
    /// 结果图里"读取 / 写入"两条线的颜色：顺序项红/蓝，随机项黄/绿。
    /// **主窗口和结果窗口共用这一份规则** —— 同一份数据在两个窗口里画成
    /// 两种颜色，用户会以为是两组不同的数据。颜色只按测试项分，不按读/写分。
    static void perfItemColors(PerfItem item, QColor *readColor, QColor *writeColor);

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

    /// 目标**目录** —— 测试文件会建在 <targetDir>/ml_disktest 里。
    ///
    /// 为什么是目录而不是"分区/盘符根"：容量和"写满"针对的始终是这个目录
    /// **所在的文件系统**，而不是该目录本身。所以同一个文件系统里任意一个
    /// 可写目录都等价 —— 系统盘的 "/" 常常只有 root 能写，但
    /// /home/<用户> 就在同一个文件系统上，选它就等于测系统盘，而且不需要提权。
    /// 见 docs/readme.md 的「目标为什么是一个目录」。
    QString targetDir;
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
    /// 勾选要跑的测试项，下标是 PerfItem 的枚举序号。
    /// **只有这一份"哪些项要跑"的状态**（以前旁边还挂着一个 QVector<PerfItem>
    /// perfItems，谁都不读，却和第二份真相并存）。
    bool                perfItemChecked[4] = {true, true, true, true};

    /// 仅自检使用：把"写满整个分区"的目标压到这么多 MiB（0 = 不限制）。
    int     fillLimitMiB = 0;

    static QString folderName() { return QStringLiteral("ml_disktest"); }
    static QString fileBaseName() { return QStringLiteral("testfile"); }
    /// 单个测试文件的大小上限：2 GiB（FAT32 单文件 4 GiB 上限的一半）。
    static qint64  maxFileSize() { return qint64(2) * 1024 * 1024 * 1024; }

    /// 一次最多同时打开几个测试文件（fio 的 `openfiles=`）。
    ///
    /// **fio 默认会把 `nrfiles` 个文件全部同时打开**，然后在它们之间并行发 IO。
    /// 于是同一块盘上时刻有好几段区间在被写，落到盘上的位置来回跳，
    /// 实测**写入速度波动非常明显**（写入阶段的曲线一会儿高一会儿低）。
    /// 限成 1 之后同一时刻只有一段在写，曲线平稳得多，
    /// 也更接近"顺序写这一块盘"的真实表现。
    ///
    /// 代价是并行度降低：本来 N 个文件各自的 iodepth 是叠加的，
    /// 现在只有一路。测吞吐时这会让绝对数值略低一些，但换来的是**可读性** ——
    /// 这个工具要回答的是"这块盘稳不稳、有没有坏块"，
    /// 一条剧烈抖动的曲线既看不出坏块也看不出趋势。
    ///
    /// 只影响可靠性测试（写入 / 读取两个阶段都用）。
    /// 性能测试固定单文件（见 planFilesForSize），这个值对它没有意义。
    static int openFiles() { return 1; }

    /// 当前使用的 IO 引擎。
    ///
    /// Windows 上只有 windowsaio 支持直接 I/O（见 README §5.8）；
    /// Linux 上 direct=1 就是 O_DIRECT，libaio / io_uring / psync 都支持，
    /// 默认选 libaio —— 只有异步引擎才能让 iodepth 真正生效，
    /// 性能项的 Q8T1 / Q32T1 靠它才测得准。
    ///
    /// 这个值是**全局**的，由 MainWindow 在解析出 fio 路径之后
    /// 用 `fio --enghelp` 探一次再 setIoEngine() 定下来：
    /// 发行版的 fio 未必编了 libaio，探不到就自动退回 psync。
    static QString ioEngine();
    /// 平台默认引擎（还没探测过 fio 时用它）。
    static QString defaultIoEngine();
    /// 按优先级排列的候选引擎，探测时取第一个可用的。
    static QStringList candidateIoEngines();
    /// 用探测结果覆盖引擎；传空串恢复成平台默认。
    static void    setIoEngine(const QString &engine);
    /// job 里是否用直接 I/O（`direct=1`）。
    ///
    /// Linux 上 libaio / io_uring / posixaio / psync 都支持 O_DIRECT，恒为 true。
    /// Windows 上只有 windowsaio 支持；探测到的引擎若是 psync / sync / vsync /
    /// mmap 这类同步引擎，配上 direct=1 会直接报错，所以那种情况下退回缓冲 I/O，
    /// 并由界面明确说明"这次测量包含系统缓存，数字仅供参考"。
    /// （判据就是 engineRequiresBufferedIo —— 它以前只被自检调用，等于没有生效。）
    static bool    directIo();

    bool    isSequential() const;
    QString blockSize() const;                       ///< "4m" / "4k"
    QString writeRw() const;                         ///< "write" / "randwrite"
    QString readRw() const;                          ///< "read" / "randread"
    QString modeText() const;
    QString testModeText() const;                    ///< "可靠性测试" / "性能测试"

    QString testFolder() const;                      ///< <root>/ml_disktest

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
