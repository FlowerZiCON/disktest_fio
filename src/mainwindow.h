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

#include <QMainWindow>

#include <memory>
#include <QVector>

#include "driveinfo.h"
#include "fioconfig.h"
#include "fiorunner.h"
#include "rundata.h"

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QFile;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
QT_END_NAMESPACE

class SpeedChart;
class PerfCell;
class QFile;
class QRadioButton;

/// 硬盘可靠性测试工具的主窗口。
///
/// 流程：在选中分区的 ml_disktest 目录里预建 testfile_* 文件（每个 ≤ 2 GiB）铺满
/// 剩余空间 → fio 顺序/随机写入一遍 → （可选）读回并逐块 crc32c 校验。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void setFioPath(const QString &path);
    void setSelfTestOutputDir(const QString &dir);
    /// 自检用的目标分区（形如 "G:/"）。
    void setSelfTestTarget(const QString &root);
    /// 自检用的填充上限（MiB）：真写满大盘太慢，自动化测试压到一个小值。
    void setSelfTestFillLimit(int mib);
    /// 自检用的读写模式 key（seq / rand / seqmix / randmix）。
    void setSelfTestMode(const QString &modeKey);
    /// 自检时在第 N 秒触发一次「停止」，用来回归验证停止路径。
    void setSelfTestStopAfter(int seconds);
    /// 自检时把绘图方式切到散点图。
    void setSelfTestScatter(bool scatter);
    /// 自检时只跑读取阶段（用来验证「有文件才能只读」这条路径）。
    void setSelfTestReadOnly(bool readOnly);
    /// 自检：把折线图切到低密度（按数据量自动抽稀）。
    void setSelfTestLowDensity(bool low);
    /// 自检：测试圈数。
    void setSelfTestRounds(int rounds);
    /// 自检：跑性能模式，并指定测试数据大小（MiB）。
    void setSelfTestPerf(bool perf, int dataMiB);
    /// 自检：跑完后把测试数据写到这个 CSV（验证保存）。
    void setSelfTestSaveData(const QString &path);
    /// 自检：直接加载这个 CSV 画图并出报告（验证加载）。
    void setSelfTestLoadData(const QString &path);
    /// **仅供开发排查**：把校验失败告警条强制显示出来。
    /// 只有在真盘上真的跑出坏块时这条告警才会自己出现，而想要"看一眼它长什么样"
    /// 不该要求手边正好有一块坏盘。用了这个开关会在窗口标题和日志里明确标出来，
    /// 免得截图被当成真实测试结果。
    void setDebugForceVerifyBanner(bool force);

public slots:
    void runSelfTest();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onBrowseFio();
    void onOpenTestDirectory();
    void onCleanupTestFiles();
    void onSaveDiagnostics();
    void onSaveRunData();
    void onLoadRunData();
    /// 勾上「写入」时，如果目标分区里已有测试文件就先问要不要覆盖。
    void onWriteCheckToggled(bool checked);
    /// 切换可靠性/性能模式：换参数页、换标题、重置结果表。
    void onTestModeChanged();
    /// 弹出"要覆盖旧测试文件吗"的确认框；返回 true 表示可以继续。
    bool confirmOverwriteTestFiles();
    void onStart();
    void onStop();
    void onClearChart();
    void onAbout();
    void onDriveSelectionChanged();
    void onOptionsChanged();
    void onPlotStyleChanged();
    void onDensityChanged();

    void onIoRate(double readBytesPerSec, double writeBytesPerSec,
                  double intervalStartSec, double intervalSec,
                  quint64 cumulativeReadBytes, quint64 cumulativeWriteBytes);
    void onRunnerOutput(const QString &text);
    void onRunnerWarning(const QString &text);
    /// fio 报出一块校验不一致：实时把右侧的大字红色告警挂出来，
    /// 并在图表的对应横坐标上打一条黄色标记。
    void onVerifyFailure(int totalCount, const QString &text, qint64 fileOffset, int fileIndex);
    void onJobFinished(const FioResult &result);

private:
    /// 运行期统计量（全部来自 0.5 秒一次的实时采样）。
    struct RunStats
    {
        int    count = 0;
        double readMin = 0.0, readAvg = 0.0, readMax = 0.0;
        double writeMin = 0.0, writeAvg = 0.0, writeMax = 0.0;
    };

    /// 每个阶段结束后的存档，用于最终汇总。
    struct PhaseOutcome
    {
        int       round = 1;
        JobKind   kind = JobKind::Write;
        FioResult result;
        RunStats  stats;
    };

    enum class Phase { Idle, Preparing, Writing, Reading };

    void buildUi();
    void buildMenu();
    void buildParamPanel(QWidget *panel);
    /// 性能模式的参数页：测试数据大小 + 测试项/结果表。
    void buildPerfPage(QWidget *page);
    FioConfig::TestMode currentTestMode() const;
    QString currentModeName() const;
    void updateWindowTitle();
    void buildDrivePanel(QWidget *panel);
    QWidget *buildChartPanel();

    /// 把界面恢复成出厂默认值。**只读代码里的常量，不碰任何配置文件**。
    /// 每次启动调一次；这样"启动状态"永远是确定的。
    void resetToDefaultSettings();

    FioConfig configFromUi() const;
    void      applyConfigToUi(const FioConfig &cfg);

    /// 当前目标**目录**（测试文件建在它下面的 ml_disktest 里）。
    /// 选中列表里的一行会把它设成那一行的「可测目录」；
    /// 用「浏览目录…」可以直接指定任意可写目录（不限于列表里的）。
    QString targetDir() const { return m_targetDir; }
    /// 把目标目录换成 <dir>，并尝试在列表里选中对应的文件系统那一行。
    void setTargetDir(const QString &dir);
    /// 当前选中的那一行对应的可测目录（没选中 / 那一行不可测时返回空）。
    QString selectedTargetDir() const;
    /// 「浏览目录…」：用户直接指定一个目录。
    void onBrowseTargetDir();
    /// 重新枚举并填充磁盘 / 分区列表。
    /// startup = true 时**不预选任何盘**（启动时用）：第一行常常是系统盘，
    /// 默认选中它等于"打开程序就能顺手点开始测"，选盘必须由用户主动做一次。
    bool    refreshDriveList(bool startup = false);
    void    refreshTargetInfo();
    void    refreshDiskBar(quint64 total, quint64 free);
    void    updateReadAvailability();
    void    updateStateLabel();
    void    setRunningState(bool running);

    void recordOutput(const QString &text);
    /// 往左侧「运行日志」里写一行：`2026.10.11 13:50:30  开始写入`。
    /// isProblem = true 时用红色（错误/异常）。
    void logEvent(const QString &text, bool isProblem = false);
    /// 开始一圈测试（写模式下会重新确认、删旧文件、量空间、重新规划）。
    void startNextRound();
    /// 一圈跑完：写日志小结，还有下一圈就继续，否则整体收尾。
    void finishRound();
    void logRoundSummary();

    // ---- 性能测试流程 ----
    /// 预建测试文件 → 依次跑勾选的测试项，每项固定 10 秒。
    void startPerfRun();
    void startNextPerfItem();
    /// 一项内部的一趟：0 = 写入趟，1 = 读取趟（两趟叠在同一张时间图上）。
    void startPerfPass();
    void finishPerfRun();
    /// 把某一项的结果填进结果表，并同步进度条满量程。
    void fillPerfResult(FioConfig::PerfItem item, const FioResult &writeResult,
                        const FioResult &readResult);
    QVector<FioConfig::PerfItem> checkedPerfItems() const;
    void logPerfToChart(FioConfig::PerfItem item);
    /// 开始预建下一个测试文件（一个一个来，界面不卡）。
    void preallocNextFile();
    /// 重新计算所有结果格子的进度条满量程。
    void rescalePerfCells();
    /// 丢掉阶段末尾那一拍"收尾残段"采样（速率必然偏低）。
    void dropTrailingTailSample();
    /// 删过采样点之后整条重画图表。
    void rebuildChartFromSamples();
    /// 性能测试收尾时删掉测试文件。
    void deletePerfFiles();
    /// 记录某一项某一趟的逐点数据，供点击查看。
    void recordPerfTrace(double x, double readMiBs, double writeMiBs);
    /// 点击某个测试项的读取/写入格子：在**右侧图表**里显示这一项的读写两条曲线。
    void showPerfItemTrace(int itemIndex);
    /// 按测试项类型取读/写两条线的颜色（顺序项红/蓝，随机项黄/绿）。
    void perfItemColors(FioConfig::PerfItem item, QColor *readColor, QColor *writeColor) const;
    void reportProblem(const QString &title, const QString &text, bool critical = false);
    /// 测试还没真正开始就失败时的统一出口。
    /// 自检模式下必须仍然出报告并退出，否则会一直挂着。
    void abortStart(const QString &title, const QString &text, bool critical = false);
    void resetPhaseStats();
    void clearAllStats();
    /// 按当前档位 + 数据量决定折线抽稀步长（高密度 = 全画，低密度 = 自动）。
    void applyDensity();
    /// 往图表和采样数组里记一个点（起点锚、正常采样、终点闭合都走这里）。
    void appendSamplePoint(double readX, double writeX,
                           double readMiBs, double writeMiBs, double durationSec);
    void prepareCapacityAxis(qint64 totalBytes);
    /// 测试数据 CSV 的读写（对话框与自检都走这两个）。
    /// target 为空 → 加载进主窗口（保留给自检 --selftest-load-data 用）；
    /// target 非空 → **只往这张图表里画**，绝不碰主窗口的任何状态。
    bool saveRunDataTo(const QString &path, QString *error);
    bool loadRunDataFrom(const QString &path, QString *error, SpeedChart *target = nullptr);
    void beginWritePhase();
    void beginReadPhase();
    void finishWholeTest();
    RunStats computeRunStats(bool dropLastSample) const;

    bool clearTestFiles(const QString &folder, QString *error);
    bool ensureTestFolder(const QString &folder, QString *error);
    void updateDiskBarFor(const QString &root);

    QString locateFioExecutable() const;

    /// 用 `fio --enghelp` 探一次可用 IO 引擎，挑一个真正存在的定下来
    /// （见 FioConfig::candidateIoEngines），并把「IO 方式」那行刷新。
    /// 探测不出来就退回平台默认值，绝不因为探测失败而让界面停在错误状态。
    void applyDetectedIoEngine(const QString &fioPath);

    void writeSelfTestReport();
    /// 自检断言：绘图方式与密度两组单选都必须"恰好选中一个"。
    bool verifyExclusiveGroups();

    // ---- 参数控件 ----
    QLineEdit    *m_fioPathEdit     = nullptr;
    /// 磁盘 / 分区列表。**两层**：磁盘是顶层项（不可选），它下面的文件系统才是可选目标。
    QTreeWidget  *m_driveTree       = nullptr;
    /// 当前目标目录（测试文件建在 <m_targetDir>/ml_disktest 里，见 targetDir()）。
    QString       m_targetDir;
    QLabel       *m_targetLabel     = nullptr;
    QRadioButton *m_modeReliabilityRadio = nullptr;
    QRadioButton *m_modePerfRadio   = nullptr;
    QComboBox    *m_testModeCombo   = nullptr;
    QStackedWidget *m_paramStack    = nullptr;
    QWidget      *m_perfPage        = nullptr;
    QComboBox    *m_perfSizeCombo   = nullptr;
    QCheckBox    *m_perfChecks[4]   = {nullptr, nullptr, nullptr, nullptr};
    PerfCell     *m_perfReadCells[4]  = {nullptr, nullptr, nullptr, nullptr};
    PerfCell     *m_perfWriteCells[4] = {nullptr, nullptr, nullptr, nullptr};
    QComboBox    *m_modeCombo       = nullptr;
    QLabel       *m_blockSizeLabel  = nullptr;
    QSpinBox     *m_iodepthSpin     = nullptr;
    QSpinBox     *m_roundSpin       = nullptr;
    QCheckBox    *m_writeCheck      = nullptr;
    QCheckBox    *m_readCheck       = nullptr;
    QCheckBox    *m_verifyCheck     = nullptr;
    QPushButton  *m_startButton     = nullptr;
    QPushButton  *m_stopButton      = nullptr;
    QRadioButton *m_lineRadio       = nullptr;
    QRadioButton *m_scatterRadio    = nullptr;
    QRadioButton *m_densityHighRadio = nullptr;
    QRadioButton *m_densityLowRadio  = nullptr;
    /// 「密度」那一组控件的容器：它是折线图的子选项，散点图时整组隐藏。
    QWidget      *m_densityBox       = nullptr;
    /// 显式互斥组。不能只靠"同一个父窗口"这条隐式规则：
    /// 一旦控件的父子关系或插入方式变了，就可能出现两个同时选中或都不选。
    QButtonGroup *m_plotStyleGroup   = nullptr;
    QButtonGroup *m_densityGroup     = nullptr;

    // ---- 状态显示 ----
    QLabel       *m_writeValueLabel = nullptr;
    QLabel       *m_readValueLabel  = nullptr;
    QLabel       *m_elapsedLabel    = nullptr;
    QLabel       *m_stateLabel      = nullptr;
    QLabel       *m_avgWriteLabel   = nullptr;
    QLabel       *m_avgReadLabel    = nullptr;
    QLabel       *m_fioVersionLabel = nullptr;
    QLabel       *m_ioModeLabel     = nullptr;   ///< 「IO 方式」那行静态说明（引擎探测完要刷新）
    QLabel       *m_privilegeLabel  = nullptr;   ///< 状态栏右下角：提权模式 / 普通模式
    /// 校验失败的大字红色告警：平时隐藏，fio 一报出不一致就显示。
    /// 它和普通告警不同——普通告警只在日志里写一行红字，很容易被忽略；
    /// 数据不一致是这块盘**已经坏了**的确凿证据，必须一眼就能看到。
    QWidget      *m_verifyBanner      = nullptr;   ///< 告警条的容器（整条一起显示/隐藏）
    QLabel       *m_verifyBannerLabel = nullptr;   ///< 唯一一行：发现多少处不一致（大字红字）
    /// 本次测试累计发现的坏块数（每块算一处）。
    int           m_verifyFailures    = 0;
    /// 每个测试文件的字节数（按读取顺序）。坏块位置要靠它换算成图表横坐标。
    /// 用**逐文件大小**而不是"一个统一的单文件大小"：只读校验时用的是盘上现成的
    /// 文件，它们可能是别的大小组合（上一次用别的测试数据尺寸留下的），
    /// 末个文件还常常是零头。按统一大小算，位置会整体偏掉。
    QVector<qint64> m_verifyFileSizes;
    /// 兜底的单文件大小：拿不到逐文件大小时才用它。
    qint64        m_verifyFileSize    = 0;
    /// 把"第 N 个文件的第 off 字节"换算成图表横坐标（已处理容量，GiB）。
    /// 换算不出来（偏移未知）返回 < 0。
    double        verifyPositionToAxisX(int fileIndex, qint64 fileOffset) const;
    /// 开发排查用：强制把告警条挂出来（见 setDebugForceVerifyBanner）。
    bool          m_debugForceVerifyBanner = false;
    /// 把告警条按当前状态刷新：0 处就藏起来，>0 就显示大字红字。
    void          updateVerifyBanner();
    /// 开发排查：按 m_debugForceVerifyBanner 强制挂出告警条。
    void          applyDebugVerifyBanner();
    QProgressBar *m_diskBar         = nullptr;
    QPlainTextEdit *m_logView       = nullptr;   ///< 左侧下方的运行日志
    /// 本次测试的圈数：m_currentRound 从 1 数到 m_totalRounds。
    int m_currentRound = 0;
    int m_totalRounds  = 1;
    /// 当前这一圈的第一个 PhaseOutcome 在 m_outcomes 里的下标（写本圈小结用）。
    int m_roundOutcomeStart = 0;

    /// 性能模式：待跑的测试项队列与当前下标；预建文件的数量/单文件大小。
    QVector<FioConfig::PerfItem> m_perfQueue;
    int    m_perfQueueIndex = 0;
    int    m_perfFileCount  = 0;
    qint64 m_perfFileSize   = 0;
    int    m_perfPass       = 0;   ///< 0 = 读取趟，1 = 写入趟
    double m_perfPassBase   = -1.0; ///< 本趟第一个采样点的 intervalStartSec，用于 x 归零
    int    m_perfPassSamples = 0;   ///< 本趟已收进窗口的采样数（够 10 秒就不再收）
    /// 本阶段是否已经丢过收尾那一拍。onJobFinished 有可能被触发不止一次，
    /// 无条件丢弃必须配这个闸，否则会把曲线连着吃掉好几拍。
    bool   m_tailDropped = false;
    QVector<QPointF> m_perfReadTrace[4];
    QVector<QPointF> m_perfWriteTrace[4];

    /// 性能测试**整轮**的逐点数据，按"每一拍属于哪一项、哪一趟"存。
    ///
    /// 为什么不复用 m_readSamples / m_perfReadTrace：
    ///   * 主图表每换一项就 clear()，跑完四项只剩最后一项，前面的全丢了；
    ///   * m_perfReadTrace 存的是每项两趟各自的曲线，看不出先后顺序，
    ///     没法按时间轴还原成一份完整的逐点记录。
    /// 这份数组只增不改（重新开始测试时清空），保存 CSV 时直接写出去。
    QVector<double> m_perfRunTimes;      ///< 距本趟起点的秒数（0 ~ 10）
    QVector<double> m_perfRunRead;       ///< 读取速度（MiB/s）
    QVector<double> m_perfRunWrite;      ///< 写入速度（MiB/s）
    QVector<double> m_perfRunDurations;  ///< 这一拍覆盖的秒数
    QVector<quint8> m_perfRunFlags;      ///< bit0 = 有读，bit1 = 有写
    QVector<int>    m_perfRunItems;      ///< 这一拍属于哪个测试项（0~3）
    QVector<int>    m_perfRunPasses;     ///< 0 = 读取趟，1 = 写入趟
    /// 四个测试项的整趟结果（MiB/s），< 0 = 没测。结果表与 CSV 都用它。
    struct PerfValues
    {
        double read[4]  = {-1.0, -1.0, -1.0, -1.0};
        double write[4] = {-1.0, -1.0, -1.0, -1.0};
    };
    PerfValues m_perfValues;
    void resetPerfRunSamples();

    /// 一个盘的性能测试结果：四个格子的值 + 每一项每一趟的逐点数据。
    /// 按盘符分开存，切盘显示对应盘的结果；没测过的盘就是空的。
    struct PerfResult
    {
        double readValue[4] = {-1.0, -1.0, -1.0, -1.0};
        double writeValue[4] = {-1.0, -1.0, -1.0, -1.0};
        QVector<QPointF> readTrace[4];
        QVector<QPointF> writeTrace[4];
    };
    QHash<QString, PerfResult> m_perfResults;
    QString m_perfResultsTarget;   ///< 当前界面和内存里放的是哪个目标的结果
    /// 先把当前结果存回自己那个目标，再把 target 的结果显示出来。
    void switchPerfResultsTo(const QString &root);

    // ---- 图表里现在显示的是"谁的"数据 ----
    // 关键：**性能测试和可靠性测试各自独立**。同一块盘只测过性能时，
    // 切到可靠性模式就该显示"请先开始测试"，而不是把性能的曲线留在那里。
    // 从文件加载的历史数据走独立窗口，不参与这里的判断。
    QString m_chartOwnerTarget;  ///< 图表数据属于哪个目标
    FioConfig::TestMode m_chartOwnerMode = FioConfig::TestMode::Reliability;
    /// 按"当前模式 + 当前盘"决定显示图表还是"请先开始测试"。
    void updateChartPlaceholder();
    /// 预建文件的进度（一个 QTimer 一拍建一个，避免界面无响应）
    int    m_preallocIndex = 0;
    int    m_preallocCount = 0;
    qint64 m_preallocSize  = 0;
    QTimer *m_preallocTimer = nullptr;
    QFile *m_preallocFile = nullptr;   ///< 正在写的那个文件（分块写，一拍一块）
    qint64 m_preallocOffset = 0;
    FioResult m_perfWriteResult;
    int    m_perfResultCount = 0;
    QLabel       *m_diskSpaceLabel  = nullptr;   ///< 进度条右侧：可用 / 共
    QLabel       *m_diskBarLabel    = nullptr;   ///< 进度条右侧：已用 %
    SpeedChart   *m_chart           = nullptr;
    QStackedWidget *m_chartStack    = nullptr;   ///< 0 = 请先开始测试，1 = 图表
    QTimer       *m_versionDebounce = nullptr;

    FioRunner *m_runner = nullptr;

    /// 本阶段第一个采样点在**全过程数组**里的下标。
    /// 采样数组保存整轮数据（保存 CSV 要用），各阶段的统计靠这个下标切分。
    int m_phaseStartIndex = 0;
    /// 本阶段已经收到几拍采样（用来丢掉第 1 拍，见 onIoRate）。
    int m_phaseSampleCount = 0;
    /// 本阶段首拍已被丢弃，等第 2 拍来了要在容量 0 处补一个"起点锚"，
    /// 这样曲线视觉上仍从最左边开始。
    bool m_phaseNeedOriginAnchor = false;
    /// 一共丢掉了几个"偏慢的首拍"，写进自检报告作为证据。
    int m_skippedFirstSamples = 0;
    QVector<double> m_readSamples;
    QVector<double> m_writeSamples;
    QVector<double> m_sampleTimes;      ///< 读方向的横坐标（已处理容量，GB）
    QVector<double> m_writeTimes;       ///< 写方向的横坐标（两个方向推进速度不同）
    QVector<double> m_sampleDurations;
    /// 每个采样点属于哪些方向：bit0=有读、bit1=有写（保存 CSV 时要带上）
    QVector<quint8> m_sampleFlags;
    QStringList     m_logLines;
    QStringList     m_observedStates;
    QString         m_lastError;
    QStringList     m_diskSummary;   ///< 最近一次枚举到的磁盘/分区，写进自检报告便于排查
    /// 被规则排除掉的卷（网络驱动器 / 内存盘 / 没有物理磁盘的映射盘）。
    /// 也写进自检报告 —— 免得用户只看到"我的盘怎么不见了"却查不出原因。
    QStringList     m_ignoredVolumes;

    double   m_phaseOffsetSec = 0.0;   ///< 保留字段：横轴改用容量后不再需要时间偏移
    double   m_elapsedSeconds = 0.0;
    /// 用户已经点了「停止」：此后不再接收任何采样点，曲线就停在按下那一刻。
    bool     m_stopRequested = false;
    /// 横轴以容量计：本阶段起点在横轴上的字节位置、本阶段要处理的字节数，
    /// 以及本阶段已经读/写了多少字节（两个方向推进速度不同，分开累加）。
    qint64   m_phaseByteBase  = 0;
    qint64   m_phaseByteTotal = 0;
    qint64   m_axisByteTotal  = 0;   ///< 横轴上限（整轮测试要处理的字节数）
    double   m_readBytesInPhase  = 0.0;
    double   m_writeBytesInPhase = 0.0;
    Phase    m_phase = Phase::Idle;
    /// 曲线的**可见性**：只要任一阶段产生过该方向的数据就一直显示，
    /// 不能因为进入只读阶段就把已经画好的写入曲线藏掉。
    bool     m_plotRead  = true;
    bool     m_plotWrite = true;
    /// **当前阶段**是否有该方向的数据（决定加不加点、卡片显不显示）。
    bool     m_phaseRead  = true;
    bool     m_phaseWrite = true;

    FioConfig         m_activeConfig;
    FioConfig::FilePlan m_activePlan;
    QString           m_jobFilePath;
    QVector<PhaseOutcome> m_outcomes;

    // ---- 自检 ----
    bool    m_selfTestMode = false;
    QString m_selfTestOutDir;
    QString m_selfTestTarget;
    int     m_selfTestFillLimit = 0;
    QString m_selfTestModeKey;
    int     m_selfTestStopAfter = 0;
    bool    m_selfTestScatter = false;
    bool    m_selfTestReadOnly = false;
    bool    m_selfTestLowDensity = false;
    int     m_selfTestRounds = 0;
    bool    m_selfTestPerf = false;
    int     m_selfTestPerfMiB = 0;
    QString m_selfTestSaveData;   ///< 自检：跑完后把测试数据存到这个 CSV
    QString m_selfTestLoadData;   ///< 自检：直接加载这个 CSV 画图，不跑测试
    bool    m_resultWindowOk = false;    ///< 自检：结果窗口打开成功了吗
    int     m_resultWindowItem = -1;     ///< 自检：结果窗口当前单独显示的是哪一项
    /// 自检：加载过的那份数据的解析结果，用来对"模式判对了没、四项有没有值"
    /// 出可断言的结论（截图上肉眼看数字不算证据）。
    bool    m_loadedRunDataValid = false;
    RunData m_loadedRunData;
    /// 正在重建驱动器列表。重建过程中 setRowCount(0) 会清空选中并触发
    /// itemSelectionChanged，如果不挡住，那一刻"当前没有选中分区"会被当成
    /// "分区里没有测试文件"，从而把「读取」的勾选悄悄取消掉。
    bool m_refreshingDrives = false;
    /// 用户已经确认过"覆盖旧测试文件"的目录；同一个目录不再重复问。
    QString  m_overwriteConfirmedFor;
    /// 程序自己改复选框时置位，避免触发覆盖确认对话框。
    bool     m_suppressOverwritePrompt = false;
    QString  m_loadedDataFrom;     ///< 本次图表内容来自哪个 CSV（空 = 实时采集）
    bool    m_loadedDataOk = false;
    bool    m_exclusiveGuardOk = true;   ///< 单选互斥断言的结果
};
