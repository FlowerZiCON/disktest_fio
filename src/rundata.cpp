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

#include "rundata.h"

#include "fioconfig.h"
#include "uitext.h"   // 版本过旧之类的用户可见文案

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

/// 采样行里 "item/pass" 两列没写（或写坏了）时的取值。
constexpr int kUnknownIndex = -1;

/// 老文件用 axis_max_GB，新文件用 axis_max_GiB；值都是 GiB。
const char *kAxisKeys[] = {"axis_max_GiB", "axis_max_GB"};

/// 参数段里出现这些键说明这是一份性能测试数据。
/// 只在没有 test_mode 的老文件里用作兜底判据。
const char *kPerfOnlyKeys[] = {"perf_items", "perf_data_bytes", "perf_size_MiB",
                               "perf_size", "perf_item_count"};

bool hasPerfOnlyKey(const QHash<QString, QString> &params)
{
    for (const char *key : kPerfOnlyKeys) {
        if (params.contains(QString::fromLatin1(key)))
            return true;
    }
    return false;
}

/// 采样段的列数：9 列（末尾 item,pass）。
/// 版本门槛已经把 2.2.7 之前的文件挡在外面了，所以这里只认这一种列数，
/// 不再保留"7 列老格式"的兼容分支——那些文件根本走不到这里。
constexpr int kSampleColumns = 9;

/// 单份结果文件最多接受多少行采样。
///
/// 结果文件是**不可信输入**（可能是别人给的、可能是坏掉的一半文件）。
/// 没有上限的话，一个几 GB 的文件会把 5 个 QVector<double> 撑到几 GB 内存。
/// 实测一份 1 TB 分区的可靠性测试也就一万多拍，100 万行远远够用；
/// 超出的行会被计入 droppedSampleCount() 并在界面上说出来，不静默丢。
constexpr int kMaxSampleRows = 1000 * 1000;

/// 解析采样行里的一个数值字段。
///
/// 必须**整段都是数字**，而且结果是有限值：
///   * 不检查 ok 的话，"abc" 会静默变成 0，一行坏数据被当成有效采样；
///   * Qt 的 toDouble 认 "nan" / "inf"（ok = true），放进来会污染平均值、
///     让曲线和汇总出现 nan；
///   * 小数逗号（"1,5"）会让整行的列位置整体错开，那种行只有靠"列数必须
///     正好等于 kSampleColumns"才能挡住（见 parseStream）。
bool fieldToDouble(const QString &text, double *out)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return false;
    *out = value;
    return true;
}

/// 同上，整数列。
bool fieldToInt(const QString &text, int *out)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok)
        return false;
    *out = value;
    return true;
}

} // namespace

int RunData::compareVersions(const QString &a, const QString &b)
{
    // 结果文件是不可信输入：版本号里出现任何怪东西都不能抛异常、不能崩。
    // 所以纯按数字段比较，认不出来的一律当 0。
    const auto parts = [](const QString &text) {
        QVector<int> out;
        for (const QString &seg : text.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
            // "7-beta" 这种只取前导数字；取不到就算 0。
            // **累加要封顶**：版本号来自文件，"99999999999999999999.0.0" 这种
            // 20 位段位会把 int 乘溢出（有符号溢出是 UB，编译器完全可以
            // 据此做出任意假设）。到 INT_MAX 就不再往上加了。
            int value = 0;
            int digits = 0;
            for (const QChar c : seg) {
                if (!c.isDigit())
                    break;
                if (value > (std::numeric_limits<int>::max() - 9) / 10) {
                    value = std::numeric_limits<int>::max();
                    break;
                }
                value = value * 10 + c.digitValue();
                ++digits;
            }
            out << (digits > 0 ? value : 0);
        }
        return out;
    };

    const QVector<int> va = parts(a);
    const QVector<int> vb = parts(b);
    const int count = qMax(va.size(), vb.size());
    for (int i = 0; i < count; ++i) {
        // 段数不齐时缺的按 0 算，所以 "2.2" == "2.2.0"、但 "2.2" < "2.2.1"
        const int x = va.value(i, 0);
        const int y = vb.value(i, 0);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

QString RunData::formatSpeed(double mib)
{
    if (mib < 0.0)
        return QStringLiteral("--");
    if (mib >= 1000.0)
        return QStringLiteral("%1 GiB/s").arg(mib / 1024.0, 0, 'f', 2);
    if (mib >= 10.0)
        return QStringLiteral("%1 MiB/s").arg(mib, 0, 'f', 1);
    return QStringLiteral("%1 MiB/s").arg(mib, 0, 'f', 2);
}

QString RunData::formatBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(qMax<qint64>(0, bytes));
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', unit == 0 ? 0 : 1)
        .arg(QString::fromLatin1(units[unit]));
}

QString RunData::formatDuration(qint64 seconds)
{
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

double RunData::paramDouble(const QString &key, double fallback) const
{
    if (!params.contains(key))
        return fallback;
    bool ok = false;
    const double value = params.value(key).toDouble(&ok);
    return (ok && std::isfinite(value)) ? value : fallback;
}

double RunData::perfRuntimeSeconds() const
{
    // 每项跑多少秒是**写这份结果时**决定的，必须跟着文件走。
    // 以前读侧直接写死 FioConfig::perfRuntimeSeconds()：一份每项 30 秒的结果
    // 会被标成"每项固定 10 秒"，横轴也被裁到 10 秒，后 20 秒的曲线直接看不见。
    const double recorded = paramDouble(QStringLiteral("perf_runtime_sec"), 0.0);
    if (recorded > 0.0 && recorded <= 24.0 * 3600.0)
        return recorded;
    return double(FioConfig::perfRuntimeSeconds());
}

bool RunData::prefersLowDensity() const
{
    const QString recorded = param(QStringLiteral("density"));
    if (recorded == QLatin1String("low"))
        return true;
    if (recorded == QLatin1String("high"))
        return false;
    // 没有 density（2.2.7 那会儿还没有这一项）：line_step > 1 说明当年抽过稀，
    // 那就照用户当初看到的样子画。
    return paramDouble(QStringLiteral("line_step"), 0.0) > 1.0;
}

double RunData::verifyAxisXGiB(int fileIndex, qint64 fileOffset,
                               const QVector<qint64> &fileSizes, qint64 fallbackFileSize)
{
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    if (fileOffset < 0)
        return -1.0;

    // 文件序号来自不可信文件里的 verify_file_index，必须夹住：
    // 不夹的话 `fileSizes.last() * (index - size)` 会乘溢出（有符号溢出，UB），
    // 算出来的横坐标是垃圾，图上会画出一条位置毫无意义的色带。
    // 上限取"已知文件数 + 一点点"，因为 fio 报的序号理论上不该超出它。
    const int maxIndex = fileSizes.isEmpty() ? 4096 : int(fileSizes.size()) + 8;
    const int index = qBound(0, fileIndex, maxIndex);

    // 全局字节 = Σ(序号之前的文件) + 文件内偏移；逐步饱和，不让它溢出。
    const qint64 kLimit = std::numeric_limits<qint64>::max() / 4;
    qint64 before = 0;
    if (!fileSizes.isEmpty()) {
        for (int i = 0; i < index && i < fileSizes.size(); ++i) {
            const qint64 size = qMax<qint64>(0, fileSizes.at(i));
            before = (size > kLimit - before) ? kLimit : before + size;
        }
        // 序号超出已知文件数（理论上不会）：后面的按最后那个文件的大小顺延
        if (index > fileSizes.size()) {
            const qint64 last = qMax<qint64>(0, fileSizes.last());
            const qint64 extra = (last > kLimit / qMax(1, index - fileSizes.size()))
                                     ? kLimit
                                     : last * (index - fileSizes.size());
            before = (extra > kLimit - before) ? kLimit : before + extra;
        }
    } else if (fallbackFileSize > 0) {
        // 没有逐文件大小（老文件只有 file_size_bytes）：只能按统一大小算
        const qint64 index64 = index;
        before = (fallbackFileSize > kLimit / qMax<qint64>(1, index64))
                     ? kLimit
                     : fallbackFileSize * index64;
    }
    const qint64 total = (fileOffset > kLimit - before) ? kLimit : before + fileOffset;
    return double(total) / kGiB;
}

double RunData::axisMaxGiB() const
{
    // 1) 数据本身要求的下限：横轴至少要包住所有采样点。
    //    两种模式都算：横坐标在可靠性里是"已处理容量"，在性能里是"秒"，
    //    但"文件里记的上限必须包住数据"这条要求是一样的。
    double maxX = 0.0;
    for (double x : readX)
        maxX = qMax(maxX, x);
    for (double x : writeX)
        maxX = qMax(maxX, x);

    // 2) 文件里记的上限。**只要它能包住数据就照用**（那是当初画图时的真实值，
    //    回看要和当初一致）；只有缺失、非有限值、或者包不住数据时才按数据补。
    //    以前无条件信文件里的值：缺这一项就退回 1.0 GiB，于是 58 GiB 的曲线
    //    被裁到只剩前 1 GiB，而界面上什么都不说。
    for (const char *key : kAxisKeys) {
        const QString k = QString::fromLatin1(key);
        if (!params.contains(k))
            continue;
        bool ok = false;
        const double value = params.value(k).toDouble(&ok);
        if (!ok || !std::isfinite(value) || value <= 0.0)
            continue;
        if (value >= maxX)
            return value;
        break;   // 记了但包不住数据：往下走去按数据算
    }

    if (maxX > 0.0)
        return maxX * 1.02;
    return 1.0;
}

double RunData::averageReadMiBps() const
{
    // 平均值**优先取文件里记录的"分阶段"平均值**，不能拿全部采样点重算：
    // 写入阶段和读取阶段的速度能差两三倍，混在一起平均出来的数没有意义，
    // 也会和保存时卡片上显示的数字对不上。
    //   平均读取 = 读取阶段的读均速(read_avg_read_MiBps)
    //   平均写入 = 写入阶段的写均速(write_avg_write_MiBps)
    const double recorded = paramDouble(QStringLiteral("read_avg_read_MiBps"), -1.0);
    if (recorded >= 0.0)
        return recorded;

    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < readV.size(); ++i) {
        if (flags.value(i, 0) & 1) {
            sum += readV.at(i);
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

double RunData::averageWriteMiBps() const
{
    const double recorded = paramDouble(QStringLiteral("write_avg_write_MiBps"), -1.0);
    if (recorded >= 0.0)
        return recorded;

    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < writeV.size(); ++i) {
        if (flags.value(i, 0) & 2) {
            sum += writeV.at(i);
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

double RunData::elapsedSeconds() const
{
    double total = 0.0;
    for (double d : durations)
        total += d;
    const double recorded =
        paramDouble(QStringLiteral("write_elapsed_sec"), 0.0)
        + paramDouble(QStringLiteral("read_elapsed_sec"), 0.0);
    return recorded > 0.0 ? recorded : total;
}

QVector<int> RunData::perfItemsPresent() const
{
    QVector<int> found;
    for (int item : items) {
        if (item >= 0 && item < 4 && !found.contains(item))
            found << item;
    }
    std::sort(found.begin(), found.end());
    return found;
}

QVector<RunData::Trace> RunData::perfTraces() const
{
    const int itemCount = FioConfig::allPerfItems().size();
    QVector<Trace> traces(qMax(1, itemCount));

    for (int i = 0; i < readV.size(); ++i) {
        const int item = items.value(i, kUnknownIndex);
        if (item < 0 || item >= traces.size())
            continue;
        const int pass = passes.value(i, kUnknownIndex);
        const quint8 f = flags.value(i, 0);
        Trace &trace = traces[item];
        // pass 只认 0（读取趟）/ 1（写入趟）。**认不出来时退回按 has_read /
        // has_write 分流，不能把这一拍直接丢掉** —— 那种行里明明有数据
        // （f 的对应位是 1），静默丢掉就是平白少画一段曲线。
        if (pass == 0) {
            if (f & 1)
                trace.read.append(QPointF(readX.value(i, 0.0), readV.at(i)));
        } else if (pass == 1) {
            if (f & 2)
                trace.write.append(QPointF(writeX.value(i, 0.0), writeV.at(i)));
        } else {
            if (f & 1)
                trace.read.append(QPointF(readX.value(i, 0.0), readV.at(i)));
            if (f & 2)
                trace.write.append(QPointF(writeX.value(i, 0.0), writeV.at(i)));
        }
    }

    // 每一趟的**结果值**优先用文件里记录的整趟均值（<item>_read_MiBps /
    // <item>_write_MiBps）——那才是"测出来多少"，和实时界面上格子里的数字一致。
    // 老文件没有这些参数时才退回取轨迹最大值（量级对得上，但不是同一个数）。
    const QVector<FioConfig::PerfItem> allItems = FioConfig::allPerfItems();
    for (int i = 0; i < traces.size() && i < allItems.size(); ++i) {
        const QString key = FioConfig::perfItemKey(allItems.at(i));
        const double readAvg = paramDouble(key + QStringLiteral("_read_MiBps"), -1.0);
        const double writeAvg = paramDouble(key + QStringLiteral("_write_MiBps"), -1.0);
        for (const QPointF &p : traces.at(i).read)
            traces[i].readValue = qMax(traces[i].readValue, p.y());
        for (const QPointF &p : traces.at(i).write)
            traces[i].writeValue = qMax(traces[i].writeValue, p.y());
        if (readAvg >= 0.0)
            traces[i].readValue = readAvg;
        if (writeAvg >= 0.0)
            traces[i].writeValue = writeAvg;
    }
    return traces;
}

/// 真正的解析：从已经打开的流里读。parse() 与解析器自检都走这条路，
/// 保证"自检验证的规则"和"界面实际用的规则"是同一份实现。
static bool parseStream(QTextStream &in, const QString &path, RunData *data, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    RunData parsed;
    parsed.path = path;

    bool magicOk = false;
    bool inSamples = false;
    bool headerSkipped = false;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('#'))) {
            if (line.contains(QLatin1String(RunData::magic())))
                magicOk = true;
            if (line.contains(QLatin1String(RunData::samplesMarker())))
                inSamples = true;
            continue;
        }
        if (!inSamples) {
            // 参数行按**第一个**逗号切分，值里再有逗号也不影响
            const int comma = line.indexOf(QLatin1Char(','));
            if (comma > 0)
                parsed.params.insert(line.left(comma).trimmed(),
                                     line.mid(comma + 1).trimmed());
            continue;
        }
        if (!headerSkipped) {
            headerSkipped = true;   // 表头行只用来跳过，列名不参与解析
            continue;
        }
        const QStringList f = line.split(QLatin1Char(','));
        // 列数必须**正好** 9 列。少了说明这行被截断；多了说明值里混进了逗号
        // （欧洲小数写法 "1,5" 就是这种），两种都不能按列位置硬读 ——
        // 多一列时按位置读会把整行数据**整体错位**，却照样"解析成功"。
        if (f.size() != kSampleColumns) {
            ++parsed.m_droppedSamples;
            continue;
        }
        if (parsed.readV.size() >= kMaxSampleRows) {
            ++parsed.m_droppedSamples;   // 超上限的行不再收，也不静默
            continue;
        }

        double readX = 0.0;
        double readV = 0.0;
        double writeX = 0.0;
        double writeV = 0.0;
        double duration = 0.0;
        int hasRead = 0;
        int hasWrite = 0;
        int item = kUnknownIndex;
        int pass = kUnknownIndex;
        // 任何一列解析不出来就整行丢掉并计数：静默当 0 会让一行坏数据
        // 变成"一个速度很低的采样点"，悄悄拉低平均值、骗过曲线。
        if (!fieldToDouble(f.at(0), &readX) || !fieldToDouble(f.at(1), &readV)
            || !fieldToInt(f.at(2), &hasRead) || !fieldToDouble(f.at(3), &writeX)
            || !fieldToDouble(f.at(4), &writeV) || !fieldToInt(f.at(5), &hasWrite)
            || !fieldToDouble(f.at(6), &duration) || !fieldToInt(f.at(7), &item)
            || !fieldToInt(f.at(8), &pass)) {
            ++parsed.m_droppedSamples;
            continue;
        }

        parsed.readX << readX;
        parsed.readV << readV;
        parsed.writeX << writeX;
        parsed.writeV << writeV;
        parsed.durations << duration;
        parsed.flags << quint8((hasRead != 0 ? 1 : 0) | (hasWrite != 0 ? 2 : 0));
        parsed.items << item;
        parsed.passes << pass;
    }

    // ---- 文件类型标记 ----
    // 只查这一条就够：没有标记的文件连参数段都不可信。
    if (!magicOk) {
        return fail(kMsgBadCsvHeader.arg(QDir::toNativeSeparators(path),
                                         QString::fromLatin1(RunData::magic())));
    }

    // ---- 版本门槛 ----
    // **必须排在"有没有采样行"之前**：过旧的文件（7 列采样）在采样循环里
    // 会被整段当成坏行丢掉，如果先判"一个采样行都没有"，用户看到的会是
    // "第一行格式不对"，而真正的原因是版本过旧 —— 提示会把人引向错误的方向。
    //
    // 为什么干脆不支持老文件：这个格式在 2.2.7 里改动很大（test_mode、item/pass、
    // 坏块信息、逐文件大小、file_sizes 都是新加的）。老文件缺字段时只能靠猜，
    // 猜出来的图看着正常、位置和内容却是错的——那比明说"不支持"更坑人。
    // 没有 app_version 的文件 = 2.2.7 之前存的，同样在拒收之列。
    parsed.appVersion = parsed.params.value(RunData::appVersionKey()).trimmed();
    const QString minimum = RunData::minimumReadableVersion();
    if (parsed.appVersion.isEmpty()) {
        return fail(kErrRunDataTooOldNoVersion.arg(
            QDir::toNativeSeparators(path), minimum));
    }
    if (RunData::compareVersions(parsed.appVersion, minimum) < 0) {
        return fail(kErrRunDataTooOld.arg(QDir::toNativeSeparators(path),
                                          parsed.appVersion, minimum));
    }

    // 到这里格式和版本都对了：一个采样行都没解析出来，说明数据段本身是空的
    // 或者整段坏掉了。这和"第一行不是标记"是两件事，必须分开说。
    if (parsed.readV.isEmpty()) {
        return fail(kErrRunDataNoSamples.arg(QDir::toNativeSeparators(path))
                        .arg(parsed.m_droppedSamples));
    }

    // ---- 模式判别 ----
    // 顺序：显式 test_mode → 性能专属键 → 其余按可靠性（老文件全是可靠性）。
    const QString declared = parsed.params.value(RunData::testModeKey()).trimmed().toLower();
    if (declared == QLatin1String("performance")) {
        parsed.mode = RunData::Mode::Performance;
        parsed.modeExplicit = true;
    } else if (declared == QLatin1String("reliability")) {
        parsed.mode = RunData::Mode::Reliability;
        parsed.modeExplicit = true;
    } else if (hasPerfOnlyKey(parsed.params)) {
        parsed.mode = RunData::Mode::Performance;
    } else {
        parsed.mode = RunData::Mode::Reliability;
    }

    // ---- 坏块信息 ----
    // 老文件没有这几项，读出来就是"0 处、没有明细"，也就是"这块盘当时没发现坏块"，
    // 界面按"没有坏块信息"处理，不显示那一行。
    //
    // 但"写了却读不出来"和"没写"是两回事：前者是文件坏了，必须记成**未知（-1）**。
    // 以前两条路都落到 0，而 0 的含义是"没发现不一致" —— 一个坏掉的字段就足以
    // 把"这块盘坏了"这个结论抹掉。
    {
        const QString text = parsed.params.value(QStringLiteral("verify_failures")).trimmed();
        bool ok = false;
        const qlonglong value = text.toLongLong(&ok);
        if (ok && value >= 0 && value <= std::numeric_limits<int>::max())
            parsed.m_verifyFailures = int(value);
        else if (text.isEmpty())
            parsed.m_verifyFailures = 0;    // 没有这一项 = 没记录到坏块
        else
            parsed.m_verifyFailures = -1;   // 有这一项但读不出来 = 未知
    }
    // 逐处的原话和偏移是两条用 | 分隔的并列列表：**顺序一一对应**。
    // 用 | 而不是逗号，是因为原话里本来就有逗号（fio 的完整输出很长）。
    const auto splitList = [](const QString &text) {
        QStringList list;
        for (const QString &part : text.split(QLatin1Char('|'), Qt::SkipEmptyParts))
            list << part.trimmed();
        return list;
    };
    parsed.m_verifyTexts = splitList(parsed.params.value(QStringLiteral("verify_text")));
    for (const QString &part : splitList(parsed.params.value(QStringLiteral("verify_offsets")))) {
        bool ok = false;
        const qint64 value = part.toLongLong(&ok);
        parsed.m_verifyOffsets << (ok ? value : qint64(-1));
    }
    // 单文件大小：本程序写的是整数（QString::number(qint64)），工具造的文件也
    // 可能是整数字符串。以前走 toDouble 再转 qint64：天文数字或 NaN 是 UB。
    {
        const QString text = parsed.params.value(QStringLiteral("file_size_bytes")).trimmed();
        bool ok = false;
        qlonglong value = text.toLongLong(&ok);
        if (!ok) {
            // 容忍 "1073741824.0" 这种写法，但仍然要夹住范围
            const double d = text.toDouble(&ok);
            if (ok && std::isfinite(d) && d > 0.0 && d < 9.0e18)
                value = qlonglong(d);
        }
        parsed.m_fileSizeBytes = (ok && value > 0) ? qint64(value) : 0;
    }
    // 逐文件大小：坏块位置的换算基准。只读校验时盘上现成的文件不一定等大，
    // 所以优先用这份列表，没有（老文件）才退回统一的 file_size_bytes。
    //
    // 注意它和下面两条**分隔符不一样**：这里是数字列表，用空格分就够了；
    // verify_text 里是 fio 的原话、自带逗号，只能用 | 分。
    // 一开始这里错用了 splitList（按 | 切），整串数字切不开、一个都解析不出来，
    // 结果逐文件大小为空 → 位置全按"第一个文件"算 → 坏块又挤回图的最前面。
    for (const QString &part : parsed.params.value(QStringLiteral("file_sizes"))
                                   .split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool ok = false;
        const qint64 value = part.trimmed().toLongLong(&ok);
        if (ok)
            parsed.m_fileSizes << value;
    }
    // 文件序号：老文件没有这一列，那就全按 0（第一个文件）处理。
    // 宁可位置偏，也不要因为缺一列就整份文件的坏块都不显示。
    for (const QString &part : splitList(parsed.params.value(QStringLiteral("verify_file_index")))) {
        bool ok = false;
        const int value = part.toInt(&ok);
        parsed.m_verifyFileIndexes << (ok ? value : 0);
    }
    while (parsed.m_verifyFileIndexes.size() < parsed.m_verifyOffsets.size())
        parsed.m_verifyFileIndexes << 0;

    // 现在就换算好"盘上位置"（GiB）：这一步要同时用到文件序号和文件大小，
    // 两者都是解析时才知道的，放到界面里去算容易漏掉一个。
    //
    // 换算只有一份实现（RunData::verifyAxisXGiB），主窗口实时换算也调它：
    // 以前这里和 MainWindow::verifyPositionToAxisX 各写了一遍，连溢出问题都两份。
    for (int i = 0; i < parsed.m_verifyOffsets.size(); ++i) {
        parsed.m_verifyAxisX << RunData::verifyAxisXGiB(parsed.m_verifyFileIndexes.value(i, 0),
                                                        parsed.m_verifyOffsets.at(i),
                                                        parsed.m_fileSizes,
                                                        parsed.m_fileSizeBytes);
    }

    *data = parsed;
    return true;
}

bool RunData::parse(const QString &path, RunData *data, QString *error)
{
    if (!data) {
        // 不能只返回 false：调用方会拿 *error 去弹对话框，空的 error
        // 会弹出一个内容空白的警告框。
        if (error)
            *error = kErrLoadDataFailed;
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QTextStream in(&file);
    const bool ok = parseStream(in, path, data, error);
    file.close();
    return ok;
}

bool RunData::runParserSelfTest(QStringList *lines)
{
    const auto text = [](const QStringList &rows) {
        return rows.join(QLatin1Char('\n')) + QLatin1Char('\n');
    };
    const QString header9 =
        QStringLiteral("read_GiB,read_MiBps,has_read,write_GiB,write_MiBps,has_write,duration_sec,item,pass");
    /// 随便一份"当前版本"的文件头，各用例按需再往里插参数行。
    const QString versionLine =
        QStringLiteral("app_version,") + RunData::minimumReadableVersion();

    bool allOk = true;
    const auto check = [&](const QString &name, bool ok, const QString &detail) {
        allOk = allOk && ok;
        if (lines)
            *lines << QStringLiteral("    %1 : %2 %3")
                          .arg(name, ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), detail);
    };
    const auto parseText = [](const QString &body, RunData *out) {
        QString buf = body;
        QTextStream stream(&buf);
        QString err;
        return parseStream(stream, QStringLiteral("mem"), out, &err);
    };

    // 1) 版本比较规则（纯函数，先单独验，后面几条都靠它把关）
    {
        const bool ok =
            compareVersions(QStringLiteral("2.2.7"), QStringLiteral("2.2.7")) == 0
            && compareVersions(QStringLiteral("2.2.6"), QStringLiteral("2.2.7")) < 0
            && compareVersions(QStringLiteral("2.3.0"), QStringLiteral("2.2.7")) > 0
            && compareVersions(QStringLiteral("2.2"), QStringLiteral("2.2.0")) == 0
            && compareVersions(QStringLiteral("2.2"), QStringLiteral("2.2.1")) < 0
            && compareVersions(QStringLiteral("10.0.0"), QStringLiteral("9.9.9")) > 0
            // 怪版本号不能崩、也不能被当成"够新"
            && compareVersions(QStringLiteral(""), QStringLiteral("2.2.7")) < 0
            && compareVersions(QStringLiteral("x.y.z"), QStringLiteral("2.2.7")) < 0
            && compareVersions(QStringLiteral("2.2.7-beta"), QStringLiteral("2.2.7")) == 0;
        check(QStringLiteral("版本号比较规则"), ok, QString());
    }

    // 2) 版本门槛：过旧 / 没写版本 一律拒收
    {
        const auto body = [&header9](const QString &versionLineText) {
            QStringList rows;
            rows << QStringLiteral("# ml_disktest-run-data;1");
            if (!versionLineText.isEmpty())
                rows << versionLineText;
            rows << QStringLiteral("test_mode,reliability")
                 << QStringLiteral("# [samples]") << header9
                 << QStringLiteral("0.000000,0.000,0,0.000000,100.000,1,0.500,0,1");
            return rows.join(QLatin1Char('\n')) + QLatin1Char('\n');
        };
        RunData d;
        check(QStringLiteral("当前版本可打开"),
              parseText(body(versionLine), &d), QString());
        check(QStringLiteral("低版本被拒（2.2.6）"),
              !parseText(body(QStringLiteral("app_version,2.2.6")), &d), QString());
        check(QStringLiteral("低版本被拒（2.0.0）"),
              !parseText(body(QStringLiteral("app_version,2.0.0")), &d), QString());
        check(QStringLiteral("没写版本的老文件被拒"),
              !parseText(body(QString()), &d), QString());
        check(QStringLiteral("更高版本可打开（2.3.0）"),
              parseText(body(QStringLiteral("app_version,2.3.0")), &d), QString());
    }

    // 3) 性能格式：显式 test_mode=performance，9 列采样，四项各有两趟
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,performance")
             << QStringLiteral("perf_data_bytes,1073741824")
             << QStringLiteral("# [samples]") << header9;
        for (int item = 0; item < 4; ++item) {
            for (int pass = 0; pass < 2; ++pass) {
                rows << QStringLiteral("%1,%2,%3,%1,%4,%5,0.500,%6,%7")
                            .arg(QString::number(pass * 0.5, 'f', 3))
                            .arg(pass == 0 ? 100.0 * (item + 1) : 0.0, 0, 'f', 3)
                            .arg(pass == 0 ? 1 : 0)
                            .arg(pass == 1 ? 50.0 * (item + 1) : 0.0, 0, 'f', 3)
                            .arg(pass == 1 ? 1 : 0)
                            .arg(item)
                            .arg(pass);
            }
        }
        RunData d;
        const bool ok = parseText(text(rows), &d);
        check(QStringLiteral("性能格式解析"), ok, QString());
        check(QStringLiteral("test_mode 显式识别"),
              d.mode == Mode::Performance && d.modeExplicit, QString());
        check(QStringLiteral("9 列采样行数"), d.sampleCount() == 8,
              QStringLiteral("(=%1)").arg(d.sampleCount()));
        check(QStringLiteral("分出 4 个测试项"), d.perfItemsPresent().size() == 4,
              QStringLiteral("(=%1)").arg(d.perfItemsPresent().size()));
        const QVector<Trace> traces = d.perfTraces();
        bool tracesOk = true;
        for (int i = 0; i < 4 && i < traces.size(); ++i) {
            tracesOk = tracesOk && traces.at(i).readValue == 100.0 * (i + 1)
                       && traces.at(i).writeValue == 50.0 * (i + 1)
                       && traces.at(i).read.size() == 1 && traces.at(i).write.size() == 1;
        }
        check(QStringLiteral("四项读写按 item/pass 归位"), tracesOk, QString());
    }

    // 4) 可靠性格式：分阶段平均值、横轴上限、已用时都取参数段记录的值
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,reliability")
             << QStringLiteral("axis_max_GiB,58.407598")
             << QStringLiteral("read_avg_read_MiBps,356.436")
             << QStringLiteral("write_avg_write_MiBps,180.775")
             << QStringLiteral("write_elapsed_sec,331.000")
             << QStringLiteral("read_elapsed_sec,167.511")
             << QStringLiteral("# [samples]") << header9
             << QStringLiteral("0.000000,0.000,0,0.000000,183.633,1,0.501,0,1")
             << QStringLiteral("0.000935,0.000,0,0.089844,216.000,1,0.500,0,1")
             << QStringLiteral("57.872028,348.884,1,0.000010,0.000,0,0.493,0,0");
        RunData d;
        const bool ok = parseText(text(rows), &d);
        check(QStringLiteral("可靠性格式解析"), ok, QString());
        check(QStringLiteral("可靠性模式识别"),
              d.mode == Mode::Reliability && d.modeExplicit, QString());
        check(QStringLiteral("采样行数"), d.sampleCount() == 3,
              QStringLiteral("(=%1)").arg(d.sampleCount()));
        check(QStringLiteral("可靠性样本带 item/pass 两列"),
              d.items.size() == 3 && d.passes.size() == 3
                  && d.items.at(0) == 0 && d.items.at(2) == 0
                  && d.passes.at(0) == 1 && d.passes.at(2) == 0,
              QStringLiteral("(item0=%1 pass0=%2)")
                  .arg(d.items.value(0)).arg(d.passes.value(0)));
        check(QStringLiteral("横轴上限"), qAbs(d.axisMaxGiB() - 58.407598) < 1e-6,
              QStringLiteral("(=%1)").arg(d.axisMaxGiB(), 0, 'f', 6));
        check(QStringLiteral("平均读取取分阶段值"), qAbs(d.averageReadMiBps() - 356.436) < 1e-6,
              QStringLiteral("(=%1)").arg(d.averageReadMiBps(), 0, 'f', 3));
        check(QStringLiteral("平均写入取分阶段值"), qAbs(d.averageWriteMiBps() - 180.775) < 1e-6,
              QStringLiteral("(=%1)").arg(d.averageWriteMiBps(), 0, 'f', 3));
        check(QStringLiteral("已用时取阶段耗时之和"),
              qAbs(d.elapsedSeconds() - (331.0 + 167.511)) < 1e-3,
              QStringLiteral("(=%1)").arg(d.elapsedSeconds(), 0, 'f', 3));
    }

    // 5) 没有坏块信息：必须是"0 处、无明细"，不能报错也不能凭空报红
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,reliability")
             << QStringLiteral("# [samples]") << header9
             << QStringLiteral("0.000000,0.000,0,0.000000,100.000,1,0.500,0,1");
        RunData d;
        const bool ok = parseText(text(rows), &d);
        check(QStringLiteral("无坏块信息不报错"),
              ok && d.verifyFailures() == 0 && d.verifyFailuresText().isEmpty()
                  && d.verifyOffsets().isEmpty() && d.verifyAxisX().isEmpty(),
              QString());
    }

    // 6) 坏块信息：块数 + 逐处原话 + 逐处偏移 + 文件序号 + 换算成横坐标
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,reliability")
             << QStringLiteral("file_size_bytes,2097152")   // 2 MiB
             << QStringLiteral("verify_failures,3")
             << QStringLiteral("verify_text,crc32c: verify failed at file testfile_0 offset 4096|"
                               "crc32c: verify failed at file testfile_3 offset 255852544|"
                               "crc32c: verify failed at file testfile_12 offset 1048576")
             << QStringLiteral("verify_offsets,4096|255852544|1048576")
             << QStringLiteral("verify_file_index,0|3|12")
             << QStringLiteral("# [samples]") << header9
             << QStringLiteral("0.000000,0.000,0,0.000000,100.000,1,0.500,0,1");
        RunData d;
        const bool ok = parseText(text(rows), &d);
        check(QStringLiteral("坏块信息解析"), ok, QString());
        check(QStringLiteral("坏块块数"), d.verifyFailures() == 3,
              QStringLiteral("(=%1)").arg(d.verifyFailures()));
        check(QStringLiteral("坏块逐处原话"), d.verifyFailuresText().size() == 3,
              QStringLiteral("(=%1)").arg(d.verifyFailuresText().size()));
        check(QStringLiteral("坏块逐处偏移"), d.verifyOffsets().size() == 3,
              QStringLiteral("(=%1)").arg(d.verifyOffsets().size()));
        check(QStringLiteral("坏块文件序号"), d.verifyFileIndexes().size() == 3
                  && d.verifyFileIndexes().at(0) == 0
                  && d.verifyFileIndexes().at(1) == 3
                  && d.verifyFileIndexes().at(2) == 12,
              QStringLiteral("(=%1)").arg(d.verifyFileIndexes().value(1)));
        check(QStringLiteral("单文件大小"), d.fileSizeBytes() == 2097152,
              QStringLiteral("(=%1)").arg(d.fileSizeBytes()));

        // 横坐标 = 文件序号 × 单文件大小 + 文件内偏移。
        // **序号必须参与**：fio 报的偏移每个文件都从 0 开始，
        // testfile_0 的 4096 和 testfile_3 的 255852544 是两个不同的盘上位置。
        //   序号0  偏移4096      → 4096
        //   序号3  偏移255852544 → 3*2097152+255852544 = 262144000
        //   序号12 偏移1048576   → 12*2097152+1048576 = 26214400
        const QVector<double> &axis = d.verifyAxisX();
        constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
        const bool axisOk = axis.size() == 3
            && qAbs(axis.at(0) - 4096.0 / kGiB) < 1e-9
            && qAbs(axis.at(1) - 262144000.0 / kGiB) < 1e-9
            && qAbs(axis.at(2) - 26214400.0 / kGiB) < 1e-9;
        check(QStringLiteral("坏块横坐标含文件序号"), axisOk,
              QStringLiteral("(第2处=%1 GiB，应为 %2)")
                  .arg(axis.size() > 1 ? axis.at(1) : -1.0, 0, 'f', 6)
                  .arg(262144000.0 / kGiB, 0, 'f', 6));
    }

    // 7) 逐文件大小（**空格分隔**）必须真的被解析出来。
    //    这里曾经错用按 | 切的逻辑，整串数字切不开、一个都读不到，
    //    于是位置全按"第一个文件"算，坏块又挤回图的最前面。
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,reliability")
             << QStringLiteral("file_sizes,1048576 2097152 4194304")   // 1+2+4 MiB
             << QStringLiteral("verify_failures,2")
             << QStringLiteral("verify_offsets,0|0")
             << QStringLiteral("verify_file_index,1|2")
             << QStringLiteral("# [samples]") << header9
             << QStringLiteral("0.000000,0.000,0,0.000000,100.000,1,0.500,0,1");
        RunData d;
        const bool ok = parseText(text(rows), &d);
        constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
        // 序号1（第2个文件）起点 = 第1个文件大小 = 1 MiB
        // 序号2（第3个文件）起点 = 1 MiB + 2 MiB = 3 MiB
        const QVector<double> &axis = d.verifyAxisX();
        const bool ok2 = d.fileSizes().size() == 3
            && axis.size() == 2
            && qAbs(axis.at(0) - 1048576.0 / kGiB) < 1e-12
            && qAbs(axis.at(1) - 3145728.0 / kGiB) < 1e-12;
        check(QStringLiteral("逐文件大小按空格解析并按累计偏移定位"), ok && ok2,
              QStringLiteral("(解析出 %1 个文件大小，第3处位置=%2 GiB)")
                  .arg(d.fileSizes().size())
                  .arg(axis.size() > 1 ? axis.at(1) : -1.0, 0, 'f', 6));
    }

    // 8) 损坏文件必须被拒（缺 magic / 没有采样点）
    {
        RunData d;
        const bool ok = parseText(QStringLiteral("test_mode,performance\n# [samples]\n")
                                  + header9 + QLatin1Char('\n'), &d);
        check(QStringLiteral("缺 magic 的文件被拒"), !ok, QString());
    }

    // 9) 坏采样行必须**整行丢掉并计数**，不能静默当成 0：
    //      * 列数不对（值里混了逗号，欧洲小数写法就是这种）
    //      * 字段不是数字
    //      * nan / inf（Qt 的 toDouble 认这两个词，会把它当合法采样收下）
    //    一行坏数据混进来，要么悄悄拉低平均值，要么让整条曲线出现 nan。
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,reliability")
             << QStringLiteral("# [samples]") << header9
             // 唯一一行好的：读 250 MiB/s
             << QStringLiteral("0.000000,250.000,1,0.000000,0.000,0,0.500,0,0")
             // 小数逗号 → 10 列，整行错位
             << QStringLiteral("1,5,250.000,1,0.000,0.000,0,0.500,0,0")
             // 不是数字
             << QStringLiteral("0.100000,abc,1,0.000000,0.000,0,0.500,0,0")
             // nan / inf
             << QStringLiteral("0.200000,nan,1,0.000000,0.000,0,0.500,0,0")
             << QStringLiteral("0.300000,inf,1,0.000000,0.000,0,0.500,0,0");
        RunData d;
        const bool ok = parseText(text(rows), &d);
        const double avg = d.averageReadMiBps();
        check(QStringLiteral("坏采样行被丢掉并计数"),
              ok && d.sampleCount() == 1 && d.droppedSampleCount() == 4,
              QStringLiteral("(收下 %1 行，丢掉 %2 行)")
                  .arg(d.sampleCount()).arg(d.droppedSampleCount()));
        check(QStringLiteral("坏行不污染平均值（不是 nan、仍为 250）"),
              std::isfinite(avg) && qAbs(avg - 250.0) < 1e-9,
              QStringLiteral("(平均=%1)").arg(avg, 0, 'f', 3));
    }

    // 10) verify_failures 读不出来必须是**未知**，不能变成"0 处"。
    //     0 的含义是"没发现不一致"——一个坏掉的字段就足以把"盘坏了"的结论抹掉。
    {
        const auto body = [&](const QString &verifyLine) {
            QStringList rows;
            rows << QStringLiteral("# ml_disktest-run-data;1")
                 << versionLine
                 << QStringLiteral("test_mode,reliability");
            if (!verifyLine.isEmpty())
                rows << verifyLine;
            rows << QStringLiteral("# [samples]") << header9
                 << QStringLiteral("0.000000,0.000,0,0.000000,100.000,1,0.500,0,0");
            return rows.join(QLatin1Char('\n')) + QLatin1Char('\n');
        };
        RunData noParam;
        RunData broken;
        RunData negative;
        const bool parsed = parseText(body(QString()), &noParam)
                            && parseText(body(QStringLiteral("verify_failures,abc")), &broken)
                            && parseText(body(QStringLiteral("verify_failures,-5")), &negative);
        check(QStringLiteral("没写坏块字段 = 0 处（已记录）"),
              parsed && noParam.verifyFailures() == 0 && noParam.verifyFailuresKnown(),
              QStringLiteral("(=%1)").arg(noParam.verifyFailures()));
        check(QStringLiteral("坏块字段损坏 = 未知，不是 0 处"),
              broken.verifyFailures() < 0 && !broken.verifyFailuresKnown(),
              QStringLiteral("(=%1)").arg(broken.verifyFailures()));
        check(QStringLiteral("负数坏块数 = 未知"),
              !negative.verifyFailuresKnown(), QString());
    }

    // 11) 坏块序号是天文数字时不能乘溢出（那是有符号溢出，UB）。
    //     旧实现在这里会算出垃圾甚至负数，图上的色带位置完全没有意义。
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,reliability")
             << QStringLiteral("file_sizes,1099511627776")          // 1 TiB
             << QStringLiteral("verify_failures,1")
             << QStringLiteral("verify_offsets,0")
             << QStringLiteral("verify_file_index,2147483647")      // 放大到离谱
             << QStringLiteral("# [samples]") << header9
             << QStringLiteral("0.000000,0.000,0,0.000000,100.000,1,0.500,0,0");
        RunData d;
        const bool ok = parseText(text(rows), &d);
        const double x = d.verifyAxisX().isEmpty() ? -1.0 : d.verifyAxisX().first();
        check(QStringLiteral("坏块序号被夹住、不溢出"),
              ok && std::isfinite(x) && x >= 0.0,
              QStringLiteral("(横坐标=%1 GiB)").arg(x, 0, 'f', 3));
    }

    // 12) 横轴上限缺失时必须**按数据自己算**，不能回落到 1.0 GiB ——
    //     那会把 58 GiB 的曲线裁到只剩前 1 GiB，而且什么都不说。
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,reliability")
             << QStringLiteral("# [samples]") << header9
             << QStringLiteral("58.000000,250.000,1,0.000000,0.000,0,0.500,0,0")
             << QStringLiteral("0.000000,0.000,0,58.000000,100.000,1,0.500,0,1");
        RunData d;
        const bool ok = parseText(text(rows), &d);
        check(QStringLiteral("横轴上限缺失时包住数据"),
              ok && d.axisMaxGiB() >= 58.0,
              QStringLiteral("(=%1 GiB)").arg(d.axisMaxGiB(), 0, 'f', 3));
    }

    // 13) 性能每项时长跟着文件走：一份每项 30 秒的结果不能被按 10 秒渲染
    {
        QStringList rows;
        rows << QStringLiteral("# ml_disktest-run-data;1")
             << versionLine
             << QStringLiteral("test_mode,performance")
             << QStringLiteral("perf_runtime_sec,30")
             << QStringLiteral("# [samples]") << header9
             << QStringLiteral("0.000000,100.000,1,0.000000,0.000,0,0.500,0,0");
        RunData d;
        RunData noParam;
        QStringList rowsNoParam;
        rowsNoParam << QStringLiteral("# ml_disktest-run-data;1")
                    << versionLine
                    << QStringLiteral("test_mode,performance")
                    << QStringLiteral("# [samples]") << header9
                    << QStringLiteral("0.000000,100.000,1,0.000000,0.000,0,0.500,0,0");
        const bool ok = parseText(text(rows), &d) && parseText(text(rowsNoParam), &noParam);
        check(QStringLiteral("每项时长取文件里的值（30 秒）"),
              ok && qAbs(d.perfRuntimeSeconds() - 30.0) < 1e-9,
              QStringLiteral("(=%1)").arg(d.perfRuntimeSeconds(), 0, 'f', 1));
        check(QStringLiteral("文件里没记时长才用默认 10 秒"),
              qAbs(noParam.perfRuntimeSeconds()
                   - double(FioConfig::perfRuntimeSeconds())) < 1e-9,
              QStringLiteral("(=%1)").arg(noParam.perfRuntimeSeconds(), 0, 'f', 1));
    }

    return allOk;
}
