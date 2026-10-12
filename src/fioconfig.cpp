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

#include "fioconfig.h"

#include "uitext.h"

#include <QDir>
#include <QFileInfo>

QVector<FioConfig::PerfItem> FioConfig::allPerfItems()
{
    return {PerfItem::SeqQ8T1, PerfItem::SeqQ1T1, PerfItem::RandQ32T1, PerfItem::RandQ1T1};
}

QString FioConfig::perfItemName(PerfItem item)
{
    switch (item) {
    case PerfItem::SeqQ8T1:   return kPerfSeqQ8T1;
    case PerfItem::SeqQ1T1:   return kPerfSeqQ1T1;
    case PerfItem::RandQ32T1: return kPerfRandQ32T1;
    case PerfItem::RandQ1T1:  return kPerfRandQ1T1;
    }
    return kTextUnknown;
}

QString FioConfig::perfItemKey(PerfItem item)
{
    switch (item) {
    case PerfItem::SeqQ8T1:   return QStringLiteral("seq_q8t1");
    case PerfItem::SeqQ1T1:   return QStringLiteral("seq_q1t1");
    case PerfItem::RandQ32T1: return QStringLiteral("rand_q32t1");
    case PerfItem::RandQ1T1:  return QStringLiteral("rand_q1t1");
    }
    return QStringLiteral("seq_q8t1");
}

FioConfig::PerfItem FioConfig::perfItemFromKey(const QString &key)
{
    for (PerfItem item : allPerfItems()) {
        if (perfItemKey(item) == key)
            return item;
    }
    return PerfItem::SeqQ8T1;
}

int FioConfig::perfItemIodepth(PerfItem item)
{
    switch (item) {
    case PerfItem::SeqQ8T1:   return 8;
    case PerfItem::SeqQ1T1:   return 1;
    case PerfItem::RandQ32T1: return 32;
    case PerfItem::RandQ1T1:  return 1;
    }
    return 1;
}

bool FioConfig::perfItemIsRandom(PerfItem item)
{
    return item == PerfItem::RandQ32T1 || item == PerfItem::RandQ1T1;
}

QString FioConfig::perfItemBlockSize(PerfItem item)
{
    return perfItemIsRandom(item) ? QStringLiteral("4k") : QStringLiteral("4m");
}

QVector<qint64> FioConfig::perfSizeChoices()
{
    constexpr qint64 kMiB = 1024LL * 1024;
    constexpr qint64 kGiB = 1024LL * kMiB;
    return {128 * kMiB, 256 * kMiB, 512 * kMiB,
            1 * kGiB, 2 * kGiB, 4 * kGiB, 8 * kGiB, 16 * kGiB, 32 * kGiB};
}

QString FioConfig::perfSizeText(qint64 bytes)
{
    constexpr double kGiB = 1024.0 * 1024 * 1024;
    constexpr double kMiB = 1024.0 * 1024;
    if (bytes >= qint64(kGiB))
        return QStringLiteral("%1 GiB").arg(bytes / kGiB, 0, 'g', 4);
    return QStringLiteral("%1 MiB").arg(bytes / kMiB, 0, 'g', 4);
}

QString FioConfig::testModeText() const
{
    return testMode == TestMode::Performance ? kModeNamePerformance : kModeNameReliability;
}

bool FioConfig::isSequential() const
{
    return mode == RwMode::SequentialWriteRead;
}

QString FioConfig::blockSize() const
{
    // 需求：顺序固定 4M，随机固定 4K
    return isSequential() ? QStringLiteral("4m") : QStringLiteral("4k");
}

QString FioConfig::writeRw() const
{
    return isSequential() ? QStringLiteral("write") : QStringLiteral("randwrite");
}

QString FioConfig::readRw() const
{
    // 读取阶段永远是纯读：不再有"边读边写"的混合模式
    return isSequential() ? QStringLiteral("read") : QStringLiteral("randread");
}

QString FioConfig::modeText() const
{
    switch (mode) {
    case RwMode::SequentialWriteRead: return kModeSeq;
    case RwMode::RandomWriteRead:     return kModeRand;
    }
    return kTextUnknown;
}

QString FioConfig::modeKey(RwMode mode)
{
    return mode == RwMode::RandomWriteRead ? QStringLiteral("rand") : QStringLiteral("seq");
}

FioConfig::RwMode FioConfig::modeFromKey(const QString &key)
{
    // 旧版本存过 seqmix / randmix，这里一律回落到"顺序读写"，
    // 免得读老配置文件或老 CSV 时出错。
    return key == QLatin1String("rand") ? RwMode::RandomWriteRead
                                        : RwMode::SequentialWriteRead;
}

bool FioConfig::looksLikeRawDevice(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t.isEmpty())
        return false;
    return t.startsWith(QLatin1String("\\\\.\\"))
        || t.startsWith(QLatin1String("\\\\?\\"))
        || t.startsWith(QLatin1String("//./"))
        || t.startsWith(QLatin1String("/dev/"))
        || t.contains(QLatin1String("physicaldrive"))
        || t.contains(QLatin1String("globalroot"))
        || t.contains(QLatin1String("harddiskvolume"));
}

bool FioConfig::engineRequiresBufferedIo(const QString &engine)
{
    static const QStringList syncEngines = {
        QStringLiteral("psync"), QStringLiteral("sync"),
        QStringLiteral("vsync"), QStringLiteral("mmap")
    };
    return syncEngines.contains(engine.trimmed().toLower());
}

namespace {
/// 运行时探测到的 IO 引擎。空串 = 还没探测过，用平台默认值。
QString g_ioEngineOverride;
} // namespace

QString FioConfig::defaultIoEngine()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windowsaio");
#else
    // Linux 上 direct=1 走的是 O_DIRECT，libaio / io_uring / psync 都支持。
    // 默认 libaio：只有它这个异步引擎能让 iodepth 真正生效。
    return QStringLiteral("libaio");
#endif
}

QStringList FioConfig::candidateIoEngines()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("windowsaio"), QStringLiteral("psync")};
#else
    // posixaio 排在 psync 前面：它同样是同步接口，但至少走的是 aio 那套
    // 语义，在少数没有 libaio/io_uring 的 fio 构建里比 psync 更接近原意。
    return {QStringLiteral("libaio"), QStringLiteral("io_uring"),
            QStringLiteral("posixaio"), QStringLiteral("psync")};
#endif
}

QString FioConfig::ioEngine()
{
    return g_ioEngineOverride.isEmpty() ? defaultIoEngine() : g_ioEngineOverride;
}

void FioConfig::setIoEngine(const QString &engine)
{
    g_ioEngineOverride = engine.trimmed().toLower();
}

QString FioConfig::testFolder() const
{
    // 目标是一个**目录**，测试文件建在它下面的 ml_disktest 里。
    // 传挂载点（"/"、"C:/"）也照样成立 —— 挂载点本身也是目录。
    QString dir = targetDir.trimmed();
    if (dir.isEmpty())
        return QString();
    if (!dir.endsWith(QLatin1Char('/')) && !dir.endsWith(QLatin1Char('\\')))
        dir += QLatin1Char('/');
    return QDir::cleanPath(dir + folderName());
}

QString FioConfig::testFilePattern() const
{
    return fileBaseName() + QStringLiteral("_*");
}

qint64 FioConfig::reserveBytes(quint64 volumeTotal)
{
    // 不能真写到 0 字节：卷上还需要空间给 MFT、其它程序临时文件和系统本身。
    // 取 2%，夹在 256 MiB ~ 2 GiB 之间。
    const qint64 twoPercent = qint64(volumeTotal / 50);
    return qBound<qint64>(256LL * 1024 * 1024, twoPercent, 2LL * 1024 * 1024 * 1024);
}

FioConfig::FilePlan FioConfig::planFiles(quint64 volumeTotal, quint64 volumeFree) const
{
    FilePlan plan;

    qint64 target = qint64(volumeFree) - reserveBytes(volumeTotal);
    if (fillLimitMiB > 0)
        target = qMin<qint64>(target, qint64(fillLimitMiB) * 1024 * 1024);
    if (target <= 0)
        return plan;

    // 文件数取"用 2 GiB 装完需要几个"，再把总量均分回去。
    // 这样每个文件都 ≤ 2 GiB，而且不会剩下一个零头文件（fio 的 filename_format
    // 不支持给单个文件指定不同大小，均分是最省事的做法）。
    const qint64 cap = maxFileSize();
    qint64 count = (target + cap - 1) / cap;
    if (count < 1)
        count = 1;

    qint64 size = target / count;   // 向下取整，保证总量不超过可用空间
    if (size <= 0)
        return plan;
    if (size > cap)
        size = cap;

    plan.fileCount  = int(count);
    plan.fileSize   = size;
    plan.totalBytes = size * count;
    return plan;
}

FioConfig::FilePlan FioConfig::planFilesForSize(qint64 bytes)
{
    FilePlan plan;
    if (bytes <= 0)
        return plan;

    // 性能测试**固定单文件**：多文件会让顺序测试在文件边界处被打断，读数不稳。
    // 代价是 FAT32 上放不下 > 4 GiB 的文件 —— 那种情况在开测前就拦下来报错。
    plan.fileCount  = 1;
    plan.fileSize   = bytes;
    plan.totalBytes = bytes;
    return plan;
}

QString FioConfig::perfJobFileText(PerfItem item, bool writePass, int fileCount, qint64 fileSize) const
{
    QStringList lines;
    lines << QStringLiteral("[global]");
    lines << QStringLiteral("ioengine=%1").arg(ioEngine());
    lines << QStringLiteral("direct=1");
    lines << QStringLiteral("group_reporting=1");
    lines << QStringLiteral("bs=%1").arg(perfItemBlockSize(item));
    lines << QStringLiteral("iodepth=%1").arg(perfItemIodepth(item));
    // 读和写**分两趟**，一趟只做一个方向。
    // 不能用 rw=readwrite/randrw 混跑：读写抢同一段位置，实测会互相打断，
    // 4M 的顺序项只能跑到 8 MiB/s（r=1,w=1 IOPS），完全不是盘的吞吐能力。
    const QString base = perfItemIsRandom(item) ? QStringLiteral("rand") : QString();
    lines << QStringLiteral("rw=%1%2").arg(base, writePass ? QStringLiteral("write")
                                                          : QStringLiteral("read"));
    // 固定时长；文件已经预先建好，fio 不会走布局
    lines << QStringLiteral("time_based=1");
    // ramp_time=1 + runtime=10 → fio 实际跑 11 秒，但它**报告的口径正好是后 10 秒**
    // （即第 2 秒 ~ 第 11 秒），和我们采样的窗口完全重合：
    // 表格里的数字和曲线看到的是同一段，不会一个含预热门槛、一个不含。
    lines << QStringLiteral("ramp_time=1");
    lines << QStringLiteral("runtime=%1").arg(perfRuntimeSeconds());
    if (!writePass)
        lines << QStringLiteral("invalidate=1");   // 读之前丢掉缓存，避免读到内存里的数据
    lines << QStringLiteral("nrfiles=%1").arg(fileCount);
    lines << QStringLiteral("filesize=%1").arg(fileSize);
    lines << QStringLiteral("filename_format=%1_$filenum").arg(fileBaseName());
    lines << QStringLiteral("size=%1").arg(fileSize * fileCount);
    lines << QString();
    lines << QStringLiteral("[perf]");
    lines << QString();
    return lines.join(QLatin1Char('\n'));
}

QString FioConfig::writeJobFileText(const FilePlan &plan) const
{
    QStringList lines;
    lines << QStringLiteral("[global]");
    lines << QStringLiteral("ioengine=%1").arg(ioEngine());
    lines << QStringLiteral("direct=1");   // 固定直接 I/O
    lines << QStringLiteral("group_reporting=1");
    lines << QStringLiteral("bs=%1").arg(blockSize());
    lines << QStringLiteral("iodepth=%1").arg(iodepth);
    lines << QStringLiteral("rw=%1").arg(writeRw());
    // create_on_open：文件在任务真正打开时才创建，fio **不做布局**。
    // 这一步很关键：默认 fio 会先把文件布局写一遍、任务再写一遍（双倍时间）；
    // 而我们自己预分配文件又很慢（G: 上实测 2 GB 要 8.9 秒，占整个测试时间的四成），
    // create_on_open 让"直接写一遍"成为可能，既省掉布局也省掉预分配。
    lines << QStringLiteral("create_on_open=1");
    lines << QStringLiteral("nrfiles=%1").arg(plan.fileCount);
    lines << QStringLiteral("filesize=%1").arg(plan.fileSize);
    lines << QStringLiteral("filename_format=%1_$filenum").arg(fileBaseName());
    lines << QStringLiteral("size=%1").arg(plan.totalBytes);
    lines << QString();
    lines << QStringLiteral("[diskcheck]");
    lines << QString();
    return lines.join(QLatin1Char('\n'));
}

QString FioConfig::readJobFileText(int fileCount, qint64 totalBytes) const
{
    QStringList lines;
    lines << QStringLiteral("[global]");
    lines << QStringLiteral("ioengine=%1").arg(ioEngine());
    lines << QStringLiteral("direct=1");   // 固定直接 I/O
    lines << QStringLiteral("group_reporting=1");
    lines << QStringLiteral("bs=%1").arg(blockSize());
    lines << QStringLiteral("iodepth=%1").arg(iodepth);
    lines << QStringLiteral("rw=%1").arg(readRw());
    // 读阶段不给 filesize：文件已经在那儿了，fio 按实际大小读。
    lines << QStringLiteral("nrfiles=%1").arg(fileCount);
    lines << QStringLiteral("filename_format=%1_$filenum").arg(fileBaseName());
    lines << QStringLiteral("size=%1").arg(totalBytes);
    lines << QString();
    lines << QStringLiteral("[diskcheck]");
    lines << QString();
    return lines.join(QLatin1Char('\n'));
}

QStringList FioConfig::commonExtraArguments() const
{
    // eta / eta-newline 只能出现在命令行上（写进 job 文件会被判 Bad option）
    QStringList args;
    args << QStringLiteral("--thread");
    args << QStringLiteral("--eta=always");
    args << QStringLiteral("--eta-newline=1");
    return args;
}

QStringList FioConfig::writeExtraArguments() const
{
    QStringList args = commonExtraArguments();
    if (verify) {
        // 写进校验头，但**不做即时校验**：
        // 默认 fio 会在写完每一块后立刻读回来校验，那会把"写入阶段"变成读写混合，
        // 也会让写入速度腰斩。校验留到读取阶段统一做。
        args << QStringLiteral("--verify=crc32c");
        args << QStringLiteral("--do_verify=0");
    }
    return args;
}

QStringList FioConfig::readExtraArguments() const
{
    QStringList args = commonExtraArguments();
    if (verify) {
        args << QStringLiteral("--verify=crc32c");
        // **遇到校验失败不要中断**：fio 默认在第一次校验失败时就结束这个 job，
        // 结果是一块坏块就终止整轮读取，其余没读到的区域是好是坏完全不知道。
        // 可靠性测试要的恰恰是"扫完整个分区、有几块坏"，所以让它继续读完。
        //
        // verify 是 continue_on_error 的合法取值之一：
        //   none  = 任何 I/O 或校验错误都退出（fio 的默认）
        //   verify= 只对校验错误继续，其它错误照旧退出
        // 选 verify 而不是 all：真正的 I/O 错误（掉盘、超时）仍然应该立刻停。
        args << QStringLiteral("--continue_on_error=verify");
    }
    return args;
}

bool FioConfig::validate(QString *error) const
{
    const auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    if (targetDir.trimmed().isEmpty())
        return fail(kErrNeedDrive);

    const QString folder = testFolder();
    if (folder.isEmpty())
        return fail(kErrNoTestFolder);

    if (testMode == TestMode::Performance) {
        bool any = false;
        for (bool checked : perfItemChecked)
            any = any || checked;
        if (!any)
            return fail(kErrPerfNoItem);
        if (perfDataBytes <= 0)
            return fail(kErrPerfDataSize);
    } else if (!doWrite && !doRead) {
        return fail(kErrNeedOnePhase);
    }

    if (iodepth < 1)
        return fail(kErrIodepthTooSmall);
    if (rounds < 1)
        return fail(kErrRoundsTooSmall);

    const QFileInfo targetInfo(targetDir);
    if (!targetInfo.exists() || !targetInfo.isDir())
        return fail(kErrTargetUnavailable.arg(targetDir));

    return true;
}

QString FioConfig::describe() const
{
    // 引擎与 direct 都是固定的，写在这里让日志和自检报告能看清实际用了什么
    if (testMode == TestMode::Performance) {
        return QStringLiteral("%1  数据 %2  %3 直接IO  每项 %4 秒")
            .arg(testModeText(), perfSizeText(perfDataBytes), ioEngine())
            .arg(perfRuntimeSeconds());
    }
    return kConfigDescribeFmt
        .arg(modeText(), ioEngine(), blockSize())
        .arg(iodepth)
        .arg(verify ? kVerifyOn : kVerifyOff);
}
