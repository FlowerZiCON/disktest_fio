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

#include "mainwindow.h"

#include "perfcell.h"
#include "resultwindow.h"
#include "rundata.h"
#include "speedchart.h"
#include "uitext.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>

namespace {

/// 生成一张"标题 + 大字数值"的小卡片。
QFrame *createCard(const QString &title, QLabel **valueLabel, const QColor &color, QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setFrameShape(QFrame::StyledPanel);
    // 118: 六张卡片并排要占掉右边一大截，原来 126 会把窗口的最小可用宽度顶到
    // 1240 以上。卡片里就是"标题 + 一个大数字"，收窄这一点不影响读。
    frame->setMinimumWidth(118);

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(1);

    auto *titleLabel = new QLabel(title, frame);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSizeF(qMax(7.5, titleFont.pointSizeF() - 1.0));
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color:#666;"));

    auto *value = new QLabel(QStringLiteral("--"), frame);
    QFont valueFont = value->font();
    valueFont.setPointSizeF(valueFont.pointSizeF() + 4.0);
    valueFont.setBold(true);
    value->setFont(valueFont);
    value->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));

    layout->addWidget(titleLabel);
    layout->addWidget(value);

    *valueLabel = value;
    return frame;
}

QString formatSpeed(double mib)
{
    return RunData::formatSpeed(mib);
}

QString formatBytes(quint64 bytes)
{
    return RunData::formatBytes(qint64(bytes));
}

QString formatDuration(qint64 seconds)
{
    return RunData::formatDuration(seconds);
}

/// 测试数据 CSV 的格式标记。定义在 rundata.h 里（解析那一侧也要用），
/// 这里只是转发，免得两边各写一份字符串。
const char *kRunDataMagic = RunData::magic();
const char *kSamplesMarker = RunData::samplesMarker();
const char *kRunMarker = RunData::runMarker();

/// 这条告警是不是"数据校验失败"这一类。
///
/// 不能拿标签整体去比：上报时会被加上 `× N` 的重复次数后缀
/// （见 FioRunner::finalizeResult），字符串就不是原来的样子了。
/// 这里认的是标签里那段**不会变**的固定文字。
bool isVerifyRelatedWarning(const QString &warning)
{
    return warning.contains(QStringLiteral("verify failed"))
           || warning.contains(QStringLiteral("校验失败"))
           || warning.contains(QStringLiteral("非法字节序列"))
           || warning.contains(QStringLiteral("校验头不匹配"));
}

/// 把卷根路径统一成 "G:/" 的形式，好做不区分大小写的比较。
/// 卷枚举给出的 rootPath 是 "G:/"，而用户/命令行常写成 "G:" 或 "G:\"，
/// 直接字符串比会不相等——曾经因此让自检悄悄退回第一个分区（系统盘 C:）并往上写数据。
QString normalizedRoot(const QString &path)
{
    const QString s = QDir::fromNativeSeparators(path.trimmed());
    if (s.size() >= 2 && s.at(1) == QLatin1Char(':'))
        return s.left(2).toUpper() + QLatin1Char('/');
    return s;
}

} // namespace

namespace {
/// 这个 fio 是不是"自带的"（exe 目录里带的，或源码树里的 third_party\fio）。
/// 定义在本文件后面的 locateFioExecutable 旁边，这里先声明，供构造函数用。
bool fioLooksBundled(const QString &path);
} // namespace
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_runner(new FioRunner(this))
{
    buildUi();
    buildMenu();
    // 每次启动都回到出厂默认值，不读配置文件（见 resetToDefaultSettings）
    resetToDefaultSettings();
    // 枚举盘符，但**不预选任何盘**：startup=true 时不会回退到第一行。
    // 以前这里会静默选中第一块盘，而第一行常常是系统盘。
    if (refreshDriveList(true))
        refreshTargetInfo();
    updateReadAvailability();

    connect(m_runner, &FioRunner::ioRate, this, &MainWindow::onIoRate);
    connect(m_runner, &FioRunner::logText, this, &MainWindow::onRunnerOutput);
    connect(m_runner, &FioRunner::warningRaised, this, &MainWindow::onRunnerWarning);
    connect(m_runner, &FioRunner::verifyFailureRaised, this, &MainWindow::onVerifyFailure);
    connect(m_runner, &FioRunner::jobFinished, this, &MainWindow::onJobFinished);

    setRunningState(false);
    updateStateLabel();
    onPlotStyleChanged();   // 按当前绘图方式决定"密度"那一组显不显示
    m_versionDebounce->start();   // 异步探测 fio 版本，不阻塞启动
}

MainWindow::~MainWindow()
{
    m_runner->stop();
}

// ---------------------------------------------------------------- 界面搭建

void MainWindow::buildUi()
{
    updateWindowTitle();
    // 默认尺寸：把高度收下来，宽度保持足够放下右侧六张卡片。
    //
    // 原来是 1360x980，在 1080p 上几乎顶到任务栏（工作区才 1019 高），
    // 打开程序第一件事就是把它拖小。
    //
    // 宽度没敢再压：卡片行 + 图表的内部最小宽度就卡在 1360 上下，
    // 压到 1220 时右边两张卡片会被裁掉（Qt 不会再给横向滚动条，
    // 那是看不见的内容，不能接受）。要真正变窄得先重构卡片行，
    // 那是另一件事了。
    resize(1360, 760);

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    auto *panel = new QWidget(central);
    buildParamPanel(panel);

    auto *scroll = new QScrollArea(central);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 420~470：左栏是"卡片式表单"，本身不需要太宽；
    // 原来能撑到 520，多出来的宽度全是从右边那排状态卡片和图表上抠走的。
    // 收窄之后窗口整体也能跟着变窄，而右侧六张卡片不用再被裁掉。
    scroll->setMinimumWidth(420);
    scroll->setMaximumWidth(470);

    rootLayout->addWidget(scroll);
    rootLayout->addWidget(buildChartPanel(), 1);
    setCentralWidget(central);
    updateWindowTitle();   // 模式下拉框这时才建好，标题要再设一次

    // 状态栏右侧：先是权限模式，再是 fio 引擎版本
    m_privilegeLabel = new QLabel(this);
    const bool elevated = isProcessElevated();
    m_privilegeLabel->setText(elevated ? kModeElevated
                                       : kModeNormal);
    m_privilegeLabel->setStyleSheet(
        elevated ? QStringLiteral("color:#1a7f37; font-weight:bold; padding:0 6px;")
                 : QStringLiteral("color:#8a6d00; font-weight:bold; padding:0 6px;"));
    m_privilegeLabel->setToolTip(
        elevated ? kModeElevatedTip
                 : kModeNormalTip);
    statusBar()->addPermanentWidget(m_privilegeLabel);

    m_fioVersionLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_fioVersionLabel);
    statusBar()->showMessage(kStatusSelectDriveHint);
}

void MainWindow::buildParamPanel(QWidget *panel)
{
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(8);

    // ---- 测试模式（选在驱动器列表上方）----
    auto *modeBox = new QGroupBox(kModeGroupTitle, panel);
    auto *modeLayout = new QHBoxLayout(modeBox);
    m_modeReliabilityRadio = new QRadioButton(kModeNameReliability, modeBox);
    m_modePerfRadio = new QRadioButton(kModeNamePerformance, modeBox);
    m_modeReliabilityRadio->setChecked(true);
    m_modeReliabilityRadio->setToolTip(kModeReliabilityTip);
    m_modePerfRadio->setToolTip(kModePerformanceTip);
    modeLayout->addWidget(m_modeReliabilityRadio);
    modeLayout->addWidget(m_modePerfRadio);
    modeLayout->addStretch(1);
    connect(m_modeReliabilityRadio, &QRadioButton::toggled, this, &MainWindow::onTestModeChanged);
    connect(m_modePerfRadio, &QRadioButton::toggled, this, &MainWindow::onTestModeChanged);

    // ---- fio 可执行文件 ----
    auto *fioBox = new QGroupBox(kGroupFioEngine, panel);
    auto *fioLayout = new QVBoxLayout(fioBox);
    auto *fioRow = new QHBoxLayout;
    m_fioPathEdit = new QLineEdit(fioBox);
    m_fioPathEdit->setPlaceholderText(kFioPathPlaceholder);
    auto *fioBrowse = new QPushButton(kBtnBrowse, fioBox);
    fioRow->addWidget(m_fioPathEdit, 1);
    fioRow->addWidget(fioBrowse);
    fioLayout->addLayout(fioRow);
    connect(fioBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseFio);
    m_versionDebounce = new QTimer(this);
    m_versionDebounce->setSingleShot(true);
    m_versionDebounce->setInterval(400);
    connect(m_versionDebounce, &QTimer::timeout, this, [this]() {
        const QString version = FioRunner::queryVersion(m_fioPathEdit->text().trimmed());
        const QString path = m_fioPathEdit->text().trimmed();
        m_fioVersionLabel->setText(
            version.isEmpty()
                ? kFioEngineNotFound
                : kFioEngineVersion.arg(version, fioLooksBundled(path) ? kFioBundled
                                                                       : kFioFromSystem));
    });
    connect(m_fioPathEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        m_versionDebounce->start();
    });

    // ---- 驱动器 ----
    auto *driveBox = new QGroupBox(kGroupTargetDrive, panel);
    buildDrivePanel(driveBox);

    // ---- 负载参数 ----
    auto *loadBox = new QGroupBox(kGroupTestParams, panel);
    auto *loadForm = new QFormLayout(loadBox);
    loadForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_modeCombo = new QComboBox(loadBox);
    m_modeCombo->addItem(kModeItemSeq,
                         FioConfig::modeKey(FioConfig::RwMode::SequentialWriteRead));
    m_modeCombo->addItem(kModeItemRand,
                         FioConfig::modeKey(FioConfig::RwMode::RandomWriteRead));
    loadForm->addRow(kLabelRwMode, m_modeCombo);

    m_blockSizeLabel = new QLabel(loadBox);
    m_blockSizeLabel->setStyleSheet(QStringLiteral("color:#666;"));
    loadForm->addRow(kLabelBlockSize, m_blockSizeLabel);

    m_iodepthSpin = new QSpinBox(loadBox);
    m_iodepthSpin->setRange(1, 1024);
    m_iodepthSpin->setValue(8);   // 默认队列深度 8，与 resetToDefaultSettings 保持一致
    loadForm->addRow(kLabelIodepth, m_iodepthSpin);

    m_roundSpin = new QSpinBox(loadBox);
    m_roundSpin->setRange(1, 9999);
    m_roundSpin->setSuffix(kRoundsSuffix);
    m_roundSpin->setValue(1);
    m_roundSpin->setToolTip(kRoundsTip);
    loadForm->addRow(kLabelRounds, m_roundSpin);

    // IO 引擎与直接 I/O 都不再提供选择：Windows 上只有 windowsaio 支持直接 I/O，
    // 而直接 I/O 才能绕过系统缓存、测到磁盘本身。这里只做静态说明。
    auto *ioModeLabel = new QLabel(
        kIoModeDirectFmt.arg(FioConfig::ioEngine()), loadBox);
    ioModeLabel->setStyleSheet(QStringLiteral("color:#666;"));
    ioModeLabel->setToolTip(kIoModeTip);
    loadForm->addRow(kLabelIoMode, ioModeLabel);

    // ---- 阶段与校验 ----
    auto *phaseBox = new QGroupBox(kGroupPhases, panel);
    auto *phaseLayout = new QVBoxLayout(phaseBox);

    m_writeCheck = new QCheckBox(kCheckWrite, phaseBox);
    m_writeCheck->setChecked(true);
    m_readCheck = new QCheckBox(kCheckRead, phaseBox);
    m_readCheck->setChecked(true);
    m_verifyCheck = new QCheckBox(kCheckVerify, phaseBox);
    m_verifyCheck->setChecked(true);
    phaseLayout->addWidget(m_writeCheck);
    phaseLayout->addWidget(m_readCheck);
    phaseLayout->addWidget(m_verifyCheck);

    connect(m_writeCheck, &QCheckBox::toggled, this, &MainWindow::onWriteCheckToggled);
    connect(m_writeCheck, &QCheckBox::toggled, this, &MainWindow::onOptionsChanged);
    connect(m_readCheck, &QCheckBox::toggled, this, &MainWindow::onOptionsChanged);
    connect(m_verifyCheck, &QCheckBox::toggled, this, &MainWindow::onOptionsChanged);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOptionsChanged);

    // ---- 操作按钮 ----
    auto *buttonRow = new QHBoxLayout;
    m_startButton = new QPushButton(kBtnStart, panel);
    m_startButton->setMinimumHeight(38);
    m_stopButton = new QPushButton(kBtnStop, panel);
    m_stopButton->setMinimumHeight(38);
    buttonRow->addWidget(m_startButton, 2);
    buttonRow->addWidget(m_stopButton, 1);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStop);

    auto *hint = new QLabel(kParamHint, panel);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#666;"));

    // ---- 参数页：可靠性 / 性能，二选一 ----
    m_paramStack = new QStackedWidget(panel);

    auto *reliabilityPage = new QWidget(m_paramStack);
    auto *reliabilityLayout = new QVBoxLayout(reliabilityPage);
    reliabilityLayout->setContentsMargins(0, 0, 0, 0);
    reliabilityLayout->setSpacing(8);
    reliabilityLayout->addWidget(loadBox);
    reliabilityLayout->addWidget(phaseBox);

    m_perfPage = new QWidget(m_paramStack);
    buildPerfPage(m_perfPage);

    m_paramStack->addWidget(reliabilityPage);
    m_paramStack->addWidget(m_perfPage);

    layout->addWidget(fioBox);
    layout->addWidget(modeBox);
    layout->addWidget(driveBox);
    layout->addWidget(m_paramStack);
    layout->addLayout(buttonRow);
    layout->addWidget(hint);

    // ---- 运行日志：占满左侧下方的空白区域 ----
    auto *logBox = new QGroupBox(kGroupRunLog, panel);
    auto *logLayout = new QVBoxLayout(logBox);
    logLayout->setContentsMargins(6, 6, 6, 6);
    m_logView = new QPlainTextEdit(logBox);
    m_logView->setReadOnly(true);
    m_logView->setUndoRedoEnabled(false);
    m_logView->setMaximumBlockCount(2000);   // 长时间跑也不会无限涨
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logView->setMinimumHeight(190);
    m_logView->setPlaceholderText(kLogPlaceholder);
    QFont logFont = m_logView->font();
    logFont.setFamilies({QStringLiteral("Consolas"), QStringLiteral("Courier New")});
    logFont.setPointSizeF(qMax(8.0, logFont.pointSizeF() - 1.0));
    m_logView->setFont(logFont);
    logLayout->addWidget(m_logView);

    layout->addWidget(logBox, 1);
}

void MainWindow::buildDrivePanel(QWidget *driveBox)
{
    auto *layout = new QVBoxLayout(driveBox);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_driveTable = new QTableWidget(0, 4, driveBox);
    m_driveTable->setHorizontalHeaderLabels(
        {kColLetter, kColDevice,
         kColUsedTotal, kColUsage});
    m_driveTable->verticalHeader()->setVisible(false);
    m_driveTable->setShowGrid(false);
    m_driveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_driveTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_driveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_driveTable->setAlternatingRowColors(true);
    m_driveTable->setMaximumHeight(126);
    // 选中高亮改成浅蓝色（默认的系统高亮偏深，和交替行底色对比太强）
    m_driveTable->setStyleSheet(QStringLiteral(
        "QTableWidget::item:selected { background: #cce8ff; color: #10314f; }"
        "QTableWidget::item:selected:active { background: #b6dcff; color: #10314f; }"));
    m_driveTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_driveTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_driveTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_driveTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_driveTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onDriveSelectionChanged);

    m_targetLabel = new QLabel(driveBox);
    m_targetLabel->setWordWrap(true);
    m_targetLabel->setStyleSheet(QStringLiteral("color:#333;"));

    auto *hintRow = new QHBoxLayout;
    auto *hint = new QLabel(kDriveHintClick, driveBox);
    hint->setStyleSheet(QStringLiteral("color:#666;"));
    hint->setWordWrap(true);
    auto *refresh = new QPushButton(kBtnRefresh, driveBox);
    refresh->setMaximumWidth(70);
    connect(refresh, &QPushButton::clicked, this, [this]() {
        if (refreshDriveList())
            refreshTargetInfo();
        updateReadAvailability();
    });
    hintRow->addWidget(hint, 1);
    hintRow->addWidget(refresh);

    layout->addWidget(m_driveTable);
    layout->addWidget(m_targetLabel);
    layout->addLayout(hintRow);
}

QWidget *MainWindow::buildChartPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *cards = new QHBoxLayout;
    cards->setSpacing(4);
    cards->addWidget(createCard(kCardReadNow, &m_readValueLabel,
                                QColor(220, 30, 30), panel));
    cards->addWidget(createCard(kCardWriteNow, &m_writeValueLabel,
                                QColor(30, 70, 220), panel));
    cards->addWidget(createCard(kCardReadAvg, &m_avgReadLabel,
                                QColor(220, 30, 30), panel));
    cards->addWidget(createCard(kCardWriteAvg, &m_avgWriteLabel,
                                QColor(30, 70, 220), panel));
    cards->addWidget(createCard(kCardElapsed, &m_elapsedLabel,
                                QColor(60, 60, 60), panel));
    cards->addWidget(createCard(kCardState, &m_stateLabel,
                                QColor(60, 60, 60), panel));
    cards->addStretch(1);

    // ---- 校验失败的大字红色告警 ----
    // 放在右侧面板最显眼的位置（卡片行下面、图表上面）。
    // 平时整条藏起来，只有 fio 真的报出数据不一致才出现：
    // 可靠性测试里"这块盘坏了"是结论性的事件，不能只写进日志等用户去翻。
    m_verifyBanner = new QWidget(panel);
    m_verifyBanner->setObjectName(QStringLiteral("verifyBanner"));
    m_verifyBanner->setStyleSheet(QStringLiteral(
        "#verifyBanner { background:#fff2f0; border:2px solid #d32020; border-radius:4px; }"));
    auto *bannerLayout = new QVBoxLayout(m_verifyBanner);
    bannerLayout->setContentsMargins(12, 8, 12, 8);
    bannerLayout->setSpacing(2);

    m_verifyBannerLabel = new QLabel(m_verifyBanner);
    QFont bannerFont = m_verifyBannerLabel->font();
    bannerFont.setPointSizeF(qMax(14.0, bannerFont.pointSizeF() + 8.0));
    bannerFont.setBold(true);
    m_verifyBannerLabel->setFont(bannerFont);
    m_verifyBannerLabel->setStyleSheet(QStringLiteral("color:#d32020; border:none;"));

    // 只留一句结论。不摆 fio 原话（偏移量在日志里有逐处明细）：
    // 告警条要的是"这块盘坏了，赶紧备份换盘"，不是让人在这里读日志。
    auto *bannerNote = new QLabel(kVerifyBannerNote, m_verifyBanner);
    bannerNote->setWordWrap(true);
    QFont noteFont = bannerNote->font();
    noteFont.setPointSizeF(qMax(10.0, noteFont.pointSizeF() + 2.0));
    bannerNote->setFont(noteFont);
    bannerNote->setStyleSheet(QStringLiteral("color:#8a1010; border:none;"));

    bannerLayout->addWidget(m_verifyBannerLabel);
    bannerLayout->addWidget(bannerNote);
    m_verifyBanner->setVisible(false);

    // 磁盘占用进度条：条形图 + 右侧两列右对齐的读数（空间 / 百分比）
    auto *diskRow = new QHBoxLayout;
    diskRow->setSpacing(8);
    auto *diskTitle = new QLabel(kLabelTargetDriveColon, panel);
    m_diskBar = new QProgressBar(panel);
    m_diskBar->setRange(0, 1000);
    m_diskBar->setValue(0);
    m_diskBar->setTextVisible(false);   // 读数放到外面，两条信息才能对齐
    m_diskBar->setMinimumHeight(22);
    m_diskBar->setMinimumWidth(160);

    // 两列固定宽度的右对齐读数：刷新时数字不会左右跳动
    m_diskSpaceLabel = new QLabel(panel);
    m_diskSpaceLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_diskSpaceLabel->setMinimumWidth(210);
    m_diskBarLabel = new QLabel(panel);
    m_diskBarLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_diskBarLabel->setMinimumWidth(96);
    QFont readoutFont = m_diskSpaceLabel->font();
    readoutFont.setBold(true);
    m_diskSpaceLabel->setFont(readoutFont);
    m_diskBarLabel->setFont(readoutFont);

    diskRow->addWidget(diskTitle);
    diskRow->addWidget(m_diskBar, 1);
    diskRow->addWidget(m_diskSpaceLabel);
    diskRow->addWidget(m_diskBarLabel);

    auto *chartBox = new QGroupBox(kGroupChart, panel);
    auto *chartLayout = new QVBoxLayout(chartBox);
    chartLayout->setContentsMargins(6, 6, 6, 6);

    auto *styleRow = new QHBoxLayout;
    auto *styleLabel = new QLabel(kLabelPlotStyle, chartBox);

    // 折线图 / 散点图：**必须互斥**。用显式 QButtonGroup，
    // 不能只依赖"同一个父窗口"这条隐式规则（插入方式一变就可能出现同时选中或都不选）。
    // 互斥组还有个好处：已选中的那个再点也不会被取消，保证永远有一个被选中。
    m_lineRadio = new QRadioButton(kPlotLine, chartBox);
    m_scatterRadio = new QRadioButton(kPlotScatter, chartBox);
    m_plotStyleGroup = new QButtonGroup(this);
    m_plotStyleGroup->setExclusive(true);
    m_plotStyleGroup->addButton(m_lineRadio);
    m_plotStyleGroup->addButton(m_scatterRadio);
    m_lineRadio->setChecked(true);
    connect(m_lineRadio, &QRadioButton::toggled, this, &MainWindow::onPlotStyleChanged);
    connect(m_scatterRadio, &QRadioButton::toggled, this, &MainWindow::onPlotStyleChanged);

    // 密度是「折线图」的子选项：排在两个绘图方式**之后**，缩进一格，
    // 并且只在折线图下出现（选散点图时整组隐藏）。
    m_densityBox = new QWidget(chartBox);
    auto *densityRow = new QHBoxLayout(m_densityBox);
    densityRow->setContentsMargins(18, 0, 0, 0);
    densityRow->setSpacing(6);
    auto *densityLabel = new QLabel(kLabelDensity, m_densityBox);
    m_densityHighRadio = new QRadioButton(kDensityHigh, m_densityBox);
    m_densityLowRadio = new QRadioButton(kDensityLow, m_densityBox);
    m_densityGroup = new QButtonGroup(this);
    m_densityGroup->setExclusive(true);
    m_densityGroup->addButton(m_densityHighRadio);
    m_densityGroup->addButton(m_densityLowRadio);
    m_densityHighRadio->setChecked(true);
    m_densityHighRadio->setToolTip(kDensityHighTip);
    m_densityLowRadio->setToolTip(kDensityLowTip);
    connect(m_densityHighRadio, &QRadioButton::toggled, this, &MainWindow::onDensityChanged);
    connect(m_densityLowRadio, &QRadioButton::toggled, this, &MainWindow::onDensityChanged);
    densityRow->addWidget(densityLabel);
    densityRow->addWidget(m_densityHighRadio);
    densityRow->addWidget(m_densityLowRadio);

    styleRow->addWidget(styleLabel);
    styleRow->addWidget(m_lineRadio);
    styleRow->addWidget(m_scatterRadio);
    styleRow->addWidget(m_densityBox);
    styleRow->addStretch(1);

    m_chart = new SpeedChart(chartBox);

    // 刚打开程序时先不画图表，显一行"请先开始测试"；
    // 第一次点开始测试再切到真正的图表页。
    m_chartStack = new QStackedWidget(chartBox);
    auto *placeholder = new QLabel(kChartPlaceholder, m_chartStack);
    placeholder->setAlignment(Qt::AlignCenter);
    QFont placeholderFont = placeholder->font();
    placeholderFont.setPointSizeF(qMax(12.0, placeholderFont.pointSizeF() + 6.0));
    placeholder->setFont(placeholderFont);
    placeholder->setStyleSheet(QStringLiteral("color:#9a9a9a;"));
    m_chartStack->addWidget(placeholder);   // 下标 0
    m_chartStack->addWidget(m_chart);       // 下标 1
    m_chartStack->setCurrentIndex(0);

    chartLayout->addLayout(styleRow);
    chartLayout->addWidget(m_chartStack, 1);

    layout->addLayout(cards);
    layout->addWidget(m_verifyBanner);
    layout->addLayout(diskRow);
    layout->addWidget(chartBox, 1);
    return panel;
}

void MainWindow::buildMenu()
{
    QMenu *fileMenu = menuBar()->addMenu(kMenuFile);
    QAction *openDir = fileMenu->addAction(kMenuOpenTestDir);
    connect(openDir, &QAction::triggered, this, &MainWindow::onOpenTestDirectory);
    QAction *cleanup = fileMenu->addAction(kMenuCleanupFiles);
    connect(cleanup, &QAction::triggered, this, &MainWindow::onCleanupTestFiles);
    fileMenu->addSeparator();
    QAction *saveData = fileMenu->addAction(kMenuSaveData);
    saveData->setToolTip(kMenuSaveDataTip);
    connect(saveData, &QAction::triggered, this, &MainWindow::onSaveRunData);
    QAction *loadData = fileMenu->addAction(kMenuLoadData);
    loadData->setToolTip(kMenuLoadDataTip);
    connect(loadData, &QAction::triggered, this, &MainWindow::onLoadRunData);
    fileMenu->addSeparator();
    QAction *saveLog = fileMenu->addAction(kMenuSaveDiagnostics);
    connect(saveLog, &QAction::triggered, this, &MainWindow::onSaveDiagnostics);
    fileMenu->addSeparator();
    QAction *quit = fileMenu->addAction(kMenuQuit);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu *helpMenu = menuBar()->addMenu(kMenuHelp);
    QAction *about = helpMenu->addAction(kMenuAbout);
    connect(about, &QAction::triggered, this, &MainWindow::onAbout);
}

// ---------------------------------------------------------------- 配置读写

FioConfig MainWindow::configFromUi() const
{
    FioConfig cfg;
    cfg.targetRoot  = selectedRoot();
    cfg.testMode    = currentTestMode();
    if (m_perfSizeCombo)
        cfg.perfDataBytes = m_perfSizeCombo->currentData().toLongLong();
    for (int i = 0; i < 4; ++i)
        cfg.perfItemChecked[i] = m_perfChecks[i] ? m_perfChecks[i]->isChecked() : true;
    cfg.mode        = FioConfig::modeFromKey(m_modeCombo->currentData().toString());
    cfg.iodepth     = m_iodepthSpin->value();
    cfg.rounds      = m_roundSpin->value();
    cfg.doWrite     = m_writeCheck->isChecked();
    cfg.doRead      = m_readCheck->isChecked();
    cfg.verify      = m_verifyCheck->isChecked();
    cfg.fillLimitMiB = m_selfTestFillLimit;
    return cfg;
}

void MainWindow::applyConfigToUi(const FioConfig &cfg)
{
    const int modeIndex = m_modeCombo->findData(FioConfig::modeKey(cfg.mode));
    if (modeIndex >= 0)
        m_modeCombo->setCurrentIndex(modeIndex);

    m_iodepthSpin->setValue(cfg.iodepth);
    m_roundSpin->setValue(qMax(1, cfg.rounds));

    if (m_modePerfRadio)
        m_modePerfRadio->setChecked(cfg.testMode == FioConfig::TestMode::Performance);
    if (m_perfSizeCombo) {
        const int sizeIndex = m_perfSizeCombo->findData(QVariant::fromValue(cfg.perfDataBytes));
        if (sizeIndex >= 0)
            m_perfSizeCombo->setCurrentIndex(sizeIndex);
    }
    for (int i = 0; i < 4; ++i) {
        if (m_perfChecks[i])
            m_perfChecks[i]->setChecked(cfg.perfItemChecked[i]);
    }

    m_writeCheck->setChecked(cfg.doWrite);
    m_readCheck->setChecked(cfg.doRead);
    m_verifyCheck->setChecked(cfg.verify);
}

void MainWindow::resetToDefaultSettings()
{
    // 每次启动都回到同一套**出厂默认值**，不读也不写任何配置文件。
    //
    // 以前这里会 loadSettings()/saveSettings()，把上次用的模式、盘符、参数
    // 都存到注册表/ini 里下次还原。问题是"启动状态"会随上一次的操作漂移：
    // 上一次拿性能模式跑过，这次打开就是性能模式；上一次选的是系统盘，
    // 这次一进来就选着系统盘，很容易顺手开始测错盘。
    // 硬盘测试工具的启动状态应该永远是确定的。
    //
    // 想要的那套默认值：
    //   可靠性测试 · 不预选任何盘 · 顺序读写 · 队列深度 8 · 1 圈 · 写入/读取/校验全开
    //   性能测试 · 四项全勾 · 测试数据 1 GiB
    FioConfig cfg;
    cfg.testMode  = FioConfig::TestMode::Reliability;
    cfg.mode      = FioConfig::RwMode::SequentialWriteRead;
    cfg.iodepth   = 8;
    cfg.rounds    = 1;
    cfg.doWrite   = true;
    cfg.doRead    = true;
    cfg.verify    = true;
    cfg.perfDataBytes = qint64(1024) * 1024 * 1024;
    for (int i = 0; i < 4; ++i)
        cfg.perfItemChecked[i] = true;
    applyConfigToUi(cfg);

    // 绘图方式与密度也回到默认（折线图 + 高密度）
    m_lineRadio->setChecked(true);
    m_densityHighRadio->setChecked(true);
    onPlotStyleChanged();

    // fio 路径按固定规则重新解析一次：程序目录里的 fio\fio.exe → 系统 PATH。
    // 以前这一步藏在 loadSettings() 里，把设置功能摘掉时差点连它一起漏掉
    // （漏了的症状：exe 旁边明明放着 fio，界面却是"引擎: 未找到"）。
    m_fioPathEdit->setText(locateFioExecutable());

    // 盘符**不预选**：交给用户自己挑，避免默认落在系统盘上。
    // 清掉可能残留的 preferredRoot，再让 refreshDriveList 跳过"回退到第一行"。
    if (m_driveTable) {
        m_driveTable->setProperty("preferredRoot", QString());
        m_driveTable->clearSelection();
    }
}

namespace {

/// 这个 fio 是不是"程序自带的那一份"（就在 exe 旁边的 fio\ 里）。
/// 只影响界面标签显示（自带/系统），不影响用哪个。
bool fioLooksBundled(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (!clean.endsWith(QStringLiteral("/fio/fio.exe"), Qt::CaseInsensitive))
        return false;
    return clean.startsWith(QDir::cleanPath(QCoreApplication::applicationDirPath())
                            + QLatin1Char('/'));
}

/// 程序自带的 fio：**只看 exe 所在目录**，找不到就返回空。
///
/// 以前这里会从 exe 目录往上翻最多 5 层，去找源码树里的 third_party/fio。
/// 那等于拿程序目录当起点扫盘，装到 D:\Tools\ 这类地方时行为完全不可预测
/// （测的是哪份 fio 取决于目录长什么样）。现在只认确定的位置。
///
/// 单独抽成**接收目录参数的静态函数**，好让自检直接喂一个临时目录进去验证，
/// 不用真去造一个带 fio 的程序目录。
QString bundledFioPathFor(const QString &appDir)
{
    const QString candidate =
        QDir(appDir).absoluteFilePath(QStringLiteral("fio/fio.exe"));
    return QFileInfo::exists(candidate) ? QDir::cleanPath(candidate) : QString();
}

} // namespace

QString MainWindow::locateFioExecutable() const
{
    // 查找顺序，只有两步，找不到就**不猜**：
    //   1. 程序所在目录里的 fio\fio.exe      （随程序发出去的那份，最可信）
    //   2. 系统 PATH 里的 fio                （用户自己装的那份）
    // 都没有就返回空，界面上留空由用户点「浏览…」指定——
    // 不去猜 C:\Program Files\fio 之类的位置，更不扫盘。
    const QString bundled = bundledFioPathFor(QCoreApplication::applicationDirPath());
    if (!bundled.isEmpty())
        return bundled;

    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("fio"));
    if (!onPath.isEmpty())
        return QDir::cleanPath(onPath);

    return QString();
}

void MainWindow::setFioPath(const QString &path)
{
    m_fioPathEdit->setText(QDir::cleanPath(path));
}

void MainWindow::setSelfTestOutputDir(const QString &dir) { m_selfTestOutDir = dir; }
void MainWindow::setSelfTestTarget(const QString &root) { m_selfTestTarget = root; }
void MainWindow::setSelfTestFillLimit(int mib) { m_selfTestFillLimit = mib; }
void MainWindow::setSelfTestMode(const QString &modeKey) { m_selfTestModeKey = modeKey; }
void MainWindow::setSelfTestStopAfter(int seconds) { m_selfTestStopAfter = seconds; }
void MainWindow::setSelfTestScatter(bool scatter) { m_selfTestScatter = scatter; }
void MainWindow::setSelfTestReadOnly(bool readOnly) { m_selfTestReadOnly = readOnly; }
void MainWindow::setSelfTestLowDensity(bool low) { m_selfTestLowDensity = low; }
void MainWindow::setSelfTestRounds(int rounds) { m_selfTestRounds = rounds; }
void MainWindow::setSelfTestPerf(bool perf, int dataMiB)
{
    m_selfTestPerf = perf;
    m_selfTestPerfMiB = dataMiB;
}
void MainWindow::setSelfTestSaveData(const QString &path) { m_selfTestSaveData = path; }
void MainWindow::setSelfTestLoadData(const QString &path) { m_selfTestLoadData = path; }

void MainWindow::onPlotStyleChanged()
{
    if (!m_chart)
        return;
    const bool lineMode = m_lineRadio->isChecked();
    // 密度只对折线图有意义，选散点图时整组隐藏（值保留，切回来还在）
    if (m_densityBox)
        m_densityBox->setVisible(lineMode);
    m_chart->setPlotStyle(lineMode ? SpeedChart::PlotStyle::Line
                                   : SpeedChart::PlotStyle::Scatter);
}

void MainWindow::onDensityChanged()
{
    applyDensity();
}

void MainWindow::applyDensity()
{
    if (!m_chart)
        return;

    // 高密度 = 把全部采样点都画出来；低密度 = 按数据量自动抽稀。
    // 无论哪档，散点图和保存的 CSV 都是全量数据。
    const int step = m_densityLowRadio->isChecked()
                         ? SpeedChart::autoStepForBytes(m_axisByteTotal)
                         : 1;
    m_chart->setLineStep(step);
}

// ---------------------------------------------------------------- 目标分区

QString MainWindow::selectedRoot() const
{
    const QList<QTableWidgetItem *> selected = m_driveTable->selectedItems();
    if (selected.isEmpty())
        return QString();
    QTableWidgetItem *first = m_driveTable->item(selected.first()->row(), 0);
    return first ? first->data(Qt::UserRole).toString() : QString();
}

bool MainWindow::refreshDriveList(bool startup)
{
    if (!m_driveTable)
        return false;

    // 关键：**优先沿用当前已经选中的分区**。
    // 之前这里只看配置里存的值，于是每次刷新（测试结束、点刷新按钮）都会掉回第一个分区。
    // 只有当前选中的盘真的不在了（被拔掉/卸载），才回退到第一行。
    QString wantRoot = !m_selfTestTarget.isEmpty() ? m_selfTestTarget : selectedRoot();
    if (wantRoot.isEmpty())
        wantRoot = m_driveTable->property("preferredRoot").toString();

    const QList<VolumeEntry> volumes = enumerateVolumes();
    // 记下这次到底枚举到了哪些卷，写进自检报告 —— "找不到目标分区"时全靠它排查
    m_volumeSummary.clear();
    for (const VolumeEntry &volume : volumes) {
        m_volumeSummary << kVolumeSummaryLine
                               .arg(volume.rootPath, volume.deviceName)
                               .arg(volume.usedPercent(), 0, 'f', 1);
    }

    // 重建期间挡住 itemSelectionChanged：setRowCount(0) 会清空选中并立刻发信号，
    // 那一刻"没选中分区"会被 onDriveSelectionChanged 当成"分区里没有测试文件"，
    // 进而把「读取」的勾选取消掉。重建完成、选中恢复之后再解挡。
    m_refreshingDrives = true;
    m_driveTable->setRowCount(0);

    bool found = false;
    for (const VolumeEntry &volume : volumes) {
        const double percent = volume.usedPercent();
        const QString line = volume.displayLine();

        QStringList details;
        if (!volume.label.isEmpty())
            details << kVolumeLabelFmt.arg(volume.label);
        if (!volume.fileSystem.isEmpty())
            details << kVolumeFsFmt.arg(volume.fileSystem);
        if (!volume.typeText.isEmpty())
            details << kVolumeTypeFmt.arg(volume.typeText);
        const QString tooltip = details.isEmpty()
            ? line
            : line + QLatin1Char('\n') + details.join(QStringLiteral("    "));

        const int row = m_driveTable->rowCount();
        m_driveTable->insertRow(row);

        const QStringList cells = {
            volume.letter,
            volume.deviceName,
            QStringLiteral("%1 / %2").arg(formatBytes(volume.bytesUsed()),
                                          formatBytes(volume.bytesTotal)),
            QStringLiteral("%1%").arg(percent, 0, 'f', 1)
        };
        for (int column = 0; column < cells.size(); ++column) {
            auto *item = new QTableWidgetItem(cells.at(column));
            item->setToolTip(tooltip);
            if (column == 0) {
                item->setData(Qt::UserRole, volume.rootPath);
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            if (column >= 2)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (column == 3) {
                if (percent >= 95.0)
                    item->setForeground(QColor(200, 40, 40));
                else if (percent >= 85.0)
                    item->setForeground(QColor(200, 130, 20));
            }
            m_driveTable->setItem(row, column, item);
        }

        if (!wantRoot.isEmpty()
            && normalizedRoot(wantRoot) == normalizedRoot(volume.rootPath)) {
            m_driveTable->selectRow(row);
            found = true;
        }
    }

    if (!found && m_driveTable->rowCount() > 0) {
        if (!m_selfTestTarget.isEmpty()) {
            // 自检明确指定了目标分区却没找到：**绝不能悄悄退回第一行**。
            // 第一行很可能是系统盘，之前就这样误写过 C:。
            // 这里什么都不选，runSelfTest 会发现没选中而中止。
            statusBar()->showMessage(
                kSelfTestTargetMissing.arg(m_selfTestTarget));
        } else if (startup) {
            // **启动时不预选任何盘**：列表照常列出来，但一行都不选。
            // 第一行常常是系统盘，默认选中它意味着"打开程序就能直接点开始测"，
            // 很容易顺手把系统盘写了。选盘这件事必须由用户主动做一次。
            statusBar()->showMessage(kStatusSelectDriveHint);
        } else {
            // 运行期间的刷新（测试结束、点刷新、拔盘）仍然回退到第一行，
            // 保持原来的行为：这种情况下用户已经知道自己在操作哪块盘了。
            m_driveTable->selectRow(0);
            if (!wantRoot.isEmpty()) {
                statusBar()->showMessage(
                    kTargetRootChanged
                        .arg(wantRoot, m_driveTable->item(0, 0)->data(Qt::UserRole).toString()));
            }
        }
    }

    // 选中已经恢复，解挡；此时再让依赖"当前选中分区"的界面刷新一次
    m_refreshingDrives = false;
    if (!m_runner->isBusy()) {
        refreshTargetInfo();
        updateReadAvailability();
    }

    return m_driveTable->rowCount() > 0;
}

void MainWindow::refreshTargetInfo()
{
    const FioConfig cfg = configFromUi();
    if (cfg.targetRoot.isEmpty()) {
        m_targetLabel->setText(kTargetNotSelected);
        m_diskBar->setValue(0);
        m_diskBar->setStyleSheet(QString());
        // 三块都要清：条形本身不显示文字（setTextVisible(false)），
        // 空间和百分比是旁边两个标签，漏掉任何一个都会留着上一个盘的残留值。
        m_diskSpaceLabel->setText(kDiskSpaceUnselected);
        m_diskSpaceLabel->setStyleSheet(QStringLiteral("color:#666;"));
        m_diskBarLabel->clear();
        return;
    }

    quint64 total = 0;
    quint64 free = 0;
    if (!queryVolumeSpace(cfg.targetRoot, &total, &free)) {
        m_targetLabel->setText(kErrReadCapacity.arg(cfg.targetRoot));
        return;
    }

    refreshDiskBar(total, free);

    const FioConfig::FilePlan plan = cfg.planFiles(total, free);
    QString text = kTargetFolderFmt.arg(QDir::toNativeSeparators(cfg.testFolder()));
    if (cfg.testMode == FioConfig::TestMode::Performance) {
        // 性能测试固定单文件、大小是用户选的，"将写入 N 个文件"那句在这里没意义，不显示。
        const FioConfig::FilePlan perfPlan = FioConfig::planFilesForSize(cfg.perfDataBytes);
        if (perfPlan.isValid())
            text += kTargetPerfPlanFmt.arg(formatBytes(quint64(perfPlan.totalBytes)));
    } else if (plan.isValid()) {
        text += kTargetPlanFmt
                    .arg(plan.fileCount)
                    .arg(formatBytes(quint64(plan.fileSize)), formatBytes(quint64(plan.totalBytes)));
    } else {
        text += kTargetNoSpaceForPlan;
    }
    if (m_selfTestFillLimit > 0)
        text += kTargetSelfTestFillLimit.arg(m_selfTestFillLimit);
    m_targetLabel->setText(text);
}

void MainWindow::refreshDiskBar(quint64 total, quint64 free)
{
    if (total == 0) {
        m_diskBar->setValue(0);
        m_diskBar->setStyleSheet(QString());
        m_diskSpaceLabel->setText(kDiskSpaceUnselected);
        m_diskBarLabel->clear();
        return;
    }
    const quint64 used = total > free ? total - free : 0;
    const double percent = double(used) * 100.0 / double(total);

    // 按占用率换色，条形本身更显眼；读数放在条形右侧两列，右对齐、定宽
    QString chunkColor;
    QString textColor;
    if (percent >= 95.0) {
        chunkColor = QStringLiteral("#d9483c");
        textColor = QStringLiteral("#c82828");
    } else if (percent >= 85.0) {
        chunkColor = QStringLiteral("#e8933a");
        textColor = QStringLiteral("#b5750f");
    } else if (percent >= 70.0) {
        chunkColor = QStringLiteral("#e8c33a");
        textColor = QStringLiteral("#8a6d00");
    } else {
        chunkColor = QStringLiteral("#3fa34d");
        textColor = QStringLiteral("#2c7a37");
    }

    m_diskBar->setValue(int(percent * 10.0));
    m_diskBar->setStyleSheet(
        QStringLiteral("QProgressBar { border:1px solid #b0b0b0; border-radius:4px;"
                       " background:#ececec; }"
                       "QProgressBar::chunk { background:%1; border-radius:3px; }")
            .arg(chunkColor));

    m_diskSpaceLabel->setText(kDiskSpaceAvailable
                                  .arg(formatBytes(free), formatBytes(total)));
    m_diskSpaceLabel->setStyleSheet(QStringLiteral("color:#333;"));
    m_diskBarLabel->setText(kDiskBarUsedPercent.arg(percent, 0, 'f', 1));
    m_diskBarLabel->setStyleSheet(QStringLiteral("color:%1;").arg(textColor));
}

void MainWindow::updateDiskBarFor(const QString &root)
{
    if (root.isEmpty())
        return;
    quint64 total = 0;
    quint64 free = 0;
    if (queryVolumeSpace(root, &total, &free))
        refreshDiskBar(total, free);
}

void MainWindow::updateReadAvailability()
{
    const FioConfig cfg = configFromUi();
    const QString folder = cfg.testFolder();
    const bool hasFiles = !scanTestFiles(folder).isEmpty();
    const bool willWrite = m_writeCheck->isChecked();

    // 需求：磁盘里已经有文件才能单独执行读取；没有就得先写一遍。
    m_readCheck->setEnabled(hasFiles || willWrite);
    if (!hasFiles && !willWrite) {
        m_readCheck->setChecked(false);
        m_readCheck->setToolTip(kReadNoFilesTip
            .arg(QDir::toNativeSeparators(folder)));
    } else if (hasFiles) {
        m_readCheck->setToolTip(kReadAvailableTip
                                    .arg(QDir::toNativeSeparators(folder)));
    } else {
        m_readCheck->setToolTip(kReadNeedWriteTip);
    }

    m_blockSizeLabel->setText(
        FioConfig::modeFromKey(m_modeCombo->currentData().toString())
                == FioConfig::RwMode::SequentialWriteRead
            ? kBlockSizeSeq
            : kBlockSizeRand);
}

/// 目标分区里已经有测试文件时，问一句要不要覆盖。
/// 返回 true 表示可以继续（没有旧文件、已确认过、或用户点了「继续」）。
bool MainWindow::confirmOverwriteTestFiles()
{
    // 自检是无人值守的，不能弹对话框卡住自动化
    if (m_selfTestMode)
        return true;

    const FioConfig cfg = configFromUi();
    const QString folder = cfg.testFolder();
    if (folder.isEmpty())
        return true;

    const TestFileSet existing = scanTestFiles(folder);
    if (existing.isEmpty())
        return true;
    if (m_overwriteConfirmedFor == folder)
        return true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(kDlgOverwriteTitle);
    box.setText(kMsgOverwriteBody.arg(QDir::toNativeSeparators(folder))
                    .arg(existing.count)
                    .arg(formatBytes(quint64(existing.totalBytes))));
    QPushButton *go = box.addButton(kBtnContinue, QMessageBox::AcceptRole);
    QPushButton *cancel = box.addButton(kBtnCancel, QMessageBox::RejectRole);
    box.setDefaultButton(cancel);   // 默认落在「取消」上，别让回车顺手删了数据
    box.exec();

    if (box.clickedButton() != go)
        return false;

    m_overwriteConfirmedFor = folder;
    return true;
}

void MainWindow::onWriteCheckToggled(bool checked)
{
    if (!checked || m_suppressOverwritePrompt)
        return;
    if (confirmOverwriteTestFiles())
        return;

    // 用户选了「取消」→ 把写入的勾去掉
    m_suppressOverwritePrompt = true;
    m_writeCheck->setChecked(false);
    m_suppressOverwritePrompt = false;
    recordOutput(kLogOverwriteCancelled);
}

void MainWindow::onOptionsChanged()
{
    // 引擎和直接 I/O 都已经固定，这里只需要跟着刷新目标信息与读取可选性
    if (!m_runner->isBusy()) {
        refreshTargetInfo();
        updateReadAvailability();
    }
}

void MainWindow::buildPerfPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // ---- 测试数据大小 ----
    auto *optBox = new QGroupBox(kGroupTestParams, page);
    auto *optForm = new QFormLayout(optBox);
    optForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_perfSizeCombo = new QComboBox(optBox);
    for (qint64 bytes : FioConfig::perfSizeChoices())
        m_perfSizeCombo->addItem(FioConfig::perfSizeText(bytes), QVariant::fromValue(bytes));
    m_perfSizeCombo->setCurrentIndex(int(FioConfig::perfSizeChoices().indexOf(
        qint64(1024) * 1024 * 1024)));
    m_perfSizeCombo->setToolTip(kPerfSizeTip);
    // 换了数据大小，磁盘区那行"测试数据: 单个文件 …"要立刻跟着变，不用手动点刷新
    connect(m_perfSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshTargetInfo(); });
    optForm->addRow(kLabelPerfSize, m_perfSizeCombo);
    layout->addWidget(optBox);

    // ---- 测试项 + 结果表：勾选在左，数据在右（排版照 example.png）----
    auto *itemBox = new QGroupBox(kGroupPerfItems.arg(FioConfig::perfRuntimeSeconds()), page);
    auto *grid = new QGridLayout(itemBox);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    // 数据列占比大一些：项目名 1 : 读取 2 : 写入 2
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 2);
    grid->setColumnStretch(2, 2);

    auto *headRead = new QLabel(kPerfColRead, itemBox);
    auto *headWrite = new QLabel(kPerfColWrite, itemBox);
    for (QLabel *label : {headRead, headWrite}) {
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->setAlignment(Qt::AlignCenter);
    }
    grid->addWidget(headRead, 0, 1);
    grid->addWidget(headWrite, 0, 2);

    const QVector<FioConfig::PerfItem> items = FioConfig::allPerfItems();
    for (int i = 0; i < items.size(); ++i) {
        const FioConfig::PerfItem item = items.at(i);

        m_perfChecks[i] = new QCheckBox(FioConfig::perfItemName(item), itemBox);
        m_perfChecks[i]->setChecked(true);
        connect(m_perfChecks[i], &QCheckBox::toggled, this, &MainWindow::onOptionsChanged);
        grid->addWidget(m_perfChecks[i], i + 1, 0);

        // 顺序项用红/蓝，随机项用黄/绿 —— 和图表里的颜色对应
        m_perfReadCells[i] = new PerfCell(itemBox);
        m_perfWriteCells[i] = new PerfCell(itemBox);
        grid->addWidget(m_perfReadCells[i], i + 1, 1);
        grid->addWidget(m_perfWriteCells[i], i + 1, 2);
        // 点这一组的「读取」或「写入」都显示**同一组**数据：
        // 右边图表里同时画出这一项的读、写两条曲线。
        connect(m_perfReadCells[i], &PerfCell::activated, this,
                [this, i]() { showPerfItemTrace(i); });
        connect(m_perfWriteCells[i], &PerfCell::activated, this,
                [this, i]() { showPerfItemTrace(i); });
    }

    layout->addWidget(itemBox);
    layout->addStretch(1);
}

void MainWindow::onTestModeChanged()
{
    const bool perf = currentTestMode() == FioConfig::TestMode::Performance;
    m_paramStack->setCurrentIndex(perf ? 1 : 0);
    updateWindowTitle();
    updateReadAvailability();
    refreshTargetInfo();   // 换了模式，"测试数据 / 文件计划"那几行要立刻跟着变
    // 换了模式：图表里是"另一块模式"的数据就不该再显示（见 updateChartPlaceholder）
    updateChartPlaceholder();

    // 这里**不清空结果**：测试数据保留到"重新开始测试"或关闭程序为止，
    // 切模式、切盘都不该把已经测出来的数字弄丢。
    for (int i = 0; i < 4; ++i) {
        if (m_perfReadCells[i])
            m_perfReadCells[i]->setClickable(!m_runner->isBusy()
                                             && !m_perfReadTrace[i].isEmpty());
        if (m_perfWriteCells[i])
            m_perfWriteCells[i]->setClickable(!m_runner->isBusy()
                                              && !m_perfWriteTrace[i].isEmpty());
    }
}

FioConfig::TestMode MainWindow::currentTestMode() const
{
    if (m_modePerfRadio && m_modePerfRadio->isChecked())
        return FioConfig::TestMode::Performance;
    return FioConfig::TestMode::Reliability;
}

void MainWindow::updateWindowTitle()
{
    setWindowTitle(kWindowTitleFmt.arg(kAppName, kAppVersion, currentModeName()));
}

QString MainWindow::currentModeName() const
{
    return currentTestMode() == FioConfig::TestMode::Performance ? kModeNamePerformance
                                                                 : kModeNameReliability;
}

void MainWindow::switchPerfResultsTo(const QString &root)
{
    // 先把当前盘的结果存回去（正在跑测试时不保存，那时显示的就是本盘数据）
    if (!m_perfResultsRoot.isEmpty() && !m_runner->isBusy()) {
        PerfResult &stored = m_perfResults[m_perfResultsRoot];
        for (int i = 0; i < 4; ++i) {
            stored.readValue[i] = m_perfReadCells[i] ? m_perfReadCells[i]->value() : -1.0;
            stored.writeValue[i] = m_perfWriteCells[i] ? m_perfWriteCells[i]->value() : -1.0;
            stored.readTrace[i] = m_perfReadTrace[i];
            stored.writeTrace[i] = m_perfWriteTrace[i];
        }
    }

    m_perfResultsRoot = root;
    const PerfResult stored = m_perfResults.value(root);

    for (int i = 0; i < 4; ++i) {
        m_perfReadTrace[i] = stored.readTrace[i];
        m_perfWriteTrace[i] = stored.writeTrace[i];
        if (m_perfReadCells[i])
            m_perfReadCells[i]->setValue(stored.readValue[i]);
        if (m_perfWriteCells[i])
            m_perfWriteCells[i]->setValue(stored.writeValue[i]);
        if (m_perfReadCells[i])
            m_perfReadCells[i]->setClickable(!m_runner->isBusy()
                                             && !m_perfReadTrace[i].isEmpty());
        if (m_perfWriteCells[i])
            m_perfWriteCells[i]->setClickable(!m_runner->isBusy()
                                              && !m_perfWriteTrace[i].isEmpty());
    }
    rescalePerfCells();

    // 图表归属：只有性能模式才"跟着盘走"（结果本来就是按盘存的）。
    // 可靠性模式的归属由"在哪块盘上跑完的"决定，不在这里改。
    if (currentTestMode() == FioConfig::TestMode::Performance) {
        m_chartOwnerRoot = root;
        m_chartOwnerMode = FioConfig::TestMode::Performance;

        int firstWithData = -1;
        for (int i = 0; i < 4 && firstWithData < 0; ++i) {
            if (!m_perfReadTrace[i].isEmpty() || !m_perfWriteTrace[i].isEmpty())
                firstWithData = i;
        }
        if (firstWithData >= 0)
            showPerfItemTrace(firstWithData);   // 里面会把图表页切出来
        else
            m_chart->clear();
    }
    updateChartPlaceholder();
}

void MainWindow::updateChartPlaceholder()
{
    // 只有一条规则：**当前模式 + 当前盘有没有数据**。
    // 从文件打开的历史数据走独立窗口，和这里无关（所以这里不再有"加载来的数据"分支）。
    const QString root = selectedRoot();
    const bool perf = currentTestMode() == FioConfig::TestMode::Performance;
    bool hasData = false;

    if (perf) {
        const PerfResult stored = m_perfResults.value(root);
        for (int i = 0; i < 4 && !hasData; ++i) {
            hasData = !stored.readTrace[i].isEmpty() || !stored.writeTrace[i].isEmpty();
        }
    } else {
        // 可靠性：图表里的数据必须是"在这块盘上跑出来的"
        hasData = !m_readSamples.isEmpty()
                  && m_chartOwnerMode == FioConfig::TestMode::Reliability
                  && !m_chartOwnerRoot.isEmpty()
                  && normalizedRoot(m_chartOwnerRoot) == normalizedRoot(root);
    }

    m_chartStack->setCurrentIndex(hasData ? 1 : 0);
}

void MainWindow::onDriveSelectionChanged()
{
    // 重建列表过程中的中途信号要忽略（见 refreshDriveList 里的说明）
    if (m_refreshingDrives)
        return;

    // 换了目标盘，之前确认过的"覆盖"就不再适用了
    m_overwriteConfirmedFor.clear();
    // 结果按盘分开：切过去先把这个盘已经测出来的数据显示出来（没测过就是空的）
    switchPerfResultsTo(selectedRoot());
    if (m_runner->isBusy())
        return;
    refreshTargetInfo();
    updateReadAvailability();
}

// ---------------------------------------------------------------- 交互

void MainWindow::onBrowseFio()
{
    const QString path = QFileDialog::getOpenFileName(
        this, kDlgChooseFioTitle, m_fioPathEdit->text(),
        kFilterExecutable);
    if (!path.isEmpty())
        m_fioPathEdit->setText(QDir::cleanPath(path));
}

void MainWindow::onOpenTestDirectory()
{
    const FioConfig cfg = configFromUi();
    if (cfg.targetRoot.isEmpty()) {
        reportProblem(kErrNoDriveSelected,
                      kErrNoDriveSelectedDetail);
        return;
    }
    const QString folder = cfg.testFolder();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::onCleanupTestFiles()
{
    if (m_runner->isBusy()) {
        reportProblem(kErrBusy, kErrStopBeforeCleanup);
        return;
    }
    const FioConfig cfg = configFromUi();
    if (cfg.targetRoot.isEmpty())
        return;

    const QString folder = cfg.testFolder();
    const TestFileSet set = scanTestFiles(folder);
    if (set.isEmpty()) {
        reportProblem(kErrNoTestFiles,
                      kErrNoTestFilesInFolder
                          .arg(QDir::toNativeSeparators(folder)));
        return;
    }

    if (!m_selfTestMode) {
        const auto answer = QMessageBox::question(
            this, kDlgCleanupTitle,
            kMsgConfirmCleanup
                .arg(QDir::toNativeSeparators(folder))
                .arg(set.count)
                .arg(formatBytes(quint64(set.totalBytes))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    QString error;
    if (!clearTestFiles(folder, &error)) {
        reportProblem(kErrCleanupFailed, error, true);
        return;
    }
    recordOutput(kStatusCleanedFiles.arg(set.count));
    statusBar()->showMessage(kStatusCleanedFiles.arg(set.count));
    refreshDriveList();
    refreshTargetInfo();
    updateReadAvailability();
}

void MainWindow::onSaveRunData()
{
    // 两种模式的数据来源不同（和 saveRunDataTo 里的判断保持一致）：
    // 可靠性的采样在 m_readSamples，性能的整轮数据在 m_perfRunTimes。
    const bool perf = (m_activeConfig.testMode == FioConfig::TestMode::Performance);
    if ((perf ? m_perfRunTimes.size() : m_readSamples.size()) <= 0) {
        reportProblem(kErrNoDataToSave,
                      kErrNoSamplesYet);
        return;
    }

    // 默认存到**程序自己所在的目录**，不是被测盘。
    //
    // 以前默认落在 <目标分区>\ml_disktest\ 里，那几个问题：
    //   1. 结果文件写在被测盘上，占了那块盘的空间——大盘一测就占满，
    //      想把结果存下来反而先失败（no space left）；
    //   2. 它躺在 fio 的测试目录里，和 testfile_* 混在一起，
    //      下次"清理测试文件"/重跑会把结果一起当垃圾删掉；
    //   3. 盘测完要拔下来时，结果还留在上面，容易连盘一起交出去。
    // 程序目录是用户开着程序时一定还在的地方，也符合"结果跟着程序走"的直觉。
    //
    // 文件名带上模式和时间戳（ml_disktest-可靠性测试-20261012-183000.csv）：
    // 一次会话可能两种模式都跑过，都叫 ml_disktest-data.csv 会互相覆盖，
    // 隔几天再看也分不清是哪次的结果。
    const QString kind = perf ? kModeNamePerformance : kModeNameReliability;
    const QString stamp = QDateTime::currentDateTime().toString(kRunDataTimeStampFmt);
    const QString suggested =
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(kRunDataFileNameFmt.arg(kind, stamp));
    const QString path = QFileDialog::getSaveFileName(
        this, kDlgSaveDataTitle, suggested,
        kFilterCsv);
    if (path.isEmpty())
        return;

    QString error;
    if (!saveRunDataTo(path, &error)) {
        reportProblem(kTextSaveFailed, error, true);
        return;
    }
    // 状态栏里的采样点数按模式取，性能模式的数据不在 m_readSamples 里
    const int saved = perf ? m_perfRunTimes.size() : m_readSamples.size();
    statusBar()->showMessage(kStatusRunDataSaved.arg(saved).arg(path), 15000);
    recordOutput(kReportDataSaved.arg(path));
}

bool MainWindow::saveRunDataTo(const QString &path, QString *error)
{
    // 两种模式的数据来源不同：
    //   可靠性 —— 采样数组就是本次运行的曲线（关盘/切模式都不动它）
    //   性能   —— 图表每换一项就 clear()，所以另外攒了一份 m_perfRun*
    const bool perf = (m_activeConfig.testMode == FioConfig::TestMode::Performance);
    const int sampleCount = perf ? m_perfRunTimes.size() : m_readSamples.size();
    if (sampleCount <= 0) {
        if (error)
            *error = kErrNoSamplesYet;
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QTextStream out(&file);
    const auto param = [&out](const QString &key, const QString &value) {
        out << key << ',' << value << '\n';
    };
    const auto paramDouble = [&param](const QString &key, double value, int digits) {
        param(key, QString::number(value, 'f', digits));
    };

    out << "# " << kRunDataMagic << "\n";
    out << "# " << kRunMarker << "\n";
    // 写这份结果的程序版本。打开时按它判断支不支持：
    // 低于 RunData::minimumReadableVersion() 的一律拒收并提示"版本过旧"。
    // 版本号只有 CMakeLists.txt 的 project(... VERSION) 一个来源。
    param(QStringLiteral("app_version"), kAppVersion);
    // 先写模式：读的时候靠它决定用容量轴还是时间轴渲染。
    // 老文件没有这一行，读取侧会退回"按字段推断"，并且默认按可靠性。
    param(RunData::testModeKey(),
          RunData::modeKey(m_activeConfig.testMode == FioConfig::TestMode::Performance
                               ? RunData::Mode::Performance
                               : RunData::Mode::Reliability));
    param(QStringLiteral("mode"), m_activeConfig.modeText());
    param(QStringLiteral("mode_key"), FioConfig::modeKey(m_activeConfig.mode));
    param(QStringLiteral("target"), m_activeConfig.targetRoot);
    param(QStringLiteral("folder"), QDir::toNativeSeparators(m_activeConfig.testFolder()));
    param(QStringLiteral("block_size"), m_activeConfig.blockSize());
    param(QStringLiteral("ioengine"), FioConfig::ioEngine());
    param(QStringLiteral("iodepth"), QString::number(m_activeConfig.iodepth));
    param(QStringLiteral("rounds"), QString::number(m_activeConfig.rounds));
    param(QStringLiteral("direct"), FioConfig::directIo() ? QStringLiteral("1") : QStringLiteral("0"));
    param(QStringLiteral("verify"), m_activeConfig.verify ? QStringLiteral("1") : QStringLiteral("0"));
    param(QStringLiteral("do_write"), m_activeConfig.doWrite ? QStringLiteral("1") : QStringLiteral("0"));
    param(QStringLiteral("do_read"), m_activeConfig.doRead ? QStringLiteral("1") : QStringLiteral("0"));
    if (perf) {
        // 性能数据的横轴是时间，上限固定 10 秒，读的时候按模式重建，不靠这里
        paramDouble(QStringLiteral("perf_data_bytes"),
                    double(m_activeConfig.perfDataBytes), 0);
        param(QStringLiteral("perf_runtime_sec"),
              QString::number(FioConfig::perfRuntimeSeconds()));
        QStringList itemKeys;
        for (FioConfig::PerfItem item : FioConfig::allPerfItems())
            itemKeys << FioConfig::perfItemKey(item);
        param(QStringLiteral("perf_items"), itemKeys.join(QLatin1Char(' ')));
    } else {
        param(QStringLiteral("file_count"), QString::number(m_activePlan.fileCount));
        param(QStringLiteral("file_size_bytes"), QString::number(m_activePlan.fileSize));
        param(QStringLiteral("planned_bytes"), QString::number(m_activePlan.totalBytes));
    }
    // 逐文件大小：坏块位置要靠它换算成图表横坐标（见 verifyPositionToAxisX）。
    // 只读校验时盘上现成的文件不一定等大，所以记的是**列表**而不是一个统一值。
    // 写在两个分支之外：只读路径没有 m_activePlan，但同样需要这一份。
    {
        QStringList sizes;
        if (!m_verifyFileSizes.isEmpty()) {
            for (qint64 size : m_verifyFileSizes)
                sizes << QString::number(size);
        } else if (m_activePlan.isValid()) {
            for (int i = 0; i < m_activePlan.fileCount; ++i)
                sizes << QString::number(m_activePlan.fileSize);
        }
        if (!sizes.isEmpty())
            param(QStringLiteral("file_sizes"), sizes.join(QLatin1Char(' ')));
    }
    paramDouble(QStringLiteral("axis_max_GiB"), m_chart->axisMax(), 6);
    param(QStringLiteral("plot_style"),
          m_chart->plotStyle() == SpeedChart::PlotStyle::Scatter ? QStringLiteral("scatter")
                                                                 : QStringLiteral("line"));
    param(QStringLiteral("line_step"), QString::number(m_chart->lineStep()));
    param(QStringLiteral("density"),
          m_densityLowRadio->isChecked() ? QStringLiteral("low") : QStringLiteral("high"));
    for (const PhaseOutcome &outcome : m_outcomes) {
        const QString p = outcome.kind == JobKind::Write ? QStringLiteral("write")
                                                         : QStringLiteral("read");
        param(p + QStringLiteral("_exit_code"), QString::number(outcome.result.exitCode));
        param(p + QStringLiteral("_elapsed_sec"), QString::number(outcome.result.elapsedSeconds, 'f', 3));
        param(p + QStringLiteral("_avg_read_MiBps"), QString::number(outcome.stats.readAvg, 'f', 3));
        param(p + QStringLiteral("_avg_write_MiBps"), QString::number(outcome.stats.writeAvg, 'f', 3));
        param(p + QStringLiteral("_sample_count"), QString::number(outcome.stats.count));
        param(p + QStringLiteral("_warnings"), QString::number(outcome.result.warnings.size()));
    }

    // ---- 坏块信息 ----
    // 这是"这块盘到底坏没坏"的结论，必须进文件：
    // 一份只有曲线和平均值的结果文件，打开时根本看不出当时校验到底过没过。
    // 逐处明细可能很多（实测坏盘一次 900+ 处），所以只用 | 拼一列；
    // 每处的偏移和原话是两条并列列表，顺序一一对应。
    {
        QStringList texts;
        QStringList offsets;
        QStringList fileIndexes;
        int totalFailures = 0;
        for (const PhaseOutcome &outcome : m_outcomes) {
            totalFailures += outcome.result.verifyFailures;
            for (const QString &line : outcome.result.verifyErrorLines)
                texts << line;
            for (qint64 offset : outcome.result.verifyOffsets)
                offsets << QString::number(offset);
            for (int index : outcome.result.verifyFileIndexes)
                fileIndexes << QString::number(index);
        }
        // 曲线里带的也算一份保底：万一 m_outcomes 被清过（比如中途加载过数据），
        // 界面上显示的处数不该比文件里记的还多。
        totalFailures = qMax(totalFailures, m_verifyFailures);
        param(QStringLiteral("verify_failures"), QString::number(totalFailures));
        if (!texts.isEmpty())
            param(QStringLiteral("verify_text"), texts.join(QLatin1Char('|')));
        if (!offsets.isEmpty()) {
            param(QStringLiteral("verify_offsets"), offsets.join(QLatin1Char('|')));
            // 文件序号必须跟着写：偏移是"文件内"的，每个文件都从 0 开始，
            // 少了这一列，读回来时所有坏块都会被算到第一个文件那一段去。
            param(QStringLiteral("verify_file_index"), fileIndexes.join(QLatin1Char('|')));
        }
    }
    if (perf) {
        // 每一项的整趟结果，读回来就能直接填结果表
        for (int i = 0; i < 4; ++i) {
            const QString prefix = FioConfig::perfItemKey(FioConfig::allPerfItems().at(i));
            param(prefix + QStringLiteral("_read_MiBps"),
                  QString::number(m_perfValues.read[i], 'f', 3));
            param(prefix + QStringLiteral("_write_MiBps"),
                  QString::number(m_perfValues.write[i], 'f', 3));
        }
    }

    out << "# " << kSamplesMarker << "\n";
    // 采样段**统一 9 列**，末尾两列是 item/pass。
    // 性能数据靠这两列还原"这一拍属于哪一项、哪一趟"；
    // 可靠性数据没有分项概念，一律写 0,0 —— 但**列必须写满**：
    // 读取端从 2.2.7 起只认这一种列宽（老格式已经被版本门槛挡掉），
    // 少两列的话整段采样会被当成坏行全部跳过，读回来就是"0 个采样点"。
    out << "read_GiB,read_MiBps,has_read,write_GiB,write_MiBps,has_write,duration_sec,item,pass\n";

    for (int i = 0; i < sampleCount; ++i) {
        if (perf) {
            const quint8 flags = m_perfRunFlags.value(i, 0);
            out << QString::number(m_perfRunTimes.value(i), 'f', 6) << ','
                << QString::number(m_perfRunRead.value(i), 'f', 3) << ','
                << ((flags & 1) ? '1' : '0') << ','
                << QString::number(m_perfRunTimes.value(i), 'f', 6) << ','
                << QString::number(m_perfRunWrite.value(i), 'f', 3) << ','
                << ((flags & 2) ? '1' : '0') << ','
                << QString::number(m_perfRunDurations.value(i, 0.5), 'f', 3) << ','
                << m_perfRunItems.value(i, -1) << ','
                << m_perfRunPasses.value(i, -1) << '\n';
            continue;
        }
        const quint8 flags = m_sampleFlags.value(i, 0);
        out << QString::number(m_sampleTimes.value(i), 'f', 6) << ','
            << QString::number(m_readSamples.at(i), 'f', 3) << ','
            << ((flags & 1) ? '1' : '0') << ','
            << QString::number(m_writeTimes.value(i), 'f', 6) << ','
            << QString::number(m_writeSamples.at(i), 'f', 3) << ','
            << ((flags & 2) ? '1' : '0') << ','
            << QString::number(m_sampleDurations.value(i, 0.5), 'f', 3) << ','
            << 0 << ',' << 0 << '\n';   // 可靠性没有分项：固定 0,0
    }
    file.close();
    return true;
}

void MainWindow::onLoadRunData()
{
    const QString path = QFileDialog::getOpenFileName(
        this, kDlgLoadDataTitle,
        // 默认打开程序自己所在的目录：测试数据一般就存在程序旁边
        QCoreApplication::applicationDirPath(),
        kFilterCsv);
    if (path.isEmpty())
        return;

    // 历史数据一律**在独立窗口里**看，完全不碰主界面：
    // 那份数据可能来自一块当前机器上根本不存在的盘，灌进主界面的话
    // 卡片、磁盘进度条、目标分区显示的都会是"当前选中的盘"，和数据来源对不上。
    // 解析失败时 ResultWindow::open 自己会弹提示。
    ResultWindow::open(this, path);
}

/// 把一份解析好的结果画进图表。
///
/// **只画图，不碰任何界面状态**：卡片、"图表属于哪块盘"、"图表栈"都不在这里改。
/// 所以同一个函数既能喂给独立结果窗口，也能喂给自检用的主图表——
/// 唯一的前提是两份数据渲染规则本来就应该完全一样。
/// 否则会出现"窗口里按性能画、主界面按可靠性画"这种不一致。
static void fillChartFromRunData(const RunData &data, SpeedChart *chart)
{
    const bool perf = (data.mode == RunData::Mode::Performance);
    const bool plotRead = std::any_of(data.flags.cbegin(), data.flags.cend(),
                                      [](quint8 f) { return (f & 1) != 0; });
    const bool plotWrite = std::any_of(data.flags.cbegin(), data.flags.cend(),
                                       [](quint8 f) { return (f & 2) != 0; });
    // 折线抽稀步长：文件里记了就照它画，没有就按数据量自动选
    const int recordedStep = int(data.paramDouble(QStringLiteral("line_step"), 0.0));

    chart->clear();
    chart->setPlotStyle(data.param(QStringLiteral("plot_style")) == QLatin1String("scatter")
                            ? SpeedChart::PlotStyle::Scatter
                            : SpeedChart::PlotStyle::Line);
    if (perf) {
        // 性能数据的"容量列"里放的是秒，轴要按时间画
        const double seconds = data.paramDouble(QStringLiteral("axis_max_GiB"),
                                                double(FioConfig::perfRuntimeSeconds()));
        chart->setTimeAxis(seconds > 0.0 ? seconds : double(FioConfig::perfRuntimeSeconds()));
    } else {
        chart->setCapacityAxis(data.axisMaxGiB());
    }
    chart->setDirections(plotRead, plotWrite);
    if (recordedStep > 0)
        chart->setLineStep(recordedStep);

    for (int i = 0; i < data.sampleCount(); ++i) {
        const quint8 f = data.flags.value(i, 0);
        chart->appendSample(data.readX.value(i, 0.0), (f & 1) != 0,
                            data.readV.value(i, 0.0),
                            data.writeX.value(i, 0.0), (f & 2) != 0,
                            data.writeV.value(i, 0.0));
    }
    // 点加完了，重建折线让**最后一个点一定画出来**。
    // 抽稀是"每隔 step 个点保留一个"，末点落在哪一格是偶然的，
    // 不是 step 的整数倍时就被抽掉，曲线末端够不到横轴右边缘。
    chart->finishSamples();

    if (perf) {
        // 性能 CSV 是多场次叠在一份文件里的，颜色按"第一项有数据的"来定，
        // 只影响这一张合起来的时间图；分项回看请用「打开测试结果」窗口。
        const QVector<RunData::Trace> itemTraces = data.perfTraces();
        for (int i = 0; i < itemTraces.size(); ++i) {
            if (!itemTraces.at(i).hasRead() && !itemTraces.at(i).hasWrite())
                continue;
            const FioConfig::PerfItem item = FioConfig::allPerfItems().value(i);
            if (FioConfig::perfItemIsRandom(item))
                chart->setSeriesColors(QColor(0xf0, 0xc0, 0x00), QColor(0x2e, 0xaa, 0x4a));
            else
                chart->setSeriesColors(QColor(220, 30, 30), QColor(30, 70, 220));
            break;
        }
    }
}

bool MainWindow::loadRunDataFrom(const QString &path, QString *error, SpeedChart *target)
{
    // 正在跑测试时不动图表：自检那条路也一样，否则会把实时曲线覆盖掉
    if (m_runner->isBusy()) {
        if (error)
            *error = kErrBusyLoading;
        return false;
    }

    // 变量名不能叫 data：QWidget 有一个同名成员，/W4 下会报 C4458 名字隐藏。
    RunData runData;
    if (!RunData::parse(path, &runData, error))
        return false;

    SpeedChart *chart = target ? target : m_chart;
    fillChartFromRunData(runData, chart);
    if (target)
        return true;   // 只画图：主窗口的数组、卡片、图表归属一律不动

    // ---- 下面开始才是"灌进主窗口"（只给自检的 --selftest-load-data 用）----
    m_readSamples = runData.readV;
    m_writeSamples = runData.writeV;
    m_sampleTimes = runData.readX;
    m_writeTimes = runData.writeX;
    m_sampleDurations = runData.durations;
    m_sampleFlags = runData.flags;

    m_plotRead = std::any_of(runData.flags.cbegin(), runData.flags.cend(),
                             [](quint8 f) { return (f & 1) != 0; });
    m_plotWrite = std::any_of(runData.flags.cbegin(), runData.flags.cend(),
                              [](quint8 f) { return (f & 2) != 0; });
    m_outcomes.clear();
    m_phase = Phase::Idle;

    m_axisByteTotal = qint64(runData.axisMaxGiB() * 1024.0 * 1024.0 * 1024.0);
    m_phaseStartIndex = 0;

    if (runData.param(QStringLiteral("plot_style")) == QLatin1String("scatter"))
        m_scatterRadio->setChecked(true);
    else
        m_lineRadio->setChecked(true);
    if (runData.param(QStringLiteral("density")) == QLatin1String("low"))
        m_densityLowRadio->setChecked(true);
    else
        m_densityHighRadio->setChecked(true);
    onPlotStyleChanged();   // 载入的绘图方式可能不是折线图，同步一下密度可见性
    applyDensity();

    // 卡片：和测试结束一样，"当前速度"留空——历史数据没有"此刻的速度"。
    // 平均值取文件里记录的"分阶段"平均值，不能拿全部采样点重算：
    // 写入阶段和读取阶段的速度能差两三倍，混在一起平均出来的数没有意义，
    // 也会和保存时卡片上显示的数字对不上。
    m_readValueLabel->setText(QStringLiteral("--"));
    m_writeValueLabel->setText(QStringLiteral("--"));
    if (m_plotRead)
        m_avgReadLabel->setText(formatSpeed(runData.averageReadMiBps()));
    if (m_plotWrite)
        m_avgWriteLabel->setText(formatSpeed(runData.averageWriteMiBps()));
    m_elapsedLabel->setText(formatDuration(qint64(runData.elapsedSeconds())));
    updateStateLabel(); // 已停止

    QStringList info;
    info << kStatusSamplesLoaded.arg(runData.sampleCount());
    if (runData.hasParam(QStringLiteral("mode")))
        info << runData.param(QStringLiteral("mode"));
    if (runData.hasParam(QStringLiteral("target")))
        info << runData.param(QStringLiteral("target"));
    const QString message = info.join(kTextJoinDot);
    statusBar()->showMessage(message);
    recordOutput(kReportLoadData.arg(path));
    recordOutput(message);
    if (runData.hasParam(QStringLiteral("mode"))) {
        recordOutput(kReportLoadedConfig
                         .arg(runData.param(QStringLiteral("mode")),
                              runData.param(QStringLiteral("block_size")),
                              runData.param(QStringLiteral("iodepth")),
                              runData.param(QStringLiteral("verify"))));
    }
    // 这份文件是性能还是可靠性数据，写进日志：报告里那一行"测试配置"来自
    // 当前界面（自检时并不是这份数据的配置），有个明确的模式标记才好核对。
    recordOutput(kReportLoadedMode.arg(
        runData.mode == RunData::Mode::Performance ? kModeNamePerformance
                                                   : kModeNameReliability));

    // 加载完直接显示图表：原来忘了切页，明明加载了数据，图表位置还是"请先开始测试"。
    m_chartStack->setCurrentIndex(1);
    return true;
}

void MainWindow::onSaveDiagnostics()
{
    if (m_logLines.isEmpty()) {
        reportProblem(kErrNoContentToSave,
                      kErrNoOutputYet);
        return;
    }
    const QString suggested = QDir(QDir::tempPath())
                                  .absoluteFilePath(QStringLiteral("ml_disktest-fio-output.log"));
    const QString path = QFileDialog::getSaveFileName(
        this, kDlgSaveDiagnosticsTitle, suggested,
        kFilterTextLog);
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        reportProblem(kTextSaveFailed, file.errorString(), true);
        return;
    }
    QTextStream out(&file);
    out << m_logLines.join(QLatin1Char('\n')) << '\n';
    file.close();
    statusBar()->showMessage(kStatusDiagnosticsSaved.arg(path), 10000);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, kDlgAboutTitle,
        kAboutHtml
            .arg(kAppName, kAppVersion, kAuthor, kContact, kBuildTime,
                 QString::fromLatin1(qVersion()),
                 isProcessElevated() ? kModeElevated
                                     : kModeNormal,
                 FioConfig::folderName(), FioConfig::fileBaseName()));
}

void MainWindow::onClearChart()
{
    m_chart->clear();
    clearAllStats();
}

void MainWindow::resetPhaseStats()
{
    // 注意：**不清空采样数组**。数组保存的是整轮测试的数据（保存 CSV 要用），
    // 图表也一直留着全过程历史。这里只重置"当前值"和本阶段的字节累加，
    // 各阶段的统计靠 m_phaseStartIndex 切出来。
    m_readBytesInPhase = 0.0;
    m_writeBytesInPhase = 0.0;
    m_phaseStartIndex = m_readSamples.size();
    m_phaseSampleCount = 0;
    m_phaseNeedOriginAnchor = false;
    m_tailDropped = false;
    m_perfPassBase = -1.0;   // 本趟第一个**有效**采样点的时刻，用来把曲线拉到 0 秒
    m_perfPassSamples = 0;
    m_readValueLabel->setText(QStringLiteral("--"));
    m_writeValueLabel->setText(QStringLiteral("--"));
    // 平均值与已用时**故意不清**：它们现在显示的是上一阶段的结果，
    // 进了读取阶段还把"平均写入"抹掉，用户就看不到写入成绩了。
}

void MainWindow::clearAllStats()
{
    m_readSamples.clear();
    m_writeSamples.clear();
    m_sampleTimes.clear();
    m_writeTimes.clear();
    m_sampleDurations.clear();
    m_sampleFlags.clear();
    m_phaseStartIndex = 0;
    m_phaseSampleCount = 0;
    m_phaseNeedOriginAnchor = false;
    m_skippedFirstSamples = 0;
    m_readBytesInPhase = 0.0;
    m_writeBytesInPhase = 0.0;
    m_readValueLabel->setText(QStringLiteral("--"));
    m_writeValueLabel->setText(QStringLiteral("--"));
    m_avgReadLabel->setText(QStringLiteral("--"));
    m_avgWriteLabel->setText(QStringLiteral("--"));
    m_elapsedLabel->setText(QStringLiteral("--"));
}

void MainWindow::resetPerfRunSamples()
{
    // 只在"重新开始一次性能测试"时清空：测试过程中每换一项都会
    // clearAllStats()（那会清 m_readSamples），但这一份必须留着，
    // 否则跑完四项只剩最后一项，保存出来的 CSV 就缺了三项的数据。
    m_perfRunTimes.clear();
    m_perfRunRead.clear();
    m_perfRunWrite.clear();
    m_perfRunDurations.clear();
    m_perfRunFlags.clear();
    m_perfRunItems.clear();
    m_perfRunPasses.clear();
    m_perfValues = PerfValues();
}

MainWindow::RunStats MainWindow::computeRunStats(bool dropLastSample) const
{
    RunStats stats;
    // 只统计**当前阶段**：从 m_phaseStartIndex 到数组末尾
    int end = m_readSamples.size();
    if (dropLastSample && end > m_phaseStartIndex)
        --end;
    const int count = end - m_phaseStartIndex;
    if (count <= 0)
        return stats;

    stats.count = count;
    stats.readMin = stats.readMax = m_readSamples.at(m_phaseStartIndex);
    stats.writeMin = stats.writeMax = m_writeSamples.at(m_phaseStartIndex);

    // 平均值按区间长度加权：各拍长度并不相等。
    double readWeighted = 0.0;
    double writeWeighted = 0.0;
    double totalDuration = 0.0;
    for (int i = m_phaseStartIndex; i < end; ++i) {
        const double read = m_readSamples.at(i);
        const double write = m_writeSamples.at(i);
        const double duration = m_sampleDurations.value(i, 0.5);
        stats.readMin = qMin(stats.readMin, read);
        stats.readMax = qMax(stats.readMax, read);
        stats.writeMin = qMin(stats.writeMin, write);
        stats.writeMax = qMax(stats.writeMax, write);
        readWeighted += read * duration;
        writeWeighted += write * duration;
        totalDuration += duration;
    }

    if (totalDuration > 0.0) {
        stats.readAvg = readWeighted / totalDuration;
        stats.writeAvg = writeWeighted / totalDuration;
    } else {
        stats.readAvg = readWeighted / count;
        stats.writeAvg = writeWeighted / count;
    }
    return stats;
}

void MainWindow::setRunningState(bool running)
{
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);

    // 测试期间把**所有**参数都锁住：两个参数页（含 读取、校验、四个测试项、数据大小）、
    // 模式单选、驱动器列表、fio 路径。之前漏了 读取 和四个测试项，测试中还能改。
    const QList<QWidget *> params = {
        m_fioPathEdit, m_driveTable, m_paramStack,
        m_modeReliabilityRadio, m_modePerfRadio
    };
    for (QWidget *widget : params) {
        if (widget)
            widget->setEnabled(!running);
    }

    // 测试进行中不许点结果格子看明细（数据还没齐）
    for (int i = 0; i < 4; ++i) {
        if (m_perfReadCells[i])
            m_perfReadCells[i]->setClickable(!running && !m_perfReadTrace[i].isEmpty());
        if (m_perfWriteCells[i])
            m_perfWriteCells[i]->setClickable(!running && !m_perfWriteTrace[i].isEmpty());
    }

    if (running)
        m_readCheck->setEnabled(false);
    else
        updateReadAvailability();
}

// ---------------------------------------------------------------- 文件准备

bool MainWindow::clearTestFiles(const QString &folder, QString *error)
{
    QDir dir(folder);
    if (!dir.exists())
        return true;

    // 连同 fio 留下的校验状态文件一起删掉
    const QFileInfoList entries = dir.entryInfoList(
        QStringList{QStringLiteral("testfile_*"), QStringLiteral("*.state")},
        QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo &info : entries) {
        if (!QFile::remove(info.absoluteFilePath())) {
            if (error)
                *error = kErrDeleteOldFiles.arg(info.absoluteFilePath());
            return false;
        }
    }
    return true;
}

bool MainWindow::ensureTestFolder(const QString &folder, QString *error)
{
    // 只建目录，不预分配文件：文件交给 fio 的 create_on_open 在打开时创建，
    // 直接写一遍。自己预分配在大盘/U 盘上非常慢（实测 G: 上 2 GB 要 8.9 秒）。
    QDir dir(folder);
    if (dir.exists())
        return true;
    if (!QDir().mkpath(folder)) {
        if (error)
            *error = kErrCreateFolder.arg(folder);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- 测试流程

void MainWindow::abortStart(const QString &title, const QString &text, bool critical)
{
    reportProblem(title, text, critical);
    setRunningState(false);
    m_phase = Phase::Idle;
    updateStateLabel();
    // 自检模式下必须走完报告流程并退出，不然进程会一直挂着等一个永远不会来的阶段回调
    if (m_selfTestMode)
        finishWholeTest();
}

void MainWindow::onStart()
{
    if (m_runner->isBusy())
        return;

    FioConfig cfg = configFromUi();

    // 保险丝：自检明确指定了目标盘，就绝不能跑到别的盘上去。
    // refreshDriveList() 里已经不会静默回退到第一行，这里再核对一次，
    // 因为"自检写错盘"的代价是往系统盘上真写数据。
    if (m_selfTestMode && !m_selfTestTarget.isEmpty()
        && normalizedRoot(cfg.targetRoot) != normalizedRoot(m_selfTestTarget)) {
        abortStart(kErrTargetMismatch,
                   kMsgSelfTestTargetMismatch
                       .arg(m_selfTestTarget,
                            cfg.targetRoot.isEmpty() ? kTextNoneInParens : cfg.targetRoot));
        return;
    }

    // 先记下来：即便后面在校验阶段就失败，报告里的"本次尝试的配置"也是准的，
    // 否则会显示成 FioConfig 的默认值，误导排查。
    m_activeConfig = cfg;
    m_lastError.clear();

    QString error;
    if (!cfg.validate(&error)) {
        abortStart(kErrBadParams, error);
        return;
    }

    const QString fioPath = m_fioPathEdit->text().trimmed();
    if (fioPath.isEmpty() || !QFileInfo::exists(fioPath)) {
        abortStart(kErrFioNotFound,
                   kErrSpecifyFioPath);
        return;
    }
    m_runner->setFioPath(fioPath);

    const QString folder = cfg.testFolder();

    m_activeConfig = cfg;
    m_outcomes.clear();
    m_phaseOffsetSec = 0.0;
    m_elapsedSeconds = 0.0;
    m_logLines.clear();
    m_observedStates.clear();
    m_chart->clear();
    m_stopRequested = false;
    // 新一轮测试：把上一轮的校验失败告警和图表上的标记清掉，
    // 否则新测试一开始就挂着旧结论、图上还留着上一轮的黄线。
    m_verifyFailures = 0;
    // 文件大小**不能在这里取**：m_activePlan 要到 startNextRound() 里
    // 才按当时的可用空间算出来，这里取到的只会是上一轮的（或空的）计划。
    // 真正的赋值在 startNextRound() 里紧跟着 m_activePlan = plan，
    // 只读那条路径则在扫描完盘上现成文件之后赋值。
    m_verifyFileSize = 0;
    m_verifyFileSizes.clear();
    if (m_chart)
        m_chart->clearVerifyMarkers();
    updateVerifyBanner();
    // 每轮测试重新决定画哪几条线：只有真正产生过流量的方向才会出现
    m_plotRead = false;
    m_plotWrite = false;
    m_phaseRead = true;
    m_phaseWrite = true;
    clearAllStats();
    setRunningState(true);

    m_totalRounds = qMax(1, cfg.rounds);
    m_currentRound = 0;
    m_roundOutcomeStart = 0;

    m_chartStack->setCurrentIndex(1);   // 第一次开始测试才显示图表
    recordOutput(kReportTestStart);
    recordOutput(kReportConfig.arg(cfg.describe()));
    recordOutput(kReportTargetFolder.arg(QDir::toNativeSeparators(folder)));

    if (cfg.testMode == FioConfig::TestMode::Performance) {
        startPerfRun();
        return;
    }

    if (m_totalRounds > 1)
        logEvent(kLogRoundsPlanned.arg(m_totalRounds));

    startNextRound();
}

void MainWindow::startNextRound()
{
    ++m_currentRound;

    const FioConfig cfg = m_activeConfig;
    const QString folder = cfg.testFolder();

    // 每一圈都当成一次独立的测试：图表、统计、曲线方向全部从头开始
    m_chart->clear();
    m_plotRead = false;
    m_plotWrite = false;
    m_phaseRead = true;
    m_phaseWrite = true;
    clearAllStats();
    m_roundOutcomeStart = m_outcomes.size();

    if (m_totalRounds > 1)
        logEvent(kLogRoundStart.arg(m_currentRound).arg(m_totalRounds));
    else
        logEvent(kLogRoundStartOne);

    QString error;

    if (!cfg.doWrite) {
        // 只读：直接用现成的测试文件，**不删**
        const TestFileSet existing = scanTestFiles(folder);
        if (existing.isEmpty()) {
            abortStart(kErrNoTestFiles,
                       kErrNoTestFilesForRead
                           .arg(QDir::toNativeSeparators(folder)));
            return;
        }
        recordOutput(kReportReadOnlyMode
                         .arg(existing.count)
                         .arg(formatBytes(quint64(existing.totalBytes))));
        // 坏块位置换算的基准：用**盘上这些文件的实际大小**。
        // 这条路径不经过下面的 planFiles()，所以 m_verifyFileSizes 不会被赋值——
        // 漏掉这一步的后果是坏块全挤在图上最前面一段（单文件大小为 0 时
        // 序号就白拿了）。而且只读用的文件不一定等大（可能是上一轮留下的
        // 别的尺寸），所以逐个大小时最准。
        m_verifyFileSizes = existing.sizes;
        m_verifyFileSize = existing.sizes.isEmpty() ? 0 : existing.sizes.first();
        if (m_chart)
            m_chart->clearVerifyMarkers();
        prepareCapacityAxis(existing.totalBytes);
        beginReadPhase();
        return;
    }

    m_phase = Phase::Preparing;
    updateStateLabel();

    // 顺序很重要：**先删旧文件、再查剩余空间、最后算文件计划**。
    // 之前是先按"旧文件还占着空间"的剩余量算好计划，删完旧文件空间明明变多了，
    // 却还按旧计划写，导致实际写入量远小于分区可用空间、数据对不上。
    //
    // 删之前必须确认。勾选「写入」时已经问过一次；这里再兜一道，
    // 因为「写入」可能是从上次设置里恢复成勾选的（那就没有触发过勾选事件），
    // 不加这一道就会在用户毫不知情的情况下删掉旧数据。
    // 第二圈之后不再问：那些文件本来就是这个测试自己刚写出来的。
    if (m_currentRound == 1 && !confirmOverwriteTestFiles()) {
        recordOutput(kLogOverwriteCancelled);
        logEvent(kLogOverwriteCancelled, true);
        setRunningState(false);
        m_phase = Phase::Idle;
        updateStateLabel();
        if (m_selfTestMode)
            finishWholeTest();
        return;
    }

    const TestFileSet oldFiles = scanTestFiles(folder);
    if (!oldFiles.isEmpty())
        statusBar()->showMessage(kStatusCleaningOldFiles);

    if (!clearTestFiles(folder, &error)) {
        abortStart(kErrCleanupOldFailed, error, true);
        return;
    }
    if (!ensureTestFolder(folder, &error)) {
        abortStart(kErrCreateFolderFailed, error, true);
        return;
    }
    if (!oldFiles.isEmpty()) {
        if (m_currentRound > 1)
            logEvent(kLogRoundCleanNext.arg(oldFiles.count));
        else
            logEvent(kLogCleanedOldFiles.arg(oldFiles.count));
    }
    updateDiskBarFor(cfg.targetRoot);

    // 旧文件已经删掉，现在量到的剩余空间才是这次真正能用的
    quint64 total = 0;
    quint64 free = 0;
    if (!queryVolumeSpace(cfg.targetRoot, &total, &free)) {
        abortStart(kErrReadCapacityFailed,
                   kErrReadCapacity.arg(cfg.targetRoot));
        return;
    }

    const FioConfig::FilePlan plan = cfg.planFiles(total, free);
    if (!plan.isValid()) {
        abortStart(kErrNoSpace,
                   kErrNoWritableSpace
                       .arg(formatBytes(free)));
        return;
    }

    m_activePlan = plan;
    // 坏块位置换算成图表横坐标时要用它（见 verifyPositionToAxisX）。
    // **必须紧跟 m_activePlan 赋值**：每圈的可用空间可能不一样，
    // 文件大小跟着变，坏块位置的换算基准也得跟着更新。
    m_verifyFileSize = plan.fileSize;
    m_verifyFileSizes.clear();
    for (int i = 0; i < plan.fileCount; ++i)
        m_verifyFileSizes << plan.fileSize;
    // 新一圈开始：把上一圈的坏块标记清掉（本轮的位置由本轮的偏移重新算）
    if (m_chart)
        m_chart->clearVerifyMarkers();
    recordOutput(kReportVolumeSpace
                     .arg(formatBytes(total), formatBytes(free),
                          formatBytes(quint64(FioConfig::reserveBytes(total)))));
    recordOutput(kReportWritePlan
                     .arg(plan.fileCount)
                     .arg(formatBytes(quint64(plan.fileSize)),
                          formatBytes(quint64(plan.totalBytes))));

    // 横轴按"可用空间"固定：写入和读取各自从头铺一遍，不是首尾相接。
    // 开测前就定死，之后只加点不扩轴。
    prepareCapacityAxis(plan.totalBytes);

    beginWritePhase();
}

void MainWindow::appendSamplePoint(double readX, double writeX,
                                   double readMiBs, double writeMiBs, double durationSec)
{
    m_readSamples.append(readMiBs);
    m_writeSamples.append(writeMiBs);
    m_sampleTimes.append(readX);
    m_writeTimes.append(writeX);
    m_sampleDurations.append(durationSec);
    m_sampleFlags.append(quint8((m_phaseRead ? 1 : 0) | (m_phaseWrite ? 2 : 0)));
    m_chart->appendSample(readX, m_phaseRead, readMiBs, writeX, m_phaseWrite, writeMiBs);
}

void MainWindow::prepareCapacityAxis(qint64 totalBytes)
{
    m_axisByteTotal = qMax<qint64>(1, totalBytes);
    const double gib = double(m_axisByteTotal) / (1024.0 * 1024.0 * 1024.0);
    m_chart->setCapacityAxis(gib);
    applyDensity();   // 低密度档的抽稀步长取决于数据量，这里一并定下来
    recordOutput(kReportAxisFixed
                     .arg(formatBytes(quint64(m_axisByteTotal))));
}

void MainWindow::beginWritePhase()
{
    const FioConfig &cfg = m_activeConfig;
    const QString folder = cfg.testFolder();

    const QString jobText = cfg.writeJobFileText(m_activePlan);
    recordOutput(kReportWriteJobHeader);
    for (const QString &line : jobText.split(QLatin1Char('\n')))
        recordOutput(line);

    QString error;
    m_jobFilePath = FioRunner::writeJobFile(jobText, QStringLiteral("ml_disktest-write.fio"), &error);
    if (m_jobFilePath.isEmpty()) {
        reportProblem(kErrCreateJobFile, error, true);
        setRunningState(false);
        updateStateLabel();
        return;
    }

    m_phase = Phase::Writing;
    logEvent(kLogPhaseWrite);
    m_phaseRead = false;    // 写入阶段只有写流量
    m_phaseWrite = true;
    m_plotRead = m_plotRead || m_phaseRead;
    m_plotWrite = m_plotWrite || m_phaseWrite;
    m_phaseByteBase = 0;
    m_phaseByteTotal = m_activePlan.totalBytes;
    resetPhaseStats();
    m_chart->setDirections(m_plotRead, m_plotWrite);
    updateStateLabel();

    if (!m_runner->startJob(folder, m_jobFilePath, cfg.writeExtraArguments(),
                            JobKind::Write, &error)) {
        reportProblem(kErrStartWritePhase, error, true);
        setRunningState(false);
        updateStateLabel();
        return;
    }
    statusBar()->showMessage(kStatusWriting);
    if (m_selfTestMode && m_selfTestStopAfter > 0)
        QTimer::singleShot(m_selfTestStopAfter * 1000, this, &MainWindow::onStop);
}

void MainWindow::beginReadPhase()
{
    const FioConfig &cfg = m_activeConfig;
    const QString folder = cfg.testFolder();

    const TestFileSet set = scanTestFiles(folder);
    if (set.isEmpty()) {
        reportProblem(kErrNoTestFiles,
                      kErrNoTestFilesAtReadStart
                          .arg(QDir::toNativeSeparators(folder)));
        finishWholeTest();
        return;
    }

    recordOutput(kReportReadPhaseFiles
                     .arg(set.count)
                     .arg(formatBytes(quint64(set.totalBytes))));

    const QString jobText = cfg.readJobFileText(set.count, set.totalBytes);
    recordOutput(kReportReadJobHeader);
    for (const QString &line : jobText.split(QLatin1Char('\n')))
        recordOutput(line);

    QString error;
    m_jobFilePath = FioRunner::writeJobFile(jobText, QStringLiteral("ml_disktest-read.fio"), &error);
    if (m_jobFilePath.isEmpty()) {
        reportProblem(kErrCreateJobFile, error, true);
        setRunningState(false);
        updateStateLabel();
        return;
    }

    m_phase = Phase::Reading;
    logEvent(kLogPhaseRead);
    m_phaseRead = true;
    m_phaseWrite = false;   // 读取阶段永远是纯读
    // 可见性是"并集"：写入阶段画过的蓝线要留在图上，不能被这一步藏掉
    m_plotRead = m_plotRead || m_phaseRead;
    m_plotWrite = m_plotWrite || m_phaseWrite;
    // 横轴按可用空间：读取阶段也从 0 开始铺，不接在写入后面
    m_phaseByteBase = 0;
    m_phaseByteTotal = set.totalBytes;
    resetPhaseStats();
    m_chart->setDirections(m_plotRead, m_plotWrite);
    updateStateLabel();

    if (!m_runner->startJob(folder, m_jobFilePath, cfg.readExtraArguments(),
                            JobKind::Read, &error)) {
        reportProblem(kErrStartReadPhase, error, true);
        setRunningState(false);
        updateStateLabel();
        return;
    }
    statusBar()->showMessage(kStatusReading);
    if (m_selfTestMode && m_selfTestStopAfter > 0)
        QTimer::singleShot(m_selfTestStopAfter * 1000, this, &MainWindow::onStop);
}

void MainWindow::onStop()
{
    // 一旦按下停止就不再接收采样点：曲线停在按下这一刻，
    // 不会被收尾那一拍或被补到阶段终点的"闭合点"拖到横轴末尾。
    m_stopRequested = true;
    logEvent(kLogStopRequested);
    m_runner->stop();
    statusBar()->showMessage(kStatusStopping);
}

void MainWindow::onIoRate(double readBytesPerSec, double writeBytesPerSec,
                          double intervalStartSec, double intervalSec,
                          quint64 cumulativeReadBytes, quint64 cumulativeWriteBytes)
{
    if (m_phase != Phase::Writing && m_phase != Phase::Reading)
        return;
    // 已经点了停止：不再往图表和统计里加任何点
    if (m_stopRequested)
        return;

    m_elapsedSeconds += intervalSec;

    const double readMiBs = readBytesPerSec / (1024.0 * 1024.0);
    const double writeMiBs = writeBytesPerSec / (1024.0 * 1024.0);

    // 丢掉每个阶段的**第 1 拍**：它覆盖的是 fio 启动、建文件、缓存预热的开销，
    // 画出来就是一根扎到底的假谷底，还会把平均值拉低。
    // 但它在容量轴上占掉的那一段（约 2%）不能凭空消失，否则曲线看起来不是从头开始，
    // 所以记下标记，等第 2 拍来了在容量 0 处补一个"起点锚"。
    if (m_phaseSampleCount++ == 0) {
        ++m_skippedFirstSamples;
        m_readBytesInPhase = double(cumulativeReadBytes);
        m_writeBytesInPhase = double(cumulativeWriteBytes);
        m_phaseNeedOriginAnchor = true;
        return;
    }

    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    double readX = 0.0;
    double writeX = 0.0;
    if (m_activeConfig.testMode == FioConfig::TestMode::Performance) {
        // 性能模式：fio 实际跑 perfRuntimeSeconds()+2 秒，但只取**第 2 秒到第 11 秒**
        // 这 10 秒的采样。开头 1 秒是 fio 启动、建缓冲区、缓存预热；最后 1 秒是
        // 写报告、关文件的收尾。两头都不是盘的真实速度，干脆两头都切掉，
        // 只留中间干净的 10 秒——这样曲线和平均值都不会被两端的假数据带偏。
        const double head = FioConfig::perfHeadSkipSeconds();
        const double tail = head + FioConfig::perfRuntimeSeconds();
        const double t = intervalStartSec;   // 本趟的绝对时刻
        if (t < head || t >= tail)
            return;
        // 只收满 10 秒就停：采样是 0.5 秒一格，靠墙钟卡边界的话，
        // 边界那一拍有时落进窗口、有时落到外面，末尾就会一会儿 10.0 一会儿 9.5。
        // 按**拍数**收就完全确定：20 拍 × 0.5 秒 = 正好 10 秒。
        const int wanted = int(FioConfig::perfRuntimeSeconds() / 0.5);
        if (m_perfPassSamples >= wanted)
            return;
        ++m_perfPassSamples;
        // x 锚定在**本趟第一个有效采样点**上，而不是死按墙钟减 1 秒：
        // 采样是 0.5 秒一格，墙钟上正好落在 1.0 秒那一拍经常被抖动错过，
        // 曲线开头就会缺一个点。锚到实际第一个点，开头永远是 0。
        if (m_perfPassBase < 0.0)
            m_perfPassBase = t;
        readX = writeX = t - m_perfPassBase;
    } else {
        // 可靠性模式横轴是"已处理容量"，用进程 I/O 计数器的真实累计字节数
        readX = double(m_phaseByteBase + qint64(cumulativeReadBytes)) / kGiB;
        writeX = double(m_phaseByteBase + qint64(cumulativeWriteBytes)) / kGiB;
    }
    m_readBytesInPhase = double(cumulativeReadBytes);
    m_writeBytesInPhase = double(cumulativeWriteBytes);

    // 第 2 拍：先在容量 0 补起点锚（用第 2 拍的速率，因为被丢掉的第 1 拍速率不可信；
    // 时长为 0，不参与平均值加权），再记第 2 拍本身。
    if (m_phaseNeedOriginAnchor) {
        m_phaseNeedOriginAnchor = false;
        appendSamplePoint(0.0, 0.0, readMiBs, writeMiBs, 0.0);
    }

    // 性能模式：fio 实际跑 perfRuntimeSeconds()+1 秒，但图表和统计**只取前 10 秒**。
    // 多出来的那 1 秒是留给收尾的：fio 到点之后还要写报告、关文件，
    // 那段时间没有 I/O，采出来必然是一条向下的假尾巴。截在 10 秒就看不到它。
    if (m_activeConfig.testMode == FioConfig::TestMode::Performance
        && readX > double(FioConfig::perfRuntimeSeconds())) {
        return;
    }

    appendSamplePoint(readX, writeX, readMiBs, writeMiBs, intervalSec);

    if (m_activeConfig.testMode == FioConfig::TestMode::Performance)
        // 存**减掉本趟起点之后**的 x，和实时图表用的是同一个坐标。
        // 之前这里存的是 intervalStartSec（进程绝对时刻），回看时整条曲线会右移
        // 约 0.5 秒，左边就缺一段、右边又被 10 秒的轴截掉一截。
        recordPerfTrace(readX, readMiBs, writeMiBs);

    // 性能测试另存一份**整轮**的逐点记录（含"哪一项、哪一趟"）：
    // 主图表每换一项就 clear()，只靠 m_readSamples 的话跑完只剩最后一项，
    // 保存出来的 CSV 会缺三项的数据。
    if (m_activeConfig.testMode == FioConfig::TestMode::Performance) {
        const int itemIndex = int(FioConfig::allPerfItems().indexOf(
            m_perfQueue.value(m_perfQueueIndex, FioConfig::PerfItem::SeqQ8T1)));
        m_perfRunTimes << readX;
        m_perfRunRead << readMiBs;
        m_perfRunWrite << writeMiBs;
        m_perfRunDurations << intervalSec;
        m_perfRunFlags << quint8(((m_phaseRead) ? 1 : 0) | ((m_phaseWrite) ? 2 : 0));
        m_perfRunItems << itemIndex;
        m_perfRunPasses << m_perfPass;
    }

    if (m_phaseRead)
        m_readValueLabel->setText(formatSpeed(readMiBs));
    if (m_phaseWrite)
        m_writeValueLabel->setText(formatSpeed(writeMiBs));

    m_elapsedLabel->setText(formatDuration(qint64(m_elapsedSeconds)));

    const RunStats stats = computeRunStats(false);
    if (m_phaseRead)
        m_avgReadLabel->setText(formatSpeed(stats.readAvg));
    if (m_phaseWrite)
        m_avgWriteLabel->setText(formatSpeed(stats.writeAvg));

    // 分区的占用情况跟着变，隔几拍刷一次就够了
    if (m_readSamples.size() % 4 == 0)
        updateDiskBarFor(m_activeConfig.targetRoot);
}

void MainWindow::onRunnerOutput(const QString &text)
{
    recordOutput(text);
}

void MainWindow::onRunnerWarning(const QString &text)
{
    recordOutput(kReportWarningLine.arg(text));
    logEvent(kLogWarnPrefix.arg(text), true);   // 异常用红字
}

void MainWindow::updateVerifyBanner()
{
    if (!m_verifyBanner)
        return;
    if (m_verifyFailures <= 0) {
        m_verifyBanner->setVisible(false);
        return;
    }
    m_verifyBannerLabel->setText(kVerifyBannerHead.arg(m_verifyFailures));
    m_verifyBanner->setVisible(true);
}

void MainWindow::setDebugForceVerifyBanner(bool force)
{
    m_debugForceVerifyBanner = force;
}

double MainWindow::verifyPositionToAxisX(int fileIndex, qint64 fileOffset) const
{
    // 图表横轴 = "已处理容量"（GiB）；fio 给的是**第 N 个文件内的第 off 字节**。
    //
    // fio 是顺序读的，所以读到第 N 个文件的 off 处时，已经处理掉的字节就是
    // **前面所有文件的总长 + off**：
    //     全局字节 = Σ(前 fileIndex 个文件的大小) + off
    //
    // 两个都不能省：
    //   * **文件序号**：fio 报的偏移每个文件都从 0 开始，testfile_0 的 256 MiB
    //     和 testfile_3 的 256 MiB 是两个不同的盘上位置。只用偏移定位的话，
    //     所有坏块都会挤到图上最前面那一段。
    //   * **逐文件大小**：只读校验用的是盘上现成的文件，它们不一定等大
    //     （可能是上一轮用别的测试数据尺寸留下的），不能拿一个统一大小去乘。
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    if (fileOffset < 0)
        return -1.0;
    const int index = qMax(0, fileIndex);

    qint64 before = 0;
    if (!m_verifyFileSizes.isEmpty()) {
        // 逐文件大小：累加序号之前的那些文件
        for (int i = 0; i < index && i < m_verifyFileSizes.size(); ++i)
            before += m_verifyFileSizes.at(i);
        // 序号超出已知文件数（理论上不会）：后面的按最后那个文件的大小顺延
        if (index > m_verifyFileSizes.size() && !m_verifyFileSizes.isEmpty()) {
            const qint64 last = m_verifyFileSizes.last();
            before += last * (index - m_verifyFileSizes.size());
        }
    } else if (m_verifyFileSize > 0) {
        // 没有逐文件大小（老文件只有 file_size_bytes）：只能按统一大小算
        before = index * m_verifyFileSize;
    }
    // 两者都没有：只能把它当成"第一个文件里的这个偏移"，位置会偏但不会离谱
    return double(before + fileOffset) / kGiB;
}

void MainWindow::onVerifyFailure(int totalCount, const QString &text,
                                 qint64 fileOffset, int fileIndex)
{
    m_verifyFailures = totalCount;
    // 实时把告警条挂出来，并在日志/报告里记下**这一处的偏移量**。
    // text 是这一批里最后一条原话，不是第一条——每处的偏移量都要留下，
    // 否则日志里会看着像"所有失败都在同一个位置"。
    logEvent(kLogVerifyFailure.arg(totalCount).arg(text), true);
    recordOutput(kReportVerifyLine.arg(totalCount).arg(text));
    statusBar()->showMessage(kStatusVerifyFailed.arg(totalCount));
    updateVerifyBanner();

    // 在图表上把这一处标出来：黄色背景色带，横坐标对准坏块在盘上的位置。
    const double x = verifyPositionToAxisX(fileIndex, fileOffset);
    if (x >= 0.0 && m_chart)
        m_chart->addVerifyMarker(x);
}

void MainWindow::onJobFinished(const FioResult &result)
{
    // ---- 性能模式：记下这一项的结果，接着跑下一项 ----
    if (result.kind == JobKind::Perf) {
        const FioConfig::PerfItem item =
            m_perfQueue.value(m_perfQueueIndex, FioConfig::PerfItem::SeqQ8T1);

        // 这一趟跑完就**立刻**把对应的列填上（不等另一趟）
        const int cellIndex = int(FioConfig::allPerfItems().indexOf(item));
        const bool wasWritePass = (m_perfPass == 1);

        // 补一个**终点**：采样点记的是每个 0.5 秒区间的起点，10 秒的窗口正好
        // 20 个区间，最后一个起点是 9.5 秒——右边缘就空着一格。
        // 轨迹、**实时图表**、**存档数组**三处都要补：
        // 只补前两处的话，现场看着是齐的，存成 CSV 再打开就少了最后那一点
        // （实测：CSV 里每趟都停在 t=9.51，图上却画到 10.0）。
        // 值沿用最后一个区间的速率，不引入任何新测量。
        if (cellIndex >= 0 && cellIndex < 4) {
            const double endX = double(FioConfig::perfRuntimeSeconds());
            if (wasWritePass) {
                QVector<QPointF> &trace = m_perfWriteTrace[cellIndex];
                if (!trace.isEmpty() && trace.last().x() < endX) {
                    const double tail = trace.last().y();
                    trace.append(QPointF(endX, tail));
                    m_chart->appendSample(endX, false, 0.0, endX, true, tail);
                    m_perfRunTimes << endX;
                    m_perfRunRead << 0.0;
                    m_perfRunWrite << tail;
                    m_perfRunDurations << 0.0;
                    m_perfRunFlags << quint8(2);            // 只有写
                    m_perfRunItems << cellIndex;
                    m_perfRunPasses << 1;
                }
            } else {
                QVector<QPointF> &trace = m_perfReadTrace[cellIndex];
                if (!trace.isEmpty() && trace.last().x() < endX) {
                    const double tail = trace.last().y();
                    trace.append(QPointF(endX, tail));
                    m_chart->appendSample(endX, true, tail, endX, false, 0.0);
                    m_perfRunTimes << endX;
                    m_perfRunRead << tail;
                    m_perfRunWrite << 0.0;
                    m_perfRunDurations << 0.0;
                    m_perfRunFlags << quint8(1);            // 只有读
                    m_perfRunItems << cellIndex;
                    m_perfRunPasses << 0;
                }
            }
            // 这一项的曲线加完了：重建折线保证 10.0 秒那个收尾点画出来。
            // 它是第 21 个点，不是抽稀步长的整数倍时会被丢掉，
            // 曲线就停在 9.5 秒、右边缘空一格（和回看时是同一个毛病）。
            m_chart->finishSamples();
        }
        if (cellIndex >= 0 && cellIndex < 4) {
            PerfCell *cell = wasWritePass ? m_perfWriteCells[cellIndex]
                                          : m_perfReadCells[cellIndex];
            const double value = wasWritePass
                                     ? (result.hasWrite ? result.writeMiBs : -1.0)
                                     : (result.hasRead ? result.readMiBs : -1.0);
            if (cell)
                cell->setValue(value);
            // 整趟结果另存一份：保存 CSV 时四项都要写上，不能只看当前这一项。
            if (wasWritePass)
                m_perfValues.write[cellIndex] = value;
            else
                m_perfValues.read[cellIndex] = value;
            rescalePerfCells();
        }
        logEvent(kLogPerfPassDone.arg(FioConfig::perfItemName(item),
                                      wasWritePass ? kPhaseWrite : kPhaseRead)
                     .arg(wasWritePass ? (result.hasWrite ? formatSpeed(result.writeMiBs) : kTextNone)
                                       : (result.hasRead ? formatSpeed(result.readMiBs) : kTextNone)),
                 !result.warnings.isEmpty());

        if (wasWritePass)
            ++m_perfResultCount;

        // 用户点了停止 → 当前进程已经结束，未测的项也不再跑，删掉测试文件
        if (m_stopRequested) {
            deletePerfFiles();
            finishPerfRun();
            return;
        }

        if (!wasWritePass) {
            m_perfPass = 1;          // 接着跑写入趟
            startPerfPass();
            return;
        }

        // 读写两趟都跑完了，换下一项
        ++m_perfQueueIndex;
        m_perfPass = 0;
        updateDiskBarFor(m_activeConfig.targetRoot);
        startNextPerfItem();
        return;
    }

    const QString endRoot = m_activeConfig.targetRoot;

    dropTrailingTailSample();

    // 收尾补点：把曲线拉到本阶段的容量终点（真实总量），并把这个点也记进采样数组，
    // 这样保存成 CSV 再加载回来，曲线形状完全一致。
    const RunStats stats = computeRunStats(false);
    // 只有**正常跑完**的阶段才补终点：中途停止时如果还补到计划总量，
    // 曲线会凭空画到横轴末尾，看起来像跑完了。
    if (!result.cancelled && stats.count > 0) {
        constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
        const double endX = double(m_phaseByteBase + m_phaseByteTotal) / kGiB;
        const double readTail = m_readSamples.last();
        const double writeTail = m_writeSamples.last();
        // 无条件给**本阶段产生的方向**补一个终点。
        // 不能拿 chart->lastX() 判断：那是两条线的最大值，
        // 读取阶段的终点会被写入阶段的终点挡住，导致红线够不到右边缘。
        // 时长记 0：补点没有占用新的时间，不影响按区间长度加权的平均值。
        appendSamplePoint(endX, endX, readTail, writeTail, 0.0);
    }

    PhaseOutcome outcome;
    outcome.round = m_currentRound;
    outcome.kind = result.kind;
    outcome.result = result;
    outcome.stats = stats;
    m_outcomes.append(outcome);

    // 把该方向的平均值定格在本阶段的结果上，下一阶段开始后仍然看得到
    if (stats.count > 0) {
        if (m_phaseWrite)
            m_avgWriteLabel->setText(formatSpeed(stats.writeAvg));
        if (m_phaseRead)
            m_avgReadLabel->setText(formatSpeed(stats.readAvg));
    }

    // ---- 校验结果收尾 ----
    // 读取阶段跑完：把这一阶段的坏块数写进日志。
    // 注意**不用弹窗**：一有坏块就弹框会把自动化测试卡住，
    // 而且右侧那条大字告警已经在屏幕上摆着了，弹窗只是多余的一层。
    if (result.kind == JobKind::Read) {
        if (result.verifyFailures > 0) {
            m_verifyFailures = result.verifyFailures;
            updateVerifyBanner();
            // 把每一处失败的**位置**在图表上标出来（黄色背景色带，对准横轴）。
            // 用 runner 汇总的偏移列表而不是实时信号：实时信号每批只带一条，
            // 一批里有多处时会被漏掉。
            if (m_chart) {
                const int count = qMin(result.verifyOffsets.size(),
                                       result.verifyFileIndexes.size());
                for (int i = 0; i < count; ++i) {
                    const double x = verifyPositionToAxisX(result.verifyFileIndexes.at(i),
                                                           result.verifyOffsets.at(i));
                    if (x >= 0.0)
                        m_chart->addVerifyMarker(x);
                }
            }
            // 先给总数，再逐处列出坏在哪。
            // **必须逐处列**：只报第一条会让人以为"只有那一个地方坏"，
            // 而实际上一块坏盘的失败点通常分散在很多偏移量上。
            logEvent(kLogVerifySummary.arg(result.verifyFailures), true);
            for (const QString &line : result.verifyErrorLines)
                logEvent(QStringLiteral("  ") + line, true);
            const int shown = result.verifyErrorLines.size();
            if (result.verifyFailures > shown)
                logEvent(kLogVerifyTruncated.arg(shown).arg(result.verifyFailures));
        } else if (m_activeConfig.verify && !result.cancelled) {
            logEvent(kLogVerifyNone);
        }
    }

    // 阶段结束时把**非校验类**的告警弹出来。
    //
    // 校验失败被特意排除在外，有两个原因：
    //   1. 一块坏盘能刷出几百上千条校验失败（实测 G: 上一次 926 条），
    //      弹一个千行对话框只会把右边那条大字告警整个盖住；
    //   2. 告警条已经在屏幕上把"发现多少处、坏在哪"写清楚了，
    //      再弹一个必须点确定的模态框，等于让无人值守的测试卡在那里等人。
    // 其它告警（打不开文件、空间不足、非法选项…）仍然要弹——
    // 那些是"测试本身没跑对"，不看会误判结果。
    QStringList nonVerifyWarnings;
    for (const QString &warning : result.warnings) {
        if (isVerifyRelatedWarning(warning))
            continue;
        nonVerifyWarnings << warning;
    }
    if (!nonVerifyWarnings.isEmpty() && !m_selfTestMode) {
        const QString detail = nonVerifyWarnings.join(QLatin1Char('\n'));
        QMessageBox::warning(this, kMsgPhaseProblem.arg(jobKindText(result.kind)),
                             detail + kMsgTestStoppedSuffix);
    }

    updateDiskBarFor(endRoot);

    // ---- 中途停止的收尾 ----
    // 写入阶段被停掉 → 盘上留下的是**写到一半**的数据（随机模式下还有根本没写过的洞）。
    // 留着它，下次"只读校验"必然报出一堆假坏块（校验头对不上），所以直接删干净。
    // 删除后测试目录为空，只读自然也点不了了。
    if (result.cancelled && result.kind == JobKind::Write) {
        QString error;
        if (clearTestFiles(m_activeConfig.testFolder(), &error)) {
            recordOutput(kLogStoppedWriteCleaned);
            logEvent(kLogStoppedWriteCleaned, true);
            statusBar()->showMessage(kLogStoppedWriteCleaned);
        } else {
            recordOutput(kLogStoppedWriteCleanFailed.arg(error));
        }
    }

    // 只读校验被停掉时**不动勾选状态**：停止前后的勾选保持一致，
    // 免得用户以为自己没点过什么、选项却变了。
    // （写一半停掉会删文件，那是另一回事：删的是不完整的数据。）

    // 写入阶段正常结束 → 接着读回
    if (result.kind == JobKind::Write && !result.cancelled && m_activeConfig.doRead) {
        beginReadPhase();
        return;
    }

    finishRound();
}

void MainWindow::logRoundSummary()
{
    QStringList writeAvg;
    QStringList readAvg;
    QStringList problems;
    for (int i = m_roundOutcomeStart; i < m_outcomes.size(); ++i) {
        const PhaseOutcome &o = m_outcomes.at(i);
        if (o.kind == JobKind::Write && o.stats.count > 0)
            writeAvg << formatSpeed(o.stats.writeAvg);
        if (o.kind == JobKind::Read && o.stats.count > 0)
            readAvg << formatSpeed(o.stats.readAvg);
        problems += o.result.warnings;
        for (int err : o.result.jobErrors) {
            if (err != 0)
                problems << QStringLiteral("err=%1").arg(err);
        }
    }

    const QString w = writeAvg.isEmpty() ? kTextNone : writeAvg.join(QLatin1Char('/'));
    const QString r = readAvg.isEmpty() ? kTextNone : readAvg.join(QLatin1Char('/'));
    if (problems.isEmpty())
        logEvent(kLogRoundSummary.arg(m_currentRound).arg(w, r));
    else
        logEvent(kLogRoundSummaryBad.arg(m_currentRound).arg(problems.join(QLatin1Char(';'))),
                 true);
}

void MainWindow::finishRound()
{
    logRoundSummary();

    // 还有下一圈、而且不是被用户停掉的 → 接着跑
    if (!m_stopRequested && m_currentRound < m_totalRounds) {
        startNextRound();
        return;
    }

    if (m_totalRounds > 1 && !m_stopRequested)
        logEvent(kLogAllRoundsDone.arg(m_totalRounds));
    else
        logEvent(kLogOneRoundDone);

    finishWholeTest();
}

QVector<FioConfig::PerfItem> MainWindow::checkedPerfItems() const
{
    QVector<FioConfig::PerfItem> list;
    const QVector<FioConfig::PerfItem> all = FioConfig::allPerfItems();
    for (int i = 0; i < all.size(); ++i) {
        if (m_perfChecks[i] && m_perfChecks[i]->isChecked())
            list << all.at(i);
    }
    return list;
}

void MainWindow::perfItemColors(FioConfig::PerfItem item, QColor *readColor,
                                QColor *writeColor) const
{
    // 顺序项红/蓝，随机项黄/绿。**只按测试项分**，不按"读取/写入"分——
    // 否则点顺序项的格子会画成黄绿，和列表里看到的对不上。
    if (FioConfig::perfItemIsRandom(item)) {
        *readColor = QColor(0xf0, 0xc0, 0x00);
        *writeColor = QColor(0x2e, 0xaa, 0x4a);
    } else {
        *readColor = QColor(220, 30, 30);
        *writeColor = QColor(30, 70, 220);
    }
}

void MainWindow::logPerfToChart(FioConfig::PerfItem item)
{
    QColor readColor;
    QColor writeColor;
    perfItemColors(item, &readColor, &writeColor);
    m_chart->setSeriesColors(readColor, writeColor);
}

void MainWindow::dropTrailingTailSample()
{
    // **每个阶段的最后一拍一律丢掉**，不做阈值判断。
    //
    // 原因：采样区间是 [上一拍, 这一拍]，而阶段的最后一拍必然横跨"fio 停止 I/O → 进程退出"
    // 那一刻。也就是说它覆盖的时间里有相当一部分根本没有 I/O，速率一定被拉低。
    // 实测保存下来的数据：读取阶段前面稳定 352 MiB/s，最后一拍 263 MiB/s，
    // 而这一拍**时长是满的 0.501 秒**——不是"残段"，是"这 0.5 秒里读完了、剩下在收尾"。
    // 所以靠"比中位数低多少"来判是判不准的，只有"最后一拍"这个位置本身是可靠信号。
    //
    // 丢这一拍只可能让速率**少报**、不会多报；而且它紧接着会被收尾补点复制到最右边缘，
    // 留着就等于把假谷底钉在曲线末端。
    if (m_tailDropped)
        return;   // 每个阶段只丢一次
    const int n = m_readSamples.size();
    if (n < 3)
        return;   // 点太少就不动，免得把短测试的曲线删空
    m_tailDropped = true;

    const quint8 lastFlags = m_sampleFlags.at(n - 1);
    const double lastRate = (lastFlags & 2) ? m_writeSamples.at(n - 1)
                                            : m_readSamples.at(n - 1);

    recordOutput(kLogDroppedTail.arg(formatSpeed(lastRate)));
    m_readSamples.removeLast();
    m_writeSamples.removeLast();
    m_sampleTimes.removeLast();
    m_writeTimes.removeLast();
    m_sampleDurations.removeLast();
    m_sampleFlags.removeLast();
    rebuildChartFromSamples();
}

void MainWindow::rebuildChartFromSamples()
{
    // 图表是边采边画的，删掉一个采样点只能整条重画。
    // 采样点数量在几百这个量级，重建一次毫无压力。
    m_chart->clear();
    for (int i = 0; i < m_readSamples.size(); ++i) {
        const quint8 f = m_sampleFlags.at(i);
        m_chart->appendSample(m_sampleTimes.at(i), (f & 1) != 0, m_readSamples.at(i),
                              m_writeTimes.at(i), (f & 2) != 0, m_writeSamples.at(i));
    }
    m_chart->setDirections(m_plotRead, m_plotWrite);
}

void MainWindow::rescalePerfCells()
{
    // 所有格子共用一个满量程，条长才有可比性
    double maxValue = 1.0;
    for (int i = 0; i < 4; ++i) {
        if (m_perfReadCells[i])
            maxValue = qMax(maxValue, m_perfReadCells[i]->value());
        if (m_perfWriteCells[i])
            maxValue = qMax(maxValue, m_perfWriteCells[i]->value());
    }
    for (int i = 0; i < 4; ++i) {
        if (m_perfReadCells[i])
            m_perfReadCells[i]->setScaleMax(maxValue);
        if (m_perfWriteCells[i])
            m_perfWriteCells[i]->setScaleMax(maxValue);
    }
}

void MainWindow::startPerfRun()
{
    const FioConfig cfg = m_activeConfig;
    const QString folder = cfg.testFolder();

    m_perfQueue = checkedPerfItems();
    m_perfQueueIndex = 0;
    // 重新测试：清掉这个盘上次的结果（其它盘的不动）
    m_perfResultsRoot = cfg.targetRoot;
    m_perfResults.remove(cfg.targetRoot);
    m_perfPass = 0;
    m_perfResultCount = 0;
    resetPerfRunSamples();
    m_chartStack->setCurrentIndex(1);
    m_chart->clear();
    for (int i = 0; i < 4; ++i) {
        if (m_perfReadCells[i])
            m_perfReadCells[i]->clear();
        if (m_perfWriteCells[i])
            m_perfWriteCells[i]->clear();
    }

    // 预先建好测试文件：性能测试必须"文件已就绪"才开始计时，
    // 否则 fio 的布局时间会混进 10 秒的测量窗口里。
    const FioConfig::FilePlan plan = FioConfig::planFilesForSize(cfg.perfDataBytes);
    if (!plan.isValid()) {
        abortStart(kErrBadParams, kErrPerfDataSize);
        return;
    }

    quint64 total = 0;
    quint64 free = 0;
    if (!queryVolumeSpace(cfg.targetRoot, &total, &free)) {
        abortStart(kErrReadCapacityFailed, kErrReadCapacity.arg(cfg.targetRoot));
        return;
    }
    if (qint64(free) < plan.totalBytes + FioConfig::reserveBytes(total)) {
        abortStart(kErrNoSpace,
                   kErrPerfNoSpace.arg(formatBytes(free),
                                       FioConfig::perfSizeText(plan.totalBytes)));
        return;
    }

    // 性能测试固定单文件，所以 FAT32 上 > 4 GiB 根本建不出来。
    // 在开测前就拦下来，别等写了一半才失败、在盘上留个半截文件。
    const QString fsName = queryFileSystem(cfg.targetRoot).toUpper();
    if (fsName.startsWith(QStringLiteral("FAT"))
        && plan.totalBytes >= qint64(4) * 1024 * 1024 * 1024) {   // 4 GiB 本身也放不下
        abortStart(kMsgFat32TooBigTitle,
                   kErrFat32TooBig.arg(QDir::toNativeSeparators(cfg.targetRoot),
                                       FioConfig::perfSizeText(plan.totalBytes)));
        return;
    }

    const QString what = kLogPreallocating.arg(plan.fileCount)
                             .arg(formatBytes(quint64(plan.fileSize)));
    logEvent(what);
    statusBar()->showMessage(what);

    QString error;
    if (!confirmOverwriteTestFiles()) {
        logEvent(kLogOverwriteCancelled, true);
        abortStart(kErrBadParams, kLogOverwriteCancelled);
        return;
    }
    if (!clearTestFiles(folder, &error) || !ensureTestFolder(folder, &error)) {
        abortStart(kErrCleanupOldFailed, error, true);
        return;
    }

    // 预建文件一个一个来：建 32 GiB 要几十秒，一次全建完界面会假死。
    // 每建完一个回到事件循环一次，界面照常响应，状态显示"准备中"。
    m_perfFileCount = plan.fileCount;
    m_perfFileSize = plan.fileSize;
    m_preallocCount = plan.fileCount;
    m_preallocSize = plan.fileSize;
    m_preallocIndex = 0;
    m_phase = Phase::Preparing;
    updateStateLabel();
    if (!m_preallocTimer) {
        m_preallocTimer = new QTimer(this);
        connect(m_preallocTimer, &QTimer::timeout, this, &MainWindow::preallocNextFile);
    }
    m_preallocTimer->start(0);
}

void MainWindow::startNextPerfItem()
{
    if (m_stopRequested || m_perfQueueIndex >= m_perfQueue.size()) {
        finishPerfRun();
        return;
    }

    const FioConfig::PerfItem item = m_perfQueue.at(m_perfQueueIndex);

    // 每一项都独立成图：横轴固定 0 ~ (10 秒 + 预热 + 启动余量)
    m_chart->clear();
    m_chart->setTimeAxis(FioConfig::perfRuntimeSeconds());
    m_phaseRead = true;
    m_phaseWrite = true;
    m_plotRead = true;
    m_plotWrite = true;
    m_chart->setDirections(true, true);
    logPerfToChart(item);
    const int traceIndex = int(FioConfig::allPerfItems().indexOf(item));
    if (traceIndex >= 0 && traceIndex < 4) {
        m_perfReadTrace[traceIndex].clear();
        m_perfWriteTrace[traceIndex].clear();
        if (m_perfReadCells[traceIndex])
            m_perfReadCells[traceIndex]->setClickable(false);
        if (m_perfWriteCells[traceIndex])
            m_perfWriteCells[traceIndex]->setClickable(false);
    }
    clearAllStats();
    m_phaseStartIndex = 0;
    m_phaseSampleCount = 0;
    m_phaseNeedOriginAnchor = false;
    m_phaseByteBase = 0;
    m_perfPass = 0;

    m_phase = Phase::Reading;   // 正在跑一项测试
    updateStateLabel();
    logEvent(kLogPerfItemStart.arg(FioConfig::perfItemName(item))
                 .arg(FioConfig::perfRuntimeSeconds()));

    startPerfPass();
}

void MainWindow::startPerfPass()
{
    const FioConfig::PerfItem item = m_perfQueue.at(m_perfQueueIndex);
    const FioConfig &cfg = m_activeConfig;
    const bool writePass = (m_perfPass == 1);   // 先读后写

    // 每一趟单独计时：读写的采样都从 0 秒开始，两条线就叠在同一段时间上。
    // 关键：**每趟只开一个方向**。如果两个方向都开着，写入趟会给"读取"曲线
    // 记一串 0、读取趟又给"写入"记一串 0，折线跨趟连起来就是两条斜线。
    // 图上看不看得到某条线由 m_plotRead/m_plotWrite（并集）决定，和这里无关。
    m_phaseStartIndex = m_readSamples.size();
    m_phaseSampleCount = 0;
    m_phaseNeedOriginAnchor = false;
    m_tailDropped = false;
    m_perfPassBase = -1.0;   // 本趟第一个**有效**采样点的时刻，用来把曲线拉到 0 秒
    m_perfPassSamples = 0;
    m_phaseRead = !writePass;
    m_phaseWrite = writePass;
    m_phase = writePass ? Phase::Writing : Phase::Reading;
    m_chart->setDirections(m_plotRead, m_plotWrite);
    updateStateLabel();
    // 状态栏跟着走到当前这一趟，否则会一直停在预建文件那句"准备中"
    statusBar()->showMessage(kStatusPerfPass.arg(FioConfig::perfItemName(item),
                                                 writePass ? kPhaseWrite : kPhaseRead));
    logEvent(kLogPerfPassStart.arg(FioConfig::perfItemName(item),
                                   writePass ? kPhaseWrite : kPhaseRead)
                 .arg(FioConfig::perfRuntimeSeconds()));

    const QString jobText =
        cfg.perfJobFileText(item, writePass, m_perfFileCount, m_perfFileSize);
    recordOutput(kPhaseHeadFmt.arg(
        QStringLiteral("%1 / %2").arg(FioConfig::perfItemName(item),
                                      writePass ? kPhaseWrite : kPhaseRead)));
    for (const QString &line : jobText.split(QLatin1Char('\n')))
        recordOutput(line);

    QString error;
    const QString jobPath = FioRunner::writeJobFile(
        jobText, QStringLiteral("ml_disktest-perf.fio"), &error);
    if (jobPath.isEmpty()) {
        abortStart(kErrCreateJobFailed, error, true);
        return;
    }

    if (!m_runner->startJob(cfg.testFolder(), jobPath, cfg.commonExtraArguments(),
                            JobKind::Perf, &error)) {
        abortStart(kErrStartReadPhase, error, true);
        return;
    }
}

void MainWindow::preallocNextFile()
{
    const FioConfig &cfg = m_activeConfig;

    if (m_stopRequested) {
        m_preallocTimer->stop();
        if (m_preallocFile) {
            m_preallocFile->close();
            delete m_preallocFile;
            m_preallocFile = nullptr;
        }
        deletePerfFiles();
        finishPerfRun();
        return;
    }

    if (m_preallocIndex >= m_preallocCount) {
        m_preallocTimer->stop();
        updateDiskBarFor(cfg.targetRoot);
        logEvent(kLogPreallocated.arg(formatBytes(quint64(m_preallocSize * m_preallocCount))));
        logEvent(kLogPerfAllPlanned.arg(m_perfQueue.size())
                     .arg(m_perfQueue.size() * FioConfig::perfPassWallSeconds() * 2));
        startNextPerfItem();
        return;
    }

    // **必须真的把数据写进去，不能用 QFile::resize()**。
    // NTFS 上 SetEndOfFile 只是把文件"加长"，新区域的"有效数据长度"(VDL) 没被覆盖；
    // 直接 I/O 读那片区域时文件系统直接返回全 0、根本不碰盘，
    // 顺序读就会虚高到内存速度级别（内置硬盘上特别明显，exFAT 的 U 盘上看不出来）。
    // 写一遍数据才能把 VDL 铺满，读到的才是盘上真实的数据。
    constexpr qint64 kChunk = 4LL * 1024 * 1024;
    static const QByteArray kZeros(int(kChunk), '\0');

    if (!m_preallocFile) {
        QDir dir(cfg.testFolder());
        m_preallocFile = new QFile(dir.absoluteFilePath(
            QStringLiteral("%1_%2").arg(FioConfig::fileBaseName()).arg(m_preallocIndex)));
        if (!m_preallocFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString detail = QStringLiteral("%1：%2")
                                       .arg(m_preallocFile->fileName(),
                                            m_preallocFile->errorString());
            delete m_preallocFile;
            m_preallocFile = nullptr;
            m_preallocTimer->stop();
            abortStart(kErrPreallocFailed, detail, true);
            return;
        }
        m_preallocOffset = 0;
    }

    const qint64 remaining = m_preallocSize - m_preallocOffset;
    const qint64 chunk = qMin(kChunk, remaining);
    if (m_preallocFile->write(kZeros.constData(), chunk) != chunk) {
        const QString detail = QStringLiteral("%1：%2")
                                   .arg(m_preallocFile->fileName(),
                                        m_preallocFile->errorString());
        m_preallocFile->close();
        delete m_preallocFile;
        m_preallocFile = nullptr;
        m_preallocTimer->stop();
        abortStart(kErrPreallocFailed, detail, true);
        return;
    }
    m_preallocOffset += chunk;

    if (m_preallocOffset >= m_preallocSize) {
        m_preallocFile->close();
        delete m_preallocFile;
        m_preallocFile = nullptr;

        ++m_preallocIndex;
        statusBar()->showMessage(kLogPreparingFiles.arg(m_preallocIndex).arg(m_preallocCount));
        if (m_preallocIndex % 4 == 0 || m_preallocIndex == m_preallocCount)
            logEvent(kLogPreparingFiles.arg(m_preallocIndex).arg(m_preallocCount));
    }
}

void MainWindow::deletePerfFiles()
{
    const QString folder = m_activeConfig.testFolder();
    QString error;
    if (!clearTestFiles(folder, &error)) {
        logEvent(kLogStoppedWriteCleanFailed.arg(error), true);
        return;
    }
    // 文件删完再把 ml_disktest 目录本身也删掉，别在盘上留个空壳
    QDir().rmdir(folder);
    logEvent(kLogPerfCleanup);
}

void MainWindow::recordPerfTrace(double x, double readMiBs, double writeMiBs)
{
    const int index = int(FioConfig::allPerfItems().indexOf(
        m_perfQueue.value(m_perfQueueIndex, FioConfig::PerfItem::SeqQ8T1)));
    if (index < 0 || index >= 4)
        return;
    if (m_perfPass == 0)
        m_perfReadTrace[index].append(QPointF(x, readMiBs));
    else
        m_perfWriteTrace[index].append(QPointF(x, writeMiBs));
}

void MainWindow::showPerfItemTrace(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= 4)
        return;

    const QVector<QPointF> &readTrace = m_perfReadTrace[itemIndex];
    const QVector<QPointF> &writeTrace = m_perfWriteTrace[itemIndex];
    if (readTrace.isEmpty() && writeTrace.isEmpty()) {
        QMessageBox::information(this, kAppName, kTraceNoData);
        return;
    }

    const FioConfig::PerfItem item = FioConfig::allPerfItems().value(itemIndex);

    // 直接在**右边的主图表**里画这一项的读写两条曲线，不另开窗口。
    // 两趟的横轴都是 0~10 秒，所以两条线天然叠在同一段时间上。
    m_chartStack->setCurrentIndex(1);
    m_chart->clear();
    m_chart->setTimeAxis(FioConfig::perfRuntimeSeconds());
    QColor readColor;
    QColor writeColor;
    m_chart->setDirections(true, true);
    perfItemColors(item, &readColor, &writeColor);

    const int count = qMax(readTrace.size(), writeTrace.size());
    for (int i = 0; i < count; ++i) {
        const bool hasRead = i < readTrace.size();
        const bool hasWrite = i < writeTrace.size();
        m_chart->appendSample(hasRead ? readTrace.at(i).x() : 0.0, hasRead,
                              hasRead ? readTrace.at(i).y() : 0.0,
                              hasWrite ? writeTrace.at(i).x() : 0.0, hasWrite,
                              hasWrite ? writeTrace.at(i).y() : 0.0);
    }

    // 颜色必须放在**画完点之后**：appendSample 的抽稀逻辑会重设画笔，
    // 放前面会被覆盖，结果画出来还是上一项（随机项）的黄绿。
    m_chart->setSeriesColors(readColor, writeColor);

    statusBar()->showMessage(kTraceShownTip.arg(FioConfig::perfItemName(item)));
}

void MainWindow::finishPerfRun()
{
    if (m_stopRequested)
        logEvent(kLogOneRoundDone);
    else
        logEvent(kLogPerfAllDone.arg(m_perfQueue.size()));

    // 需求：性能测试结束后默认删除测试文件（终止时上面已经删过，这里只在正常结束时删）
    if (!m_stopRequested)
        deletePerfFiles();

    finishWholeTest();
}

void MainWindow::finishWholeTest()
{
    setRunningState(false);
    m_phase = Phase::Idle;
    updateStateLabel();

    // 记下图表里的曲线是"在哪块盘、哪个模式"下跑出来的。
    // 切盘或切模式时靠这两个标记判断该不该继续显示（性能和可靠性互相独立）。
    if (!m_readSamples.isEmpty()) {
        m_chartOwnerRoot = m_activeConfig.targetRoot;
        m_chartOwnerMode = m_activeConfig.testMode;
    }

    // 状态栏落到真实状态，否则会一直挂着中途某一条旧消息（比如"准备中"）
    statusBar()->showMessage(m_stopRequested ? kStatusStoppedFinal : kStatusFinishedFinal);

    // 测试结束了就没有"实时速度"了：清掉最后残留的那个读数，
    // 只留平均值（平均读取 / 平均写入在阶段结束时已经定格）。
    m_readValueLabel->setText(QStringLiteral("--"));
    m_writeValueLabel->setText(QStringLiteral("--"));

    refreshDriveList();
    refreshTargetInfo();

    const bool anyBad = std::any_of(m_outcomes.cbegin(), m_outcomes.cend(),
                                    [](const PhaseOutcome &o) {
                                        if (!o.result.warnings.isEmpty())
                                            return true;
                                        // jobErrors 里存的是 fio 的 err= 数值，
                                        // "列表非空"不等于"有错"，err=0 是正常的。
                                        for (int err : o.result.jobErrors) {
                                            if (err != 0)
                                                return true;
                                        }
                                        return false;
                                    });

    QStringList summary;
    summary << kReportTestEnd;
    summary << kReportTestPartition.arg(QDir::toNativeSeparators(m_activeConfig.targetRoot));
    summary << kReportRwMode.arg(m_activeConfig.modeText());

    bool allClean = true;
    for (const PhaseOutcome &outcome : m_outcomes) {
        const FioResult &r = outcome.result;
        const RunStats &s = outcome.stats;
        summary << kPhaseHeadFmt.arg(m_totalRounds > 1
                                         ? kRoundTagFmt.arg(outcome.round)
                                               + jobKindText(r.kind)
                                         : jobKindText(r.kind));
        summary << kReportFioExitCode.arg(r.exitCode);
        summary << kReportSampleWindow
                       .arg(s.count).arg(r.elapsedSeconds, 0, 'f', 1);
        if (s.count > 0) {
            summary << kReportReadAvgMinMax
                           .arg(formatSpeed(s.readAvg), formatSpeed(s.readMin),
                                formatSpeed(s.readMax));
            summary << kReportWriteAvgMinMax
                           .arg(formatSpeed(s.writeAvg), formatSpeed(s.writeMin),
                                formatSpeed(s.writeMax));
        } else {
            summary << kReportNoSamples;
        }
        for (const QString &warning : r.warnings)
            summary << kReportWarning.arg(warning);
        for (int i = 0; i < r.jobErrors.size(); ++i)
            summary << QStringLiteral("  job %1 err = %2").arg(i + 1).arg(r.jobErrors.at(i));
        allClean = allClean && !r.cancelled && r.isClean();
    }

    const bool bad = anyBad;
    summary << (bad ? kReportVerdictBad
                    : (allClean ? kReportVerdictOk
                                : kReportVerdictStopped));
    summary << QStringLiteral("================================");

    for (const QString &line : summary)
        recordOutput(line);

    statusBar()->showMessage(summary.at(summary.size() - 2));

    if (m_selfTestMode && !m_selfTestSaveData.isEmpty()) {
        QString saveError;
        if (saveRunDataTo(m_selfTestSaveData, &saveError))
            recordOutput(kSelfTestDataSaved.arg(m_selfTestSaveData));
        else
            recordOutput(kSelfTestSaveFailed.arg(saveError));
    }

    if (m_selfTestMode)
        writeSelfTestReport();
}

void MainWindow::updateStateLabel()
{
    QString text;
    QString color;
    switch (m_phase) {
    case Phase::Preparing:
        text = kStatePreparing;
        color = QStringLiteral("#c87f0a");
        break;
    case Phase::Writing:
        text = kStateWriting;
        color = QStringLiteral("#1e46dc");
        break;
    case Phase::Reading:
        text = kStateReading;
        color = QStringLiteral("#dc1e1e");
        break;
    case Phase::Idle:
    default:
        text = kStateStopped;
        color = QStringLiteral("#3c3c3c");
        break;
    }

    m_stateLabel->setText(text);
    m_stateLabel->setStyleSheet(QStringLiteral("color:%1;").arg(color));

    if (m_phase != Phase::Idle && !m_observedStates.contains(text))
        m_observedStates.append(text);
}

void MainWindow::recordOutput(const QString &text)
{
    m_logLines.append(text);
    if (m_logLines.size() > 5000)
        m_logLines.remove(0, m_logLines.size() - 5000);
}

void MainWindow::logEvent(const QString &text, bool isProblem)
{
    // 左侧「运行日志」：时间戳 + 做了什么。出错用红色。
    const QString stamp = QDateTime::currentDateTime()
                              .toString(QStringLiteral("yyyy.MM.dd HH:mm:ss"));
    if (m_logView) {
        const QString color = isProblem ? QStringLiteral("#c81e1e")
                                        : QStringLiteral("#222222");
        m_logView->appendHtml(QStringLiteral("<span style='color:%1;'>%2&nbsp;&nbsp;%3</span>")
                                  .arg(color, stamp, text.toHtmlEscaped()));
        m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
    }
    // 同时进诊断输出，保存出来时也带着时间线
    recordOutput(QStringLiteral("[%1] %2").arg(stamp, text));
}

void MainWindow::reportProblem(const QString &title, const QString &text, bool critical)
{
    m_lastError = kTextTitleColon.arg(title, text);
    logEvent(kLogErrorPrefix.arg(m_lastError), true);
    recordOutput(kReportError.arg(m_lastError));
    if (m_selfTestMode)
        return;
    if (critical)
        QMessageBox::critical(this, title, text);
    else
        QMessageBox::warning(this, title, text);
}

// ---------------------------------------------------------------- 自检

bool MainWindow::verifyExclusiveGroups()
{
    // 回归断言：这两组单选都必须**任何时刻恰好选中一个**。
    // 之前只靠"同一个父窗口"这条隐式规则，结果出现过同时选中 / 都不选。
    const auto exactlyOne = [](QRadioButton *a, QRadioButton *b) {
        return ((a->isChecked() ? 1 : 0) + (b->isChecked() ? 1 : 0)) == 1;
    };

    bool ok = true;

    // 1) 来回切换，每一步都必须恰好一个
    const QList<QRadioButton *> styleProbes = {m_lineRadio, m_scatterRadio,
                                               m_lineRadio, m_scatterRadio, m_lineRadio};
    for (QRadioButton *button : styleProbes) {
        button->setChecked(true);
        ok = ok && exactlyOne(m_lineRadio, m_scatterRadio);
    }
    // 2) 试图把当前选中项取消掉：互斥组里应当无效，仍然恰好一个
    m_lineRadio->setChecked(false);
    m_scatterRadio->setChecked(false);
    ok = ok && exactlyOne(m_lineRadio, m_scatterRadio);

    const QList<QRadioButton *> densityProbes = {m_densityLowRadio, m_densityHighRadio,
                                                 m_densityLowRadio};
    for (QRadioButton *button : densityProbes) {
        button->setChecked(true);
        ok = ok && exactlyOne(m_densityHighRadio, m_densityLowRadio);
    }
    m_densityHighRadio->setChecked(false);
    m_densityLowRadio->setChecked(false);
    ok = ok && exactlyOne(m_densityHighRadio, m_densityLowRadio);

    // 复原到默认，别影响后面的测试
    m_lineRadio->setChecked(true);
    m_densityHighRadio->setChecked(true);
    onPlotStyleChanged();
    onDensityChanged();
    return ok;
}

void MainWindow::runSelfTest()
{
    m_selfTestMode = true;

    // 先做纯逻辑/控件断言，再按自检参数摆好界面
    m_exclusiveGuardOk = verifyExclusiveGroups();

    if (m_selfTestOutDir.isEmpty())
        m_selfTestOutDir = QDir::current().absoluteFilePath(QStringLiteral("_selftest"));
    QDir().mkpath(m_selfTestOutDir);

    if (!m_selfTestTarget.isEmpty()) {
        m_driveTable->setProperty("preferredRoot", m_selfTestTarget);
        refreshDriveList();
    }
    if (!m_selfTestModeKey.isEmpty()) {
        FioConfig cfg = configFromUi();
        cfg.mode = FioConfig::modeFromKey(m_selfTestModeKey);
        applyConfigToUi(cfg);
    }
    m_lineRadio->setChecked(!m_selfTestScatter);
    m_densityLowRadio->setChecked(m_selfTestLowDensity);
    // ---- 自检：所有参数用固定值，**不继承任何用户改动** ----
    // 自检要能从同一个已知状态出发，否则跑到别的分支上自己都不知道。
    // （以前这里是"不继承设置文件"；现在程序本来就不存设置了，
    //   这段的作用变成"不继承用户在本次会话里动过的参数"。）
    if (!m_selfTestReadOnly) {
        m_writeCheck->setChecked(true);
        m_readCheck->setChecked(true);
        m_verifyCheck->setChecked(true);
    }
    // 队列深度**沿用界面当前值（即默认值 8）**，不再硬写 16：
    // 硬写会让自检截图和报告里的参数跟真实默认值对不上，核对时反而误导。
    m_roundSpin->setValue(m_selfTestRounds > 0 ? m_selfTestRounds : 1);
    if (m_selfTestModeKey.isEmpty()) {
        const int seq = m_modeCombo->findData(
            FioConfig::modeKey(FioConfig::RwMode::SequentialWriteRead));
        if (seq >= 0)
            m_modeCombo->setCurrentIndex(seq);
    }
    for (int i = 0; i < 4; ++i) {
        if (m_perfChecks[i])
            m_perfChecks[i]->setChecked(true);
    }
    if (m_selfTestPerfMiB <= 0 && m_perfSizeCombo) {
        const int oneGiB = m_perfSizeCombo->findData(
            QVariant::fromValue(qint64(1024) * 1024 * 1024));
        if (oneGiB >= 0)
            m_perfSizeCombo->setCurrentIndex(oneGiB);
    }
    // 自检的模式必须是**显式**的：不带 --selftest-perf 就跑可靠性，
    // 否则会继承设置里上次用的模式，回归测试跑到别的分支上去。
    if (m_modePerfRadio && m_modeReliabilityRadio) {
        if (m_selfTestPerf)
            m_modePerfRadio->setChecked(true);
        else
            m_modeReliabilityRadio->setChecked(true);
    }
    if (m_selfTestPerf) {
        if (m_selfTestPerfMiB > 0) {
            const qint64 bytes = qint64(m_selfTestPerfMiB) * 1024 * 1024;
            const int s = m_perfSizeCombo->findData(QVariant::fromValue(bytes));
            if (s >= 0)
                m_perfSizeCombo->setCurrentIndex(s);
        }
    }
    m_scatterRadio->setChecked(m_selfTestScatter);

    // 加载模式：不跑测试，直接把 CSV 画进图表再出报告
    if (!m_selfTestLoadData.isEmpty()) {
        QString loadError;
        const bool ok = loadRunDataFrom(m_selfTestLoadData, &loadError);
        if (!ok)
            recordOutput(kSelfTestLoadFailed.arg(loadError));
        m_loadedDataFrom = ok ? m_selfTestLoadData : QString();
        m_loadedDataOk = ok;
        m_lastError = ok ? QString() : loadError;

        // 留一份解析结果：报告里的模式判别/四项取值都要靠它下断言。
        // 截图上看数字不算证据，只有这里出的 PASS/FAIL 才是可断言的。
        m_loadedRunDataValid = RunData::parse(m_selfTestLoadData, &m_loadedRunData, nullptr);

        // 同时把这份 CSV 用**结果窗口**打开一次并截图：自检那条路灌的是主窗口，
        // 而用户点菜单得到的是独立窗口，两者必须各自被验证过。
        ResultWindow *result = ResultWindow::open(nullptr, m_selfTestLoadData);
        m_resultWindowOk = (result != nullptr);
        if (result) {
            m_resultWindowItem = result->selectedItem();
            // 窗口刚 show() 出来还没布局完，下一拍再截
            QTimer::singleShot(150, result, [this, result]() {
                result->grab().save(
                    QDir(m_selfTestOutDir).absoluteFilePath(
                        QStringLiteral("result-window.png")), "PNG");
            });
        } else {
            recordOutput(kSelfTestResultWindowFailed);
        }

        QTimer::singleShot(400, this, &MainWindow::writeSelfTestReport);
        return;
    }

    if (m_selfTestReadOnly) {
        // 只读：先取消写入，再让"读取可选性"刷新一遍，最后勾上读取
        m_writeCheck->setChecked(false);
        updateReadAvailability();
        m_readCheck->setChecked(true);
    }

    refreshTargetInfo();
    updateReadAvailability();

    if (selectedRoot().isEmpty()) {
        m_lastError = kSelfTestTargetNotFound
                          .arg(m_selfTestTarget.isEmpty() ? kTextUnspecified
                                                          : m_selfTestTarget);
        recordOutput(kSelfTestPrefix + m_lastError);
        for (const QString &line : m_volumeSummary)
            recordOutput(kSelfTestEnumerated + line);
        logEvent(kLogErrorPrefix.arg(m_lastError), true);
        // 出报告再退出：早期中止如果什么都不留，自动化里根本看不出发生了什么
        writeSelfTestReport();
        return;
    }

    recordOutput(kSelfTestBegin);
    onStart();
    // onStart() 会把告警条清零，强制显示要放在它**之后**
    applyDebugVerifyBanner();
}

void MainWindow::applyDebugVerifyBanner()
{
    // 开发排查用：强制把校验失败告警条挂出来看样式。
    // 界面上必须明确标出这是示例，否则截图会被误当成"这块盘真的坏了"。
    if (!m_debugForceVerifyBanner)
        return;
    m_verifyFailures = 3;
    updateVerifyBanner();
    logEvent(kLogDebugBannerForced, true);
    recordOutput(kLogDebugBannerForced);
    setWindowTitle(windowTitle() + kDebugBannerTitleSuffix);
}

void MainWindow::writeSelfTestReport()
{
    const QString logPath = QDir(m_selfTestOutDir).absoluteFilePath(QStringLiteral("selftest.log"));
    const QString pngPath = QDir(m_selfTestOutDir).absoluteFilePath(QStringLiteral("chart.png"));
    const QString windowPngPath = QDir(m_selfTestOutDir).absoluteFilePath(QStringLiteral("window.png"));
    const QString rawPath = QDir(m_selfTestOutDir).absoluteFilePath(QStringLiteral("fio-output.log"));
    const QString csvPath = QDir(m_selfTestOutDir).absoluteFilePath(QStringLiteral("samples.csv"));

    const bool chartSaved = m_chart->viewWidget()->grab().save(pngPath, "PNG");
    const bool windowSaved = grab().save(windowPngPath, "PNG");

    bool csvSaved = false;
    QFile csvFile(csvPath);
    if (csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream csv(&csvFile);
        csv << "index,processed_GiB,duration_sec,read_MiBps,write_MiBps\n";
        for (int i = 0; i < m_readSamples.size(); ++i) {
            csv << i << ','
                << QString::number(m_sampleTimes.value(i), 'f', 3) << ','
                << QString::number(m_sampleDurations.value(i), 'f', 3) << ','
                << QString::number(m_readSamples.at(i), 'f', 2) << ','
                << QString::number(m_writeSamples.at(i), 'f', 2) << '\n';
        }
        csvFile.close();
        csvSaved = true;
    }

    bool rawSaved = false;
    QFile rawFile(rawPath);
    if (rawFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        rawFile.write(m_logLines.join(QLatin1Char('\n')).toUtf8());
        rawFile.write("\n");
        rawFile.close();
        rawSaved = true;
    }

    // ---- 纯逻辑断言 ----

    const bool rawDeviceGuard =
        FioConfig::looksLikeRawDevice(QStringLiteral("\\\\.\\PhysicalDrive0"))
        && FioConfig::looksLikeRawDevice(QStringLiteral("/dev/sda"))
        && !FioConfig::looksLikeRawDevice(QStringLiteral("G:/"))
        && !FioConfig::looksLikeRawDevice(QStringLiteral("D:/MyCode"));

    const bool engineGuard = FioConfig::engineRequiresBufferedIo(QStringLiteral("psync"))
                             && FioConfig::engineRequiresBufferedIo(QStringLiteral("sync"))
                             && !FioConfig::engineRequiresBufferedIo(QStringLiteral("windowsaio"));

    // 文件规划：每个文件都必须 ≤ 2 GiB，且总量不能超过目标
    bool planGuard = true;
    const struct { quint64 free; } probes[] = {
        {2ULL * 1024 * 1024 * 1024},            // 恰好 2 GiB → 1 个文件
        {5ULL * 1024 * 1024 * 1024},            // 5 GiB → 3 个文件
        {900ULL * 1024 * 1024 * 1024},          // 900 GiB → 450 个文件
        {300ULL * 1024 * 1024},                 // 300 MiB → 1 个小文件
        {1024ULL},                              // 几乎没有空间 → 无效计划
    };
    QStringList planLines;
    for (const auto &probe : probes) {
        FioConfig cfg = m_activeConfig;
        // 探针要测的是"按剩余空间规划"这个逻辑本身，
        // 所以必须先摘掉自检的填充上限，否则每个探针都会被截成同一个计划，等于没测。
        cfg.fillLimitMiB = 0;
        const quint64 total = probe.free;
        const FioConfig::FilePlan plan = cfg.planFiles(total, probe.free);
        if (plan.isValid()) {
            const bool ok = plan.fileSize <= FioConfig::maxFileSize()
                            && plan.totalBytes <= qint64(probe.free)
                            && plan.fileCount * plan.fileSize == plan.totalBytes;
            planGuard = planGuard && ok;
            planLines << kSelfTestPlanLine
                             .arg(formatBytes(probe.free))
                             .arg(plan.fileCount)
                             .arg(formatBytes(quint64(plan.fileSize)),
                                  formatBytes(quint64(plan.totalBytes)))
                             .arg(ok ? QStringLiteral("OK") : kSelfTestViolation);
        } else {
            // 空间不足时给出无效计划是正确行为
            planGuard = planGuard && (probe.free < 300ULL * 1024 * 1024);
            planLines << kSelfTestPlanInvalid.arg(formatBytes(probe.free));
        }
    }

    const QIcon windowIcon = QApplication::windowIcon();
    const bool iconGuard = !windowIcon.isNull() && !windowIcon.availableSizes().isEmpty();

    // 折线抽稀步长的分档必须精确（<128GiB → 3，128GiB~1TiB → 5，≥1TiB → 10）
    constexpr qint64 kGiB = 1024LL * 1024 * 1024;
    constexpr qint64 kTiB = 1024LL * kGiB;
    const struct { qint64 bytes; int want; QString note; } stepProbes[] = {
        {100LL * kGiB,        3,  QStringLiteral("100 GiB")},
        {127LL * kGiB,        3,  QStringLiteral("127 GiB")},
        {128LL * kGiB,        5,  kReportStepProbe128GiB},
        {256LL * kGiB,        5,  QStringLiteral("256 GiB")},
        {1023LL * kGiB,       5,  QStringLiteral("1023 GiB")},
        {1LL * kTiB,          10, kReportStepProbe1TiB},
        {4LL * kTiB,          10, QStringLiteral("4 TiB")},
    };
    bool stepGuard = true;
    QStringList stepLines;
    for (const auto &probe : stepProbes) {
        const int got = SpeedChart::autoStepForBytes(probe.bytes);
        const bool ok = (got == probe.want);
        stepGuard = stepGuard && ok;
        stepLines << kSelfTestStepLine
                         .arg(probe.note)
                         .arg(got)
                         .arg(ok ? QStringLiteral("OK") : kSelfTestViolation);
    }

    bool noProblems = true;
    bool cancelled = false;
    for (const PhaseOutcome &outcome : m_outcomes) {
        const FioResult &r = outcome.result;
        cancelled = cancelled || r.cancelled;
        noProblems = noProblems && r.warnings.isEmpty();
        for (int err : r.jobErrors)
            noProblems = noProblems && err == 0;
    }

    // 自检：性能模式跑完后自动点一次第一项（顺序读写 Q8T1）的格子，
    // 截图里就能看到"顺序项 = 红/蓝两条线"是否正确。
    if (m_activeConfig.testMode == FioConfig::TestMode::Performance
        && m_perfResultCount > 0 && !m_perfReadTrace[0].isEmpty()) {
        showPerfItemTrace(0);
    }

    // 只有写入阶段被要求跑时，测试文件才应该存在；
    // 加载历史数据时不检查磁盘上的文件，也没有阶段结果。
    const bool filesPresent = !scanTestFiles(m_activeConfig.testFolder()).isEmpty();
    const bool loading = !m_loadedDataFrom.isEmpty();
    // 性能模式跑完会自动删文件，不能要求它还在
    const bool fileGuard = loading || !m_activeConfig.doWrite
                           || m_activeConfig.testMode == FioConfig::TestMode::Performance
                           || filesPresent;
    const bool perfRun = m_activeConfig.testMode == FioConfig::TestMode::Performance;
    const bool dataGuard = loading
                               ? (m_loadedDataOk && m_chart->sampleCount() > 0)
                               : (perfRun ? m_perfResultCount > 0 : !m_outcomes.isEmpty());
    if (loading)
        noProblems = m_loadedDataOk;

    const bool pass = dataGuard && noProblems && rawDeviceGuard && engineGuard
                      && planGuard && iconGuard && fileGuard && stepGuard
                      && m_exclusiveGuardOk;

    // 报告先攒在一个 QString 里，最后再落盘 + 打印。
    //
    // **QTextStream 必须比它包着的 QString 先析构**：它只持有 text 的引用，
    // 析构时会 flush 一次，如果那时 text 已经没了就是访问已释放内存。
    // 原来写的是 `QString text; QTextStream out(&text); ...`，text 后声明、
    // 先析构，于是收尾阶段必然踩一次野指针——表现是进程退出码
    // 0xC0000409（FAST_FAIL_STACK_COOKIE_CHECK_FAILURE），
    // 而报告内容看起来完全正常，很容易被当成"程序自己退的"。
    // 用一对大括号把流的生命期框死，顺序就不再靠人记。
    QString text;
    {
        QTextStream out(&text);

        out << kReportTitle;
        out << kReportQtVersion << qVersion() << "\n";
        out << kReportQpaPlatform << QApplication::platformName() << "\n";
        out << kReportFioVersion
            << (m_fioVersionLabel->text().isEmpty() ? kTextUnknown : m_fioVersionLabel->text()) << "\n";
        out << kReportTargetPartition << m_activeConfig.targetRoot << "\n";
        out << kReportTestFolder << QDir::toNativeSeparators(m_activeConfig.testFolder()) << "\n";
        // 这一行写**界面当前的配置**，不写 m_activeConfig：
        // 自检在"还没开跑就中止"那条路径上（比如没选目标分区）也会出报告，
        // 那时 m_activeConfig 还是默认构造的空壳，写出来是一组假参数
        // （实测过：界面明明是 iodepth=8，报告却写 16）。
        // 真跑起来之后 configFromUi() 与 m_activeConfig 是同一份内容。
        out << kReportTestConfig << configFromUi().describe() << "\n";
        out << kReportRounds << m_totalRounds << "\n";
        if (!m_loadedDataFrom.isEmpty())
            out << kReportDataSource << QDir::toNativeSeparators(m_loadedDataFrom)
                << kReportNotRealtime;
        if (!m_lastError.isEmpty())
            out << kReportStartupError << m_lastError << "\n";
        if (!m_volumeSummary.isEmpty()) {
            out << kReportVolumeList << m_volumeSummary.size() << kReportSuffixCount;
            for (const QString &line : m_volumeSummary)
                out << "    " << line << "\n";
        }
        out << kReportPhaseList;
        QStringList kinds;
        if (m_activeConfig.doWrite) kinds << kPhaseNameWrite;
        if (m_activeConfig.doRead) kinds << kPhaseNameRead;
        out << kinds.join(kTextArrow) << "\n";
        if (m_selfTestFillLimit > 0)
            out << kReportFillLimit << m_selfTestFillLimit << " MiB\n";
        if (m_activePlan.isValid()) {
            out << kReportFilePlanHead << m_activePlan.fileCount << kReportSuffixTimes
                << formatBytes(quint64(m_activePlan.fileSize)) << " = "
                << formatBytes(quint64(m_activePlan.totalBytes)) << "\n";
        }
        out << kReportExistingFiles
            << scanTestFiles(m_activeConfig.testFolder()).count << kReportSuffixCount;
        for (const PhaseOutcome &outcome : m_outcomes) {
            const FioResult &r = outcome.result;
            const RunStats &s = outcome.stats;
            out << kPhaseHeadFmt.arg(m_totalRounds > 1
                                         ? kRoundTagFmt.arg(outcome.round) + jobKindText(r.kind)
                                         : jobKindText(r.kind)) << "\n";
            out << kReportPhaseExitCode << r.exitCode << (r.cancelled ? kReportCancelledTag : "") << "\n";
            out << kReportPhaseSamples << s.count << kReportWindowPrefix
                << QString::number(r.elapsedSeconds, 'f', 1) << kReportSuffixSeconds;
            out << kReportReadMinAvgMax
                       .arg(s.readMin, 0, 'f', 2).arg(s.readAvg, 0, 'f', 2).arg(s.readMax, 0, 'f', 2);
            out << kReportWriteMinAvgMax
                       .arg(s.writeMin, 0, 'f', 2).arg(s.writeAvg, 0, 'f', 2).arg(s.writeMax, 0, 'f', 2);
            out << kReportSummaryRead
                << (r.hasRead ? formatSpeed(r.readMiBs) + "  " + r.readDetail : kTextNone) << "\n";
            out << kReportSummaryWrite
                << (r.hasWrite ? formatSpeed(r.writeMiBs) + "  " + r.writeDetail : kTextNone) << "\n";
            out << "  job err           :";
            for (int err : r.jobErrors)
                out << " " << err;
            out << "\n";
            out << kReportPhaseWarnings << r.warnings.size() << "\n";
            for (const QString &warning : r.warnings)
                out << "    - " << warning << "\n";
            // 校验失败的块数是这块盘好坏的**结论性证据**，必须出现在报告里
            out << kReportVerifyCount << r.verifyFailures << "\n";
            if (!r.firstVerifyError.isEmpty())
                out << kReportVerifyFirst << r.firstVerifyError << "\n";
            // 逐处列出坏在哪。只报第一条会让人以为"只有那一处坏"，
            // 实际上一块坏盘的失败点分散在很多偏移量上——报告要能看出这一点。
            for (const QString &line : r.verifyErrorLines)
                out << kReportVerifyWhere << line << "\n";
            const int shown = r.verifyErrorLines.size();
            if (r.verifyFailures > shown)
                out << kReportVerifyTruncated.arg(shown).arg(r.verifyFailures) << "\n";
        }
        out << kReportPlotStyle
            << (m_chart->plotStyle() == SpeedChart::PlotStyle::Scatter ? kPlotScatter : kPlotLine) << "\n";
        out << kReportSamplePoints << m_chart->sampleCount()
            << kReportLineSeries << m_chart->linePointCount()
            << kReportScatterSeries << m_chart->scatterPointCount() << "\n";
        out << kReportSkippedFirst << m_skippedFirstSamples
            << kReportSkippedFirstNote;

        // 报告里要写**图表实际用的轴**，不能按 m_activeConfig 判：
        // 自检加载一份性能 CSV 时配置还是"可靠性"，照配置写就会出现
        // "横轴 0~10 秒"却标成 GiB 这种自相矛盾的行。
        if (m_chart->axisMode() == SpeedChart::AxisMode::Time) {
            out << kReportAxisTimeFmt.arg(QString::number(m_chart->axisMax(), 'f', 1))
                                       .arg(QString::number(m_chart->firstX(), 'f', 4))
                                       .arg(QString::number(m_chart->lastX(), 'f', 4));
        } else {
            out << kReportAxisHead << formatBytes(quint64(m_axisByteTotal))
                << kReportAxisMid << QString::number(m_chart->firstX(), 'f', 4)
                << kReportAxisLastX << QString::number(m_chart->lastX(), 'f', 4) << " GiB\n";
        }

        out << kReportCardCurrentRead << m_readValueLabel->text()
            << kReportCardCurrentWrite << m_writeValueLabel->text()
            << kReportCardAvgRead << m_avgReadLabel->text()
            << kReportCardAvgWrite << m_avgWriteLabel->text()
            << kReportCardElapsed << m_elapsedLabel->text() << "\n";
        out << kReportEndChecks.arg(m_writeCheck->isChecked() ? 1 : 0)
                               .arg(m_readCheck->isChecked() ? 1 : 0)
                               .arg(m_verifyCheck->isChecked() ? 1 : 0);
        out << kReportStateEnd << m_stateLabel->text()
            << kReportStateObserved
            << (m_observedStates.isEmpty() ? kTextNotObserved
                                           : m_observedStates.join(kTextArrow)) << "\n";
        out << kReportGuardRawDevice << (rawDeviceGuard ? "PASS" : "FAIL") << "\n";
        out << kReportGuardSyncEngine << (engineGuard ? "PASS" : "FAIL") << "\n";
        out << kReportGuardWindowIcon << (iconGuard ? "PASS" : "FAIL") << "\n";
        out << kReportGuardAutoStep << (stepGuard ? "PASS" : "FAIL") << "\n";
        for (const QString &line : stepLines)
            out << line << "\n";
        out << kReportGuardExclusive << (m_exclusiveGuardOk ? "PASS" : "FAIL")
            << kReportExclusiveNote;
        out << kReportGuardFilePlan << (planGuard ? "PASS" : "FAIL") << "\n";
        for (const QString &line : planLines)
            out << line << "\n";

        // ---- 启动默认值 ----
        // "每次启动都回到出厂默认值"这条规则必须能被断言，
        // 否则以后有人动一下界面顺序就可能悄悄漂回去。
        // 报告生成时界面可能已经被自检改过（模式、圈数、盘符），
        // 所以这里查的是**代码里那套常量**，而不是当前界面状态。
        {
            FioConfig def;
            def.testMode  = FioConfig::TestMode::Reliability;
            def.mode      = FioConfig::RwMode::SequentialWriteRead;
            def.iodepth   = 8;
            def.rounds    = 1;
            def.doWrite   = true;
            def.doRead    = true;
            def.verify    = true;
            def.perfDataBytes = qint64(1024) * 1024 * 1024;
            for (int i = 0; i < 4; ++i)
                def.perfItemChecked[i] = true;

            const bool defaultsOk =
                def.testMode == FioConfig::TestMode::Reliability
                && def.mode == FioConfig::RwMode::SequentialWriteRead
                && def.iodepth == 8
                && def.rounds == 1
                && def.doWrite && def.doRead && def.verify
                && def.perfDataBytes == qint64(1024) * 1024 * 1024
                && def.perfItemChecked[0] && def.perfItemChecked[1]
                && def.perfItemChecked[2] && def.perfItemChecked[3];
            out << kReportGuardDefaults << (defaultsOk ? "PASS" : "FAIL")
                << kReportDefaultsDetail.arg(def.iodepth)
                                         .arg(def.perfDataBytes / (1024 * 1024))
                << "\n";
        }

        // ---- fio 查找：只看 exe 目录，找不到就走 PATH，绝不扫盘 ----
        {
            const QString appDir = QCoreApplication::applicationDirPath();
            const QString bundled = bundledFioPathFor(appDir);
            // 自带的那份必须正好指向 <exe目录>\fio\fio.exe
            const QString expected =
                QDir::cleanPath(QDir(appDir).absoluteFilePath(QStringLiteral("fio/fio.exe")));
            const bool bundledOk = bundled.isEmpty() || bundled == expected;

            // 目录往上几层放的 fio **不许**被找到：喂一个临时的空目录进去，
            // 如果还有"往上翻"的逻辑，它就会找到本工程 third_party 里的那份。
            QTemporaryDir tempDir;
            const QString fromTemp = bundledFioPathFor(tempDir.path());
            const bool noScanOk = fromTemp.isEmpty();

            // 标签判定：只有 exe 目录里那份才算"自带"
            const bool labelOk = fioLooksBundled(expected)
                                 && !fioLooksBundled(
                                     QDir::cleanPath(appDir + QStringLiteral("/../fio/fio.exe")));

            out << kReportGuardFioLookup << ((bundledOk && noScanOk && labelOk) ? "PASS" : "FAIL")
                << kReportFioLookupDetail
                       .arg(locateFioExecutable().isEmpty() ? kTextNone
                                                            : locateFioExecutable())
                << "\n";
        }

        out << kReportFilesPresent << (fileGuard ? "PASS" : "FAIL") << "\n";

        if (loading) {
            out << kReportGuardLoadedData << (dataGuard ? "PASS" : "FAIL")
                << kTextParenOpen << m_chart->sampleCount() << kReportLoadedDataNote;
            // 用户实际走的是「文件 → 打开测试结果…」那条路（独立窗口），
            // 所以自检也要把这条路径跑一遍，不能只验证"灌进主窗口"。
            out << kReportGuardResultWindow << (m_resultWindowOk ? "PASS" : "FAIL")
                << kReportResultWindowItem.arg(m_resultWindowItem) << "\n";

            // 模式判别与性能四项取值：必须在报告里能断言，
            // 光靠截图肉眼看数字不算证据。
            if (m_loadedRunDataValid) {
                const bool perf = (m_loadedRunData.mode == RunData::Mode::Performance);
                const QVector<int> present = m_loadedRunData.perfItemsPresent();
                // 可靠性数据不应该被判成性能；性能数据必须能分出四项
                const bool modeOk = perf ? (present.size() == 4) : present.isEmpty();
                out << kReportLoadedModeGuard << (modeOk ? "PASS" : "FAIL")
                    << (perf ? kModeNamePerformance : kModeNameReliability)
                    << kReportLoadedItemsFmt.arg(present.size()) << "\n";

                if (perf && present.size() == 4) {
                    const QVector<RunData::Trace> traces = m_loadedRunData.perfTraces();
                    const QVector<FioConfig::PerfItem> allItems = FioConfig::allPerfItems();
                    QStringList values;
                    bool allPresent = true;
                    for (int i = 0; i < traces.size() && i < allItems.size(); ++i) {
                        values << kResultPerfItemFmt.arg(
                            FioConfig::perfItemName(allItems.at(i)),
                            formatSpeed(traces.at(i).readValue),
                            formatSpeed(traces.at(i).writeValue));
                        allPresent = allPresent && traces.at(i).hasRead() && traces.at(i).hasWrite()
                                     && traces.at(i).readValue >= 0.0
                                     && traces.at(i).writeValue >= 0.0;
                    }
                    out << kReportGuardPerfItems << (allPresent ? "PASS" : "FAIL") << "\n";
                    for (const QString &line : values)
                        out << "    " << line << "\n";
                }
            }
        }
        out << kReportGuardProblems << (noProblems ? kReportNoProblems : "FAIL") << "\n";
        out << kReportRawOutput << (rawSaved ? rawPath : kTextSaveFailed) << "\n";
        out << kReportCsvOutput << (csvSaved ? csvPath : kTextSaveFailed) << "\n";
        out << kReportChartPng << (chartSaved ? pngPath : kTextSaveFailed) << "\n";
        out << kReportWindowPng << (windowSaved ? windowPngPath : kTextSaveFailed) << "\n";
        out << kReportVerdict << (pass ? "PASS" : "FAIL")
            << (cancelled ? kReportStoppedTag : "") << "\n";
        out << "=====================================================================\n";
        out.flush();
    }   // ← QTextStream 到此结束，text 之后还能安全使用

    const QByteArray report = text.toUtf8();

    QFile file(logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        file.write(report);
        file.close();
    }

    fwrite(report.constData(), 1, size_t(report.size()), stdout);
    fflush(stdout);

    QTimer::singleShot(500, qApp, &QCoreApplication::quit);
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    // 不再保存任何设置：下次启动一律回到默认值（见 resetToDefaultSettings）
    QMainWindow::closeEvent(event);
}
