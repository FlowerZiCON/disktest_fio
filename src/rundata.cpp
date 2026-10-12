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

} // namespace

int RunData::compareVersions(const QString &a, const QString &b)
{
    // 结果文件是不可信输入：版本号里出现任何怪东西都不能抛异常、不能崩。
    // 所以纯按数字段比较，认不出来的一律当 0。
    const auto parts = [](const QString &text) {
        QVector<int> out;
        for (const QString &seg : text.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
            // "7-beta" 这种只取前导数字；取不到就算 0
            int value = 0;
            int digits = 0;
            for (const QChar c : seg) {
                if (!c.isDigit())
                    break;
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

QString RunData::formatSeconds(double seconds)
{
    return QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
}

double RunData::paramDouble(const QString &key, double fallback) const
{
    if (!params.contains(key))
        return fallback;
    bool ok = false;
    const double value = params.value(key).toDouble(&ok);
    return ok ? value : fallback;
}

double RunData::axisMaxGiB() const
{
    for (const char *key : kAxisKeys) {
        const QString k = QString::fromLatin1(key);
        if (!params.contains(k))
            continue;
        bool ok = false;
        const double value = params.value(k).toDouble(&ok);
        if (ok && value > 0.0)
            return value;
    }
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
        if (pass >= 0) {
            // 有 pass 列：这一拍属于哪一趟是明确的。
            if (pass == 0 && (f & 1))
                trace.read.append(QPointF(readX.value(i, 0.0), readV.at(i)));
            else if (pass == 1 && (f & 2))
                trace.write.append(QPointF(writeX.value(i, 0.0), writeV.at(i)));
        } else {
            // 没有 pass 列：一场只跑一个方向，按 has_read/has_write 分流就分得开。
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
        // 列数不够的行直接跳过，不能让一行坏数据把后面的列位置错开。
        // （版本门槛已经保证不存在 7 列的老文件，所以只有这一种格式。）
        if (f.size() < kSampleColumns)
            continue;
        parsed.readX << f.at(0).toDouble();
        parsed.readV << f.at(1).toDouble();
        const quint8 hasRead = (f.at(2).toInt() != 0) ? 1 : 0;
        parsed.writeX << f.at(3).toDouble();
        parsed.writeV << f.at(4).toDouble();
        const quint8 hasWrite = (f.at(5).toInt() != 0) ? 2 : 0;
        parsed.durations << f.at(6).toDouble();
        parsed.flags << quint8(hasRead | hasWrite);
        parsed.items << f.at(7).toInt();
        parsed.passes << f.at(8).toInt();
    }

    if (!magicOk || parsed.readV.isEmpty()) {
        return fail(QStringLiteral("%1\n第一行应当是 \"# %2\"，并且含 [samples] 数据段。")
                        .arg(QDir::toNativeSeparators(path),
                             QString::fromLatin1(RunData::magic())));
    }

    // ---- 版本门槛 ----
    // **过旧的结果文件一律不打开**，直接给一句"版本过旧、请重新测试"。
    //
    // 为什么不做兼容：这个格式在 2.2.7 里改动很大（test_mode、item/pass、
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
    parsed.m_verifyFailures =
        int(parsed.params.value(QStringLiteral("verify_failures"), QStringLiteral("0"))
                .toDouble());
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
    parsed.m_fileSizeBytes =
        qint64(parsed.params.value(QStringLiteral("file_size_bytes"), QStringLiteral("0"))
                   .toDouble());
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
    // 全局字节 = Σ(前 fileIndex 个文件的大小) + 文件内偏移
    // 逐文件大小缺失时退回"序号 × 统一大小"；两者都没有就只能当成第一个文件。
    for (int i = 0; i < parsed.m_verifyOffsets.size(); ++i) {
        const qint64 offset = parsed.m_verifyOffsets.at(i);
        if (offset < 0) {
            parsed.m_verifyAxisX << -1.0;
            continue;
        }
        const int index = qMax(0, parsed.m_verifyFileIndexes.value(i, 0));
        qint64 before = 0;
        if (!parsed.m_fileSizes.isEmpty()) {
            for (int f = 0; f < index && f < parsed.m_fileSizes.size(); ++f)
                before += parsed.m_fileSizes.at(f);
            // 序号超出已知文件数：后面的按最后一个文件的大小顺延
            if (index > parsed.m_fileSizes.size()) {
                before += parsed.m_fileSizes.last()
                          * (index - parsed.m_fileSizes.size());
            }
        } else if (parsed.m_fileSizeBytes > 0) {
            before = index * parsed.m_fileSizeBytes;
        }
        parsed.m_verifyAxisX << double(before + offset) / (1024.0 * 1024.0 * 1024.0);
    }

    *data = parsed;
    return true;
}

bool RunData::parse(const QString &path, RunData *data, QString *error)
{
    if (!data)
        return false;

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

    return allOk;
}
