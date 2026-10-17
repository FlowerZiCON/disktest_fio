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

#include "fiorunner.h"

#include "fioconfig.h"
#include "prociocounter.h"
#include "uitext.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace {

constexpr int kSampleIntervalMs = 500;      // 需求：每 0.5 秒采样一次
constexpr int kMaxCapturedChars = 4 * 1024 * 1024;

/// 把 "493MiB/s" / "1.5GiB/s" / "98.4k" / "250" 这类 fio 记法解析成数值。
/// 带 'i' 的前缀按 1024 进制（MiB），不带 'i' 的按 1000 进制（IOPS 里的 k/M）。
bool parseSpeedValue(const QString &text, double *valueOut)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*([0-9]+(?:\.[0-9]+)?)\s*([KMGTPE]?)(i?)(B)?(?:\s*/\s*s)?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return false;

    const double base = m.captured(1).toDouble();
    const QString prefix = m.captured(2).toUpper();
    const bool binary = !m.captured(3).isEmpty();

    double multiplier = 1.0;
    if (!prefix.isEmpty()) {
        const int index = QStringLiteral("KMGTPE").indexOf(prefix);
        if (index < 0)
            return false;
        const double step = binary ? 1024.0 : 1000.0;
        for (int i = 0; i <= index; ++i)
            multiplier *= step;
    }

    if (valueOut != nullptr)
        *valueOut = base * multiplier;
    return true;
}

/// 从一行 ETA 里取出**作业状态字符**（r / w / m / f ...）。
///
/// 形如：
///   Jobs: 1 (f=1): [m(1)][13.6%][r=811MiB/s,w=350MiB/s][r=208k,w=89.6k IOPS][eta 00m:19s]
/// 状态在那一段 `[x(n)]` 里，`f` 表示所有作业都已经结束。
///
/// **只取状态**。这一行里的速率 / 百分比 / eta 文本一个都不参与计算：
/// 实时速率来自进程 I/O 计数器（见 emitSample），分辨率 0.5 s，比 fio 1 Hz 的
/// ETA 输出细，也没有"前 3 秒不输出"的空窗期。以前这里顺手把它们都解析出来
/// 存进一个结构体，而那些字段**一个读者都没有** —— 每次采样都在白跑正则。
/// 将来真要改用 ETA 速率，从这里取即可。
bool parseEtaState(const QString &line, QString *state)
{
    static const QRegularExpression jobsRe(
        QStringLiteral(R"(^Jobs:\s*\d+\s*\(f=\d+\)\s*:?\s*(.*)$)"));
    static const QRegularExpression bracketRe(QStringLiteral(R"(\[([^\]]*)\])"));
    static const QRegularExpression stateRe(QStringLiteral(R"(^([A-Za-z])\((\d+)\)$)"));

    const QRegularExpressionMatch jobsMatch = jobsRe.match(line);
    if (!jobsMatch.hasMatch())
        return false;

    QRegularExpressionMatchIterator it = bracketRe.globalMatch(jobsMatch.captured(1));
    while (it.hasNext()) {
        const QRegularExpressionMatch stateMatch = stateRe.match(it.next().captured(1).trimmed());
        if (!stateMatch.hasMatch())
            continue;
        if (state)
            *state = stateMatch.captured(1);
        return true;
    }
    return false;
}

struct ProblemPattern
{
    const char *needle;
    /// 取标签的**函数**而不是标签本身：界面文案是运行时按当前语言查出来的，
    /// 而这张表曾经是命名空间作用域的静态数组 —— 那会在 main() 之前就把
    /// 中文标签烤进去，换了语言也不跟着变。
    QString (*label)();
};

/// fio 有时"报表干净但实际有错"（例如 windowsaio 打不开文件却报 err=0），
/// 所以除了退出码还要主动扫这些关键字。
/// 后面几条是硬盘可靠性测试的关键：校验失败必须被抓住。
const QVector<ProblemPattern> &problemPatterns()
{
    static const QVector<ProblemPattern> patterns = {
        { "failed to open",      &kProblemOpenFailed },
        { "io_u error",          &kProblemIoUnitError },
        { "permission denied",   &kProblemPermissionDenied },
        { "no space left",       &kProblemNoSpaceLeft },
        { "bad option",          &kProblemBadOption },
        { "job global dropped",  &kProblemJobGlobalDropped },
        { "cannot open",         &kProblemCannotOpen },
        { "bad magic header",    &kProblemBadMagicHeader },
        // 校验失败的**两种措辞都要登记**：fio 3.x 长格式是 "crc32c: verify failed at ..."，
        // 短格式是 "Verify error at file ..., offset ..."。scanForVerifyFailures 两种都认
        // （那才是坏块计数的来源），这里只登记一种的话，短格式那条路的结果是：
        // 计数 > 0 但 warnings 是空的 —— 而结论判定又要看 warnings，于是"盘坏了"
        // 会被同一份汇总判成"合格"。两处口径必须一致。
        { "verify failed",       &kProblemVerifyFailed },
        { "verify error",        &kProblemVerifyFailed },
        { "illegal byte sequence", &kProblemIllegalByteSequence },
    };
    return patterns;
}

} // namespace

/// 从 fio 的校验失败原话里抠出**文件名序号**和**文件内字节偏移**。
///
/// fio 的措辞在不同版本里不完全一样，实测见到的两种：
///   "crc32c: verify failed at file testfile_3 offset 255852544, length 4194304 ..."
///   "Verify error at file testfile_0, offset 4096"
/// 还有不带文件名的变体。所以不写死格式，而是按关键字切：
///   先找 "file <名字><数字>" 拿到文件序号，再找第一个 "offset" 后面的整数。
///
/// **文件序号绝对不能省**：fio 报的偏移是"文件内偏移"，每个文件都从 0 开始，
/// testfile_0 的 256 MiB 和 testfile_3 的 256 MiB 是两个不同的盘上位置。
/// 只拿偏移去定位，所有坏块都会挤在第一个文件那一段里。
/// 认不出文件名时返回 0（当成第一个文件），认不出偏移时返回 -1（那就不画标记）。
static void parseVerifyPosition(const QString &line, int *fileIndex, qint64 *offset)
{
    *fileIndex = 0;
    *offset = -1;

    static const QRegularExpression fileRe(
        QStringLiteral(R"(file\s+(\S*?)(\d+)\s*,?\s*offset\s+(\d+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch fileMatch = fileRe.match(line);
    if (fileMatch.hasMatch()) {
        bool ok = false;
        const int index = fileMatch.captured(2).toInt(&ok);
        if (ok)
            *fileIndex = index;
        const qint64 value = fileMatch.captured(3).toLongLong(&ok);
        if (ok)
            *offset = value;
        return;
    }

    // 没有文件名（有些版本/有些错误只报偏移）：当成第一个文件
    static const QRegularExpression offsetRe(
        QStringLiteral(R"(offset[^0-9\-]{0,8}(\d+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch offsetMatch = offsetRe.match(line);
    if (!offsetMatch.hasMatch())
        return;
    bool ok = false;
    const qint64 value = offsetMatch.captured(1).toLongLong(&ok);
    if (ok)
        *offset = value;
}

bool FioResult::hasProblems() const
{
    // 校验失败必须算"有问题"。它不是"可疑日志"，而是**确凿的坏数据**：
    // 以前这里漏了它，而 isClean() 又是汇总结论的唯一依据 —— 只要 fio 用的是
    // 短措辞（"Verify error at ..."）而且没给 err=N，一块真坏了的盘就会被
    // 判成"合格"，同时屏幕上还挂着"发现 N 处不一致"的大红告警。
    if (verifyFailures > 0 || !firstVerifyError.isEmpty())
        return true;
    if (!warnings.isEmpty())
        return true;
    for (int err : jobErrors) {
        if (err != 0)
            return true;
    }
    return false;
}

bool FioResult::isClean() const
{
    if (exitCode != 0 || cancelled)
        return false;
    return !hasProblems();
}

FioRunner::FioRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_sampleTimer(new QTimer(this))
    , m_ioCounter(new ProcessIoCounter)
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &FioRunner::onStdoutReady);
    connect(m_process, &QProcess::readyReadStandardError, this, &FioRunner::onStderrReady);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FioRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &FioRunner::onProcessErrorOccurred);

    m_sampleTimer->setInterval(kSampleIntervalMs);
    m_sampleTimer->setTimerType(Qt::PreciseTimer);
    connect(m_sampleTimer, &QTimer::timeout, this, &FioRunner::onSampleTimer);
}

FioRunner::~FioRunner()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    removeJobFile();
    delete m_ioCounter;
    m_ioCounter = nullptr;
}

bool FioRunner::isBusy() const
{
    return m_running;
}

QString FioRunner::writeJobFile(const QString &text, const QString &name, QString *error)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) {
        if (error)
            *error = kErrNoTempDir();
        return QString();
    }
    QDir().mkpath(dir);

    // 文件名必须**不可预测、且独占创建**。
    //
    // 原来是 `<临时目录>/ml_disktest-write.fio` 这种固定名字 + WriteOnly|Truncate。
    // 临时目录是全局可写的，别的本地用户可以先摆一个同名符号链接，本程序
    // （设计上允许提权运行）一打开就顺着链接把**任意文件**截断成 job 内容。
    // 现在：名字带随机串（猜不到）+ NewOnly（Unix 上是 O_CREAT|O_EXCL，
    // 已存在直接失败，**不会跟随符号链接**），写完还要核对字节数。
    //
    // 文件用完由 removeJobFile() 删掉，不在临时目录里留东西。
    const QString base = QFileInfo(name).completeBaseName();
    const QString unique = QStringLiteral("%1-%2.fio")
                               .arg(base.isEmpty() ? QStringLiteral("ml_disktest") : base,
                                    QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString path = QDir(dir).absoluteFilePath(unique);

    const QByteArray payload = text.toUtf8();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error)
            *error = kErrWriteJobFile().arg(path, file.errorString());
        return QString();
    }
    const qint64 written = file.write(payload);
    const bool flushed = file.flush();
    // errorString() 要在 close() 之前取，关掉之后它会被重置
    const QString writeError = file.errorString();
    file.close();

    // 写失败 / 写了一半也报成功的话，fio 会在一个空 job 文件上报
    // "Bad option" 之类的错，和真正的原因（临时目录满了）完全对不上。
    if (written != payload.size() || !flushed) {
        QFile::remove(path);
        if (error)
            *error = kErrWriteJobFile().arg(path, writeError);
        return QString();
    }
    return path;
}

void FioRunner::removeJobFile()
{
    if (m_jobFilePath.isEmpty())
        return;
    QFile::remove(m_jobFilePath);
    m_jobFilePath.clear();
}

QString FioRunner::queryVersion(const QString &fioPath, int timeoutMs)
{
    if (fioPath.isEmpty() || !QFileInfo::exists(fioPath))
        return QString();

    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(fioPath, QStringList{QStringLiteral("--version")});
    if (!probe.waitForStarted(3000))
        return QString();
    if (!probe.waitForFinished(timeoutMs)) {
        probe.kill();
        probe.waitForFinished(1000);
        return QString();
    }
    const QString out = QString::fromLocal8Bit(probe.readAll()).trimmed();
    return out.section(QLatin1Char('\n'), 0, 0).trimmed();
}

QStringList FioRunner::queryEngines(const QString &fioPath, int timeoutMs)
{
    if (fioPath.isEmpty() || !QFileInfo::exists(fioPath))
        return QStringList();

    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(fioPath, QStringList{QStringLiteral("--enghelp")});
    if (!probe.waitForStarted(3000))
        return QStringList();
    if (!probe.waitForFinished(timeoutMs)) {
        probe.kill();
        probe.waitForFinished(1000);
        return QStringList();
    }

    // 输出长这样（前面还有一行 "Available IO engines:"）：
    //     libaio
    //     io_uring
    //     ...
    // 不去认那个表头，直接按"标识符"切词再挑 —— 版本之间表头措辞改过，
    // 但引擎名本身是稳定的。挑的时候只留我们关心的候选，
    // 免得把表头里的英文单词也当成引擎名。
    static const QRegularExpression tokenRe(QStringLiteral("[A-Za-z0-9_]+"));
    const QString out = QString::fromLocal8Bit(probe.readAll());

    QSet<QString> present;
    QRegularExpressionMatchIterator it = tokenRe.globalMatch(out);
    while (it.hasNext())
        present.insert(it.next().captured(0).toLower());

    // **按 candidateIoEngines() 的优先级返回，不是按 fio 打印的顺序。**
    // 调用方直接取 first() 当"选中的引擎"，而 `fio --enghelp` 的排列顺序是实现细节
    // （实测 3.43 是 windowsaio → psync，恰好等于我们想要的默认值，所以这个 bug
    // 一直没暴露）。一旦某个版本把低优先级的候选排在前面就会被选中：
    // Windows 上选到 psync 再配 direct=1 是最糟的组合。顺序在这里钉死。
    QStringList found;
    for (const QString &candidate : FioConfig::candidateIoEngines()) {
        if (present.contains(candidate))
            found << candidate;
    }
    return found;
}

bool FioRunner::startJob(const QString &workingDir, const QString &jobFilePath,
                         const QStringList &extraArgs, JobKind kind, QString *error)
{
    if (m_running) {
        if (error)
            *error = kErrPhaseRunning();
        return false;
    }
    if (m_fioPath.isEmpty() || !QFileInfo::exists(m_fioPath)) {
        if (error)
            *error = kErrFioNotSpecified();
        return false;
    }

    resetForRun(kind);
    m_workingDir = workingDir;
    // 上一轮的 job 文件先删掉：正常跑完时 onProcessFinished 已经删过了，
    // 这里兜住"启动失败就返回"那条路，别在临时目录里越积越多。
    removeJobFile();
    m_jobFilePath = jobFilePath;
    m_running = true;

    m_process->setWorkingDirectory(workingDir);

    QStringList args = extraArgs;
    args << jobFilePath;

    emit logText(QStringLiteral("> \"%1\" %2").arg(m_fioPath, args.join(QLatin1Char(' '))));
    emit logText(kFioWorkingDir().arg(workingDir));

    m_process->start(m_fioPath, args);
    if (!m_process->waitForStarted(15000)) {
        const QString msg = kErrFioStartFailed().arg(m_process->errorString());
        m_running = false;
        removeJobFile();
        if (error)
            *error = msg;
        return false;
    }

    // 采样 fio 进程自己的 I/O 计数器 —— 图表的数据来源。
    if (!m_ioCounter->attach(m_process->processId()))
        emit logText(kFioIocounterUnavailable());

    // 基线立刻在进程刚起来时取一次（而不是等第一拍），
    // 这样第一个采样区间就是 [0, 0.5s]，曲线能从 0 秒开始。
    m_havePrevCounters = false;
    m_lastSampleStart = 0.0;
    m_elapsedSeconds = 0.0;
    quint64 baselineRead = 0;
    quint64 baselineWrite = 0;
    if (m_ioCounter->isAttached() && m_ioCounter->read(&baselineRead, &baselineWrite)) {
        m_prevRead = baselineRead;
        m_prevWrite = baselineWrite;
        m_baseRead = baselineRead;
        m_baseWrite = baselineWrite;
        m_havePrevCounters = true;
    }

    m_runClock.start();
    m_sampleTimer->start(kSampleIntervalMs);
    return true;
}

void FioRunner::resetForRun(JobKind kind)
{
    m_stdoutBuffer.clear();
    m_stdoutAll.clear();
    m_warningHits.clear();
    m_warningOrder.clear();
    m_verifyFailures = 0;
    m_firstVerifyError.clear();
    m_verifyErrorLines.clear();
    m_verifyOffsets.clear();
    m_verifyFileIndexes.clear();
    m_result = FioResult();
    m_result.kind = kind;
    m_stoppedByUser = false;
    m_testFinished = false;
    m_prevRead = 0;
    m_prevWrite = 0;
    m_havePrevCounters = false;
}

void FioRunner::stop()
{
    if (m_process->state() == QProcess::NotRunning)
        return;
    m_stoppedByUser = true;
    emit logText(kFioUserStop());
    m_process->kill();
}

void FioRunner::noteTestFinished(const QString &reason)
{
    if (m_testFinished)
        return;
    m_testFinished = true;
    m_sampleTimer->stop();
    // 这里故意不再补一采样：从最后一拍结束到 fio 打印汇总之间没有任何测试 I/O，
    // 把它算进去只会让分母变大、分子是 0，凭空把平均值拉低几个百分点。
    m_elapsedSeconds = m_lastSampleStart;
    emit logText(kFioTestFinished().arg(reason));
}

void FioRunner::onSampleTimer()
{
    if (m_testFinished)
        return;
    if (m_process->state() != QProcess::Running)
        return;
    emitSample();
}

void FioRunner::emitSample()
{
    if (!m_ioCounter->isAttached())
        return;

    quint64 readBytes = 0;
    quint64 writeBytes = 0;
    if (!m_ioCounter->read(&readBytes, &writeBytes))
        return;

    const double now = m_runClock.elapsed() / 1000.0;
    m_elapsedSeconds = now;

    if (!m_havePrevCounters) {
        m_prevRead = readBytes;
        m_prevWrite = writeBytes;
        m_havePrevCounters = true;
        m_lastSampleStart = now;
        return;
    }

    if (readBytes < m_prevRead || writeBytes < m_prevWrite) {
        m_prevRead = readBytes;
        m_prevWrite = writeBytes;
        m_lastSampleStart = now;
        return;
    }

    const double interval = now - m_lastSampleStart;
    if (interval < 0.05) {
        // 区间太短，除出来的速率没意义
        return;
    }

    const double readRate = double(readBytes - m_prevRead) / interval;
    const double writeRate = double(writeBytes - m_prevWrite) / interval;

    // 区间**起点**的累计字节数（还没把这一拍算进去）
    const quint64 cumulativeRead = m_prevRead - m_baseRead;
    const quint64 cumulativeWrite = m_prevWrite - m_baseWrite;

    m_prevRead = readBytes;
    m_prevWrite = writeBytes;
    const double intervalStart = m_lastSampleStart;
    m_lastSampleStart = now;

    emit ioRate(readRate, writeRate, intervalStart, interval, cumulativeRead, cumulativeWrite);
}

void FioRunner::onStdoutReady()
{
    consumeStdout();
}

void FioRunner::onStderrReady()
{
    const QString text = QString::fromLocal8Bit(m_process->readAllStandardError());
    if (text.isEmpty())
        return;

    scanForProblems(text);
    scanForVerifyFailures(text);
    emit logText(QStringLiteral("[stderr] %1").arg(text.trimmed()));
}

/// 把缓冲区里**已经完整**的行切出来，返回它们，并把这些字节从缓冲区里摘掉。
///
/// 关键：调用方拿到列表之后**在循环外**再逐条处理 / 发信号，绝不能边遍历缓冲区
/// 边发信号。三个信号（logText / warningRaised / verifyFailureRaised）都是直连到
/// 主窗口的，将来哪个槽一旦自己转起事件循环（弹一个模态框就够），
/// readyReadStandardOutput 就会重入这个函数：缓冲区已经被摘掉了，重入那一次
/// 什么也切不到；可要是还留在里面，同一批行会被处理两遍（坏块数翻倍）。
static QStringList takeCompleteLines(QByteArray *buffer)
{
    QStringList lines;
    int start = 0;
    for (int i = 0; i < buffer->size(); ++i) {
        const char c = buffer->at(i);
        if (c != '\r' && c != '\n')
            continue;
        if (i > start) {
            const QString fragment =
                QString::fromLocal8Bit(buffer->mid(start, i - start)).trimmed();
            if (!fragment.isEmpty())
                lines << fragment;
        }
        start = i + 1;
    }
    if (start > 0)
        buffer->remove(0, start);

    // 一整行都没有（不正常，除非 fio 输出了 64 KiB 不带换行的东西）：丢掉，
    // 否则缓冲区会无限涨。
    if (buffer->size() > 64 * 1024)
        buffer->clear();
    return lines;
}

void FioRunner::consumeStdout()
{
    m_stdoutBuffer += m_process->readAllStandardOutput();
    const QStringList fragments = takeCompleteLines(&m_stdoutBuffer);
    for (const QString &fragment : fragments)
        handleFragment(fragment);
}

void FioRunner::handleFragment(const QString &fragment)
{
    m_stdoutAll += fragment;
    m_stdoutAll += QLatin1Char('\n');
    if (m_stdoutAll.size() > kMaxCapturedChars)
        m_stdoutAll.remove(0, m_stdoutAll.size() - kMaxCapturedChars / 2);

    scanForProblems(fragment);

    // 解析 ETA 只有一个用途：状态打成 f 就说明所有 job 都跑完了。
    QString state;
    if (parseEtaState(fragment, &state) && state == QLatin1String("f"))
        noteTestFinished(kFioJobEnded());

    if (fragment.startsWith(QLatin1String("Run status group")))
        noteTestFinished(kFioSummaryPrinted());

    emit logText(fragment);
}

void FioRunner::scanForProblems(const QString &text)
{
    const QString lower = text.toLower();
    for (const ProblemPattern &pattern : problemPatterns()) {
        if (!lower.contains(QLatin1String(pattern.needle)))
            continue;

        const QString key = QString::fromLatin1(pattern.needle);
        auto it = m_warningHits.find(key);
        if (it == m_warningHits.end()) {
            m_warningHits.insert(key, 1);
            const QString label = pattern.label();
            m_warningOrder.append(qMakePair(key, label));
            emit warningRaised(label);
        } else {
            ++it.value();
        }
    }
}

void FioRunner::scanForVerifyFailures(const QString &text)
{
    // fio 校验失败时打印两行相关输出：
    //   "Verify error at offset ..."（fio 3.x 的短格式）
    //   "crc32c: verify failed at <file> offset <off>, length <len> (request ...)"
    // 两个关键字都要认：版本不同，打印的措辞不一样。
    //
    // 一次扫描里的每一行算**一块**：数字用来统计，
    // 原话用来定位——**每处都要报自己的偏移量**，
    // 不能每次都给第一条（那样日志里所有行看起来都在同一个位置，
    // 会让人以为"这块盘只在那一处坏"，而实际上分散在很多地方）。
    const QString lower = text.toLower();
    const bool fatal = lower.contains(QLatin1String("verify failed"));
    const bool counted = lower.contains(QLatin1String("verify error"));
    if (!fatal && !counted)
        return;

    // 按行数一遍，一行算一块
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    int added = 0;
    QString lastLine;
    qint64 lastOffset = -1;
    int lastFileIndex = 0;
    for (const QString &line : lines) {
        const QString l = line.toLower();
        if (l.contains(QLatin1String("verify error"))
            || l.contains(QLatin1String("verify failed"))) {
            ++added;
            lastLine = line.trimmed();
            parseVerifyPosition(lastLine, &lastFileIndex, &lastOffset);
            if (m_firstVerifyError.isEmpty())
                m_firstVerifyError = lastLine;
            // 逐处记下来（去重 + 限量）：日志里要能看出"坏在不止一个地方"
            if (m_verifyErrorLines.size() < kMaxVerifyDetailLines
                && !m_verifyErrorLines.contains(lastLine)) {
                m_verifyErrorLines << lastLine;
                m_verifyOffsets << lastOffset;
                m_verifyFileIndexes << lastFileIndex;
            }
        }
    }
    if (added <= 0) {
        added = 1;   // 关键字命中了但没切成行（碎片跨行），至少记一块
        lastLine = text.trimmed();
        parseVerifyPosition(lastLine, &lastFileIndex, &lastOffset);
    }

    m_verifyFailures += added;
    // 发**这一批里最后一条**的原话：界面按"每处一行"记日志，
    // 这样每个偏移量都会出现，而不是反复刷同一条。
    // 带上文件序号，界面才能把它换算成盘上的真实位置。
    emit verifyFailureRaised(m_verifyFailures, lastLine, lastOffset, lastFileIndex);
}

void FioRunner::onProcessErrorOccurred(int error)
{
    // 启动失败的原因必须报出去。以前它被塞进一个私有成员再没人读，
    // 于是"fio 起不来"只剩 startJob 返回 false 这一条线索，原因丢了。
    if (error == QProcess::FailedToStart)
        emit logText(kErrFioStartFailed().arg(m_process->errorString()));
}

void FioRunner::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_sampleTimer->stop();

    // 收尾那一拍：只有测试没被正常检测到结束时才补（例如用户手动停止），
    // 好让曲线和统计都覆盖到进程真正结束的时刻。
    if (!m_testFinished) {
        emitSample();
        m_elapsedSeconds = m_runClock.isValid() ? m_runClock.elapsed() / 1000.0
                                                : m_elapsedSeconds;
    }

    consumeStdout();
    onStderrReady();

    m_ioCounter->detach();
    // fio 已经读完了 job 文件，可以删了（临时目录里不留东西）
    removeJobFile();

    finalizeResult();
    m_result.exitCode = exitCode;
    m_result.cancelled = m_stoppedByUser || status != QProcess::NormalExit;
    m_result.elapsedSeconds = m_elapsedSeconds;

    m_running = false;
    emit jobFinished(m_result);
}

void FioRunner::finalizeResult()
{
    static const QRegularExpression groupRe(
        QStringLiteral(R"(^\s*(READ|WRITE)\s*:\s*bw=([^,]+),\s*([^,]+),\s*io=([^,]+),\s*run=(\S+))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression errRe(
        QStringLiteral(R"(\(groupid=\d+,\s*jobs=\d+\)\s*:\s*err=\s*(\d+))"));

    const QStringList lines = m_stdoutAll.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch groupMatch = groupRe.match(line);
        if (groupMatch.hasMatch()) {
            const QString direction = groupMatch.captured(1).toUpper();
            const QString bwText = groupMatch.captured(2).trimmed().section(QLatin1Char(' '), 0, 0);
            double bytesPerSec = 0.0;
            if (!parseSpeedValue(bwText, &bytesPerSec))
                continue;
            const double mib = bytesPerSec / (1024.0 * 1024.0);
            if (direction == QLatin1String("READ")) {
                m_result.hasRead = true;
                m_result.readMiBs = mib;
                m_result.readDetail = QStringLiteral("io=%1, run=%2")
                                          .arg(groupMatch.captured(4).trimmed(),
                                               groupMatch.captured(5).trimmed());
            } else {
                m_result.hasWrite = true;
                m_result.writeMiBs = mib;
                m_result.writeDetail = QStringLiteral("io=%1, run=%2")
                                           .arg(groupMatch.captured(4).trimmed(),
                                                groupMatch.captured(5).trimmed());
            }
            continue;
        }

        const QRegularExpressionMatch errMatch = errRe.match(line);
        if (errMatch.hasMatch())
            m_result.jobErrors.append(errMatch.captured(1).toInt());
    }

    for (const auto &entry : m_warningOrder) {
        const int count = m_warningHits.value(entry.first, 0);
        if (count <= 0)
            continue;
        m_result.warnings << (count > 1
                                  ? kTextWarningCount().arg(entry.second).arg(count)
                                  : entry.second);
    }

    // 校验失败单独带出去：它不是"可疑"，是确凿的坏数据，界面要拿它挂大字告警。
    m_result.verifyFailures = m_verifyFailures;
    m_result.firstVerifyError = m_firstVerifyError;
    m_result.verifyErrorLines = m_verifyErrorLines;
    m_result.verifyOffsets = m_verifyOffsets;
    m_result.verifyFileIndexes = m_verifyFileIndexes;
}
