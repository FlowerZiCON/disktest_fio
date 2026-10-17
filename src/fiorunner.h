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

#include "uitext.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QProcess>   // moc 需要完整定义：槽签名里用了 QProcess::ExitStatus
#include <QString>
#include <QStringList>

class QTimer;
class ProcessIoCounter;

/// 一次 fio 调用属于哪个阶段。
enum class JobKind {
    Write,   ///< 可靠性：写入阶段（把测试文件写满）
    Read,    ///< 可靠性：读取阶段（读回并校验）
    Perf     ///< 性能：某个测试项的固定时长运行
};

inline QString jobKindText(JobKind kind)
{
    switch (kind) {
    case JobKind::Write: return kPhaseWrite();
    case JobKind::Read:  return kPhaseRead();
    case JobKind::Perf:  return kPhasePerf();
    }
    return kTextUnknown();
}

/// 一个阶段结束后的汇总。
struct FioResult
{
    JobKind     kind = JobKind::Write;
    int         exitCode = -1;
    bool        cancelled = false;
    bool        hasRead = false;
    bool        hasWrite = false;
    double      readMiBs = 0.0;
    double      writeMiBs = 0.0;
    QString     readDetail;
    QString     writeDetail;
    double      elapsedSeconds = 0.0;  ///< 该阶段的窗口长度（秒）
    QList<int>  jobErrors;             ///< 每个 job 的 err= 值
    QStringList warnings;              ///< 命中的可疑日志（已汇总计数）
    /// 校验失败的块数。0 = 没发现不一致。
    /// fio 每发现一块对不上就打印一行 "Verify error"，这里数的是行数。
    int         verifyFailures = 0;
    /// 第一条校验失败的原话（含偏移量），没有就是空串。
    QString     firstVerifyError;
    /// 去重后的失败原话，最多 kMaxVerifyDetailLines 条。
    /// 用来往日志里逐处写明"坏在哪"——只报第一条会让人误以为只有那一处坏。
    QStringList verifyErrorLines;
    /// 每一处失败的**文件内字节偏移**，与 verifyErrorLines 一一对应。
    /// 注意：这是**文件内**偏移，不是盘上的位置——fio 输出的就是它。
    /// 要算盘上位置必须配合 verifyFileIndex（见下）。
    QVector<qint64> verifyOffsets;
    /// 每一处失败所在的**文件序号**（fio 原话里的 testfile_N 的 N），
    /// 与 verifyErrorLines / verifyOffsets 一一对应。
    ///
    /// 为什么必须单独记：fio 只报"文件内偏移"，**每个文件都从 0 开始报**，
    /// 所以 testfile_0 的 256 MiB 和 testfile_3 的 256 MiB 是两个完全不同的
    /// 盘上位置，光看偏移分不出来。少了这一列，图上的坏块标记会全挤到最前面
    /// （实测就踩了这个坑：530 处失败全被画到 0.24~1.43 GiB）。
    QVector<int> verifyFileIndexes;

    /// 这个阶段有没有"测试本身没跑对"或"数据校验不一致"的证据。
    ///
    /// **判定口径只有这一处**：`isClean()`、界面的汇总结论（MainWindow::finishWholeTest）
    /// 和阶段小结全部走它。以前"这次跑得好不好"在两处各写了一遍，其中一处漏了
    /// verifyFailures —— 一块真的坏了的盘有可能被判成"合格"。
    bool hasProblems() const;
    bool isClean() const;
};

/// 跑 fio 进程：写 job 文件之外的参数由调用方给，按 0.5 s 采样进程 I/O 计数，
/// 解析 ETA 判断测试何时结束，并扫描可疑日志（含校验失败）。
class FioRunner : public QObject
{
    Q_OBJECT

public:
    explicit FioRunner(QObject *parent = nullptr);
    ~FioRunner() override;

    void    setFioPath(const QString &path) { m_fioPath = path; }
    QString fioPath() const { return m_fioPath; }

    bool isBusy() const;

    /// 把一段 job 文本写到临时目录，返回绝对路径。
    static QString writeJobFile(const QString &text, const QString &name, QString *error);

    /// 启动一次 fio 调用。workingDir 就是测试目录（job 里只写裸文件名，
    /// 从根上避开 fio 把盘符冒号当成多文件分隔符的坑）。
    bool startJob(const QString &workingDir, const QString &jobFilePath,
                  const QStringList &extraArgs, JobKind kind, QString *error);

    void stop();

    /// 同步取 fio 版本（fio --version）。
    static QString queryVersion(const QString &fioPath, int timeoutMs = 5000);

    /// 同步问一次 fio 支持哪些 IO 引擎（`fio --enghelp`），返回引擎名列表。
    /// 问不出来（fio 不在 / 超时 / 输出看不懂）返回空列表。
    ///
    /// 用途：引擎不能再写死了。Windows 上固定 windowsaio，但 Linux 上
    /// 发行版编的 fio 未必带 libaio，界面得在开测前探一次、
    /// 挑一个真正存在的引擎（见 FioConfig::candidateIoEngines）。
    static QStringList queryEngines(const QString &fioPath, int timeoutMs = 5000);

signals:
    /// 每 0.5 s 一次：区间 [intervalStartSec, +intervalSec) 的读/写速度（字节/秒）。
    /// 时间基准是该阶段 fio 进程启动的瞬间，给的是区间起点，所以首点落在 0 秒。
    /// cumulative* 是该阶段**从进程启动累计**的真实字节数（取区间起点的值）——
    /// 横轴"已处理容量"用它，比"速度×时间"积分准，不会漂移。
    void ioRate(double readBytesPerSec, double writeBytesPerSec,
                double intervalStartSec, double intervalSec,
                quint64 cumulativeReadBytes, quint64 cumulativeWriteBytes);
    void logText(const QString &text);
    void warningRaised(const QString &text);
    /// 发现一块数据校验不一致时**立刻**发一次（不等阶段结束），
    /// 界面靠它实时把红色告警挂出来、并在图表上打黄色标记。
    /// text 是 fio 的原话；fileOffset 是**文件内**字节偏移，fileIndex 是
    /// 它在 testfile_N 里的 N（两者缺一都算不出盘上的位置）。
    /// 解析不出来时 fileOffset 为 -1（那就只报警、不画标记）。
    void verifyFailureRaised(int totalCount, const QString &text,
                             qint64 fileOffset, int fileIndex);
    void jobFinished(const FioResult &result);

private slots:
    void onStdoutReady();
    void onStderrReady();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(int error);
    void onSampleTimer();

private:
    void resetForRun(JobKind kind);
    /// 读一次进程 I/O 计数并按区间差分，然后发 ioRate。
    void emitSample();
    /// fio 已经跑完测试时调用：停止采样（避免把打印报告的空转算进去）。
    void noteTestFinished(const QString &reason);
    void consumeStdout();
    void handleFragment(const QString &fragment);
    void scanForProblems(const QString &text);
    /// 单独扫校验失败：它和"可疑日志"不是一回事——校验失败是**确凿的坏数据**，
    /// 要计数、要记第一条原话、还要实时通知界面，所以不能混在 warnings 里。
    void scanForVerifyFailures(const QString &text);
    void finalizeResult();
    /// 删掉本次运行用的 job 文件（它建在系统临时目录里，用完必须清掉）。
    void removeJobFile();

    QProcess         *m_process     = nullptr;
    QTimer           *m_sampleTimer = nullptr;
    ProcessIoCounter *m_ioCounter   = nullptr;

    QString m_fioPath;
    QString m_workingDir;
    /// 本次运行写到临时目录的那个 job 文件；跑完 / 析构时删掉。
    QString m_jobFilePath;
    bool    m_running = false;
    bool    m_stoppedByUser = false;

    QByteArray m_stdoutBuffer;
    QString    m_stdoutAll;
    QHash<QString, int> m_warningHits;
    QList<QPair<QString, QString>> m_warningOrder;
    /// 校验失败：累计块数 + 第一条原话 + 去重后的每条原话。
    int     m_verifyFailures = 0;
    QString m_firstVerifyError;
    QStringList m_verifyErrorLines;
    /// 失败位置的文件内偏移（字节），顺序与 m_verifyErrorLines 一一对应。
    /// 界面拿它换算成图表的横坐标（已处理容量）；-1 = 没解析出来。
    QVector<qint64> m_verifyOffsets;
    /// 上面每一处所在的文件序号（testfile_N 的 N），顺序一一对应。
    QVector<int>    m_verifyFileIndexes;    /// 逐处记录的上限：一块烂盘能刷出上千条，日志全抄一遍没有意义。
    /// 统计数字始终是完整的，只有"逐处明细"会被截断。
    /// 这个上限同时也是"图表上能标出多少处"的上限（偏移列表跟它一起截），
    /// 所以别设得太小——它是给健康度画像用的，200 条只能画到前 1/5 个盘。
    static constexpr int kMaxVerifyDetailLines = 1000;
    FioResult  m_result;

    QElapsedTimer m_runClock;          ///< 从 fio 进程启动开始计时
    double m_lastSampleStart = 0.0;
    double m_elapsedSeconds = 0.0;
    bool   m_testFinished = false;
    quint64 m_prevRead  = 0;
    quint64 m_prevWrite = 0;
    /// 阶段开始时的基线计数：累计量 = 当前计数 − 基线。
    quint64 m_baseRead  = 0;
    quint64 m_baseWrite = 0;
    bool    m_havePrevCounters = false;
};
