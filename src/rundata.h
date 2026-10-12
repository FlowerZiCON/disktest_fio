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
//  测试结果 CSV 的**只读解析**与结果窗口共用的格式化
//
//  解析部分只做一件事：把一份结果文件读成一个 RunData 结构，不碰任何界面。
//  两个使用方：
//    * ResultWindow  —— 打开历史结果（独立窗口，完全不改主界面）
//    * MainWindow::loadRunDataFrom() —— 自检用的"灌进主窗口"那条老路
//
//  解析与显示分开之后，模式判别只有一处实现，不会出现
//  "窗口里按性能渲染、主界面按可靠性渲染"这种不一致。
//
//  格式化那几个函数（format*）原本是 mainwindow.cpp 里的匿名命名空间函数，
//  结果窗口要用同一套数字格式，就搬到这里共享，避免两份不一致的副本。
// ============================================================================

#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

class RunData
{
public:
    /// 整份文件是哪种测试。**老文件没有 test_mode 时一律按可靠性**，
    /// 这是安全的默认（历史文件全部产生自可靠性测试）。
    enum class Mode {
        Reliability,
        Performance
    };

    /// 一个测试项的逐点轨迹（性能数据用）。横轴 = 秒，纵轴 = MiB/s。
    struct Trace
    {
        QVector<QPointF> read;
        QVector<QPointF> write;
        double readValue  = -1.0;   ///< 结果表里的读数（MiB/s），< 0 = 没测
        double writeValue = -1.0;
        bool   hasRead() const { return !read.isEmpty(); }
        bool   hasWrite() const { return !write.isEmpty(); }
    };

    /// 测试数据 CSV 的格式标记。第一行写它，加载时用来判断文件类型与版本。
    static const char *magic() { return "ml_disktest-run-data;1"; }
    static const char *samplesMarker() { return "[samples]"; }
    static const char *runMarker() { return "[run]"; }

    /// 结果文件里的测试模式参数名；写入与读取都只认这一处。
    static QString testModeKey() { return QStringLiteral("test_mode"); }
    static QString modeKey(Mode mode)
    {
        return mode == Mode::Performance ? QStringLiteral("performance")
                                         : QStringLiteral("reliability");
    }
    /// 写这份结果的程序版本参数名。
    static QString appVersionKey() { return QStringLiteral("app_version"); }

    /// 能打开的最低版本。**比这个低的文件一律拒收**，提示版本过旧。
    ///
    /// 为什么干脆不支持老文件：结果格式在 2.2.7 这一版改动很大
    /// （加了 test_mode、item/pass、坏块信息、逐文件大小），
    /// 老文件缺这些字段时只能靠"猜"来渲染——猜错了画出来的图看着正常、
    /// 实际位置和内容都是错的，比直接说"不支持"更坑人。
    static QString minimumReadableVersion() { return QStringLiteral("2.2.7"); }

    /// 比较两个 "x.y.z" 版本号：a < b 返回负、相等返回 0、a > b 返回正。
    /// 段数不齐时缺的按 0 算（"2.2" == "2.2.0"）；非数字段按 0 算，
    /// 不抛异常——结果文件是不可信输入，解析不能因为一个怪版本号就崩。
    static int compareVersions(const QString &a, const QString &b);

    // ---- 解析 ----

    /// 解析结果文件。失败时把原因写进 *error（若给了）并返回 false。
    /// 不检查文件里的数据是否属于当前机器上的某块盘——历史数据本来就可能来自别的盘。
    static bool parse(const QString &path, RunData *data, QString *error);

    // ---- 数据查询 ----

    /// 读取单个字符串参数；不存在时返回 fallback。
    QString param(const QString &key, const QString &fallback = QString()) const
    {
        return params.value(key, fallback);
    }
    /// 读取单个数值参数；不存在或解析不出时返回 fallback。
    double paramDouble(const QString &key, double fallback) const;
    /// 参数是否存在（老文件可能整段缺失）。
    bool   hasParam(const QString &key) const { return params.contains(key); }

    /// 横轴上限（GiB）：新文件写 axis_max_GiB，老文件写 axis_max_GB，两种都认。
    double axisMaxGiB() const;

    /// 可靠性数据：优先取参数里记录的"分阶段"平均值（读取阶段的读均速 /
    /// 写入阶段的写均速），不能拿全部采样点混着重算；老文件没有才退回采样点。
    double averageReadMiBps() const;
    double averageWriteMiBps() const;

    /// 已用秒数：优先用文件里记录的阶段耗时之和，否则用采样区间累加。
    double elapsedSeconds() const;

    /// 性能数据：把 [samples] 段按 item/pass 还原成四项各自的读写轨迹。
    /// 返回 4 个元素（顺序同 FioConfig::allPerfItems()）。
    QVector<Trace> perfTraces() const;

    /// 性能数据里实际出现过的测试项下标（升序，最多 4 个）。
    /// 没有 item 列的老文件返回空 —— 那就只能画一条合起来的曲线。
    QVector<int> perfItemsPresent() const;

    // ---- 坏块信息（校验失败）----

    /// 数据校验失败的块数。0 = 没发现不一致。
    /// 老文件（没有 verify_failures 这个参数）一律是 0，也就是"未知/没记录"。
    int  verifyFailures() const { return m_verifyFailures; }
    /// 逐处的失败原话（fio 的输出，含它自己的偏移量），用于列出坏在哪。
    const QStringList &verifyFailuresText() const { return m_verifyTexts; }
    /// 每一处失败的**文件内字节偏移**，与 verifyFailuresText() 一一对应。
    const QVector<qint64> &verifyOffsets() const { return m_verifyOffsets; }
    /// 每一处失败所在的**文件序号**（fio 原话里 testfile_N 的 N）。
    /// fio 报的偏移每个文件都从 0 开始，光有偏移定位不到盘上的位置。
    const QVector<int> &verifyFileIndexes() const { return m_verifyFileIndexes; }
    /// 每一处失败换算好的**图表横坐标**（已处理容量，GiB），顺序同上。
    /// 解析时就算好了（要用到文件序号 + 单文件大小）；< 0 = 定位不出来。
    const QVector<double> &verifyAxisX() const { return m_verifyAxisX; }
    /// 单文件大小（字节）。0 = 文件里没记。只在拿不到 fileSizes() 时用作兜底。
    qint64 fileSizeBytes() const { return m_fileSizeBytes; }
    /// 逐文件大小（字节，按读取顺序）。坏块位置换算的首选基准。
    const QVector<qint64> &fileSizes() const { return m_fileSizes; }

    /// 解析器的**纯逻辑自检**：不碰磁盘，只用内存里的字符串验证
    /// 模式判别、老文件兼容、9 列与 7 列采样行的解析。
    /// 每条断言的结果写进 *lines；全部通过返回 true。
    static bool runParserSelfTest(QStringList *lines);

    // ---- 共用格式化 ----

    /// MiB/s → "356.4 MiB/s" / "1.20 GiB/s"；负数表示"没测"，显示 --。
    static QString formatSpeed(double mib);
    /// 字节数 → "58.4 GiB"（1000 以下保留一位小数，B 不带小数）。
    static QString formatBytes(qint64 bytes);
    /// 秒数 → "00:08:18"。
    static QString formatDuration(qint64 seconds);
    /// 秒数 → "10.0 s"（性能数据的横轴用）。
    static QString formatSeconds(double seconds);

    // ---- 数据本身 ----

    QString path;                       ///< 打开的是哪份文件
    Mode    mode = Mode::Reliability;   ///< 判定出来的模式
    /// 文件里**显式**写了 test_mode。false 时模式是推断出来的，
    /// 界面上会附一句"老文件按可靠性渲染"，免得用户以为程序读错了。
    bool    modeExplicit = false;
    /// 写这份结果的程序版本（参数段 app_version）。
    /// 打开前已经用它把过旧的文件挡掉了，这里留着只为显示/排查。
    QString appVersion;

    /// 参数段原样保留（键 → 值），免得每加一个参数都要改结构体。
    QHash<QString, QString> params;

    // ---- [samples] 段的逐点数据 ----
    QVector<double> readX, readV, writeX, writeV, durations;
    QVector<quint8> flags;                    ///< bit0 = 有读，bit1 = 有写
    QVector<int>    items;                    ///< 性能数据：这一拍属于哪个测试项（-1 = 未知）
    QVector<int>    passes;                   ///< 性能数据：0 = 读取趟，1 = 写入趟（-1 = 未知）

    /// 校验失败：块数 + 逐处原话 + 逐处文件内偏移（三者一一对应）。
    /// 从参数段 verify_failures / verify_text / verify_offsets / verify_file_index 读出来。
    int             m_verifyFailures = 0;
    QStringList     m_verifyTexts;
    QVector<qint64> m_verifyOffsets;
    QVector<int>    m_verifyFileIndexes;
    /// 算好的图表横坐标（GiB），与上面几项一一对应
    QVector<double> m_verifyAxisX;
    /// 单文件大小，用于把"文件内偏移"换算成图表横坐标
    qint64          m_fileSizeBytes = 0;
    /// 逐文件大小（按读取顺序），坏块位置换算的首选基准
    QVector<qint64> m_fileSizes;

    int sampleCount() const { return readV.size(); }
};
