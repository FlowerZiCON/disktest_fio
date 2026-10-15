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

#include "resultwindow.h"

#include "fioconfig.h"
#include "perfcell.h"
#include "speedchart.h"
#include "uitext.h"

#include <QButtonGroup>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

namespace {

/// 结果表里的格子数必须正好等于性能测试项的数量：
/// 填格子的循环按 allPerfItems() 走，数组长度却是编译期固定的，
/// 两边不一致就是越界写。
constexpr int kExpectedPerfCells = 4;

} // namespace

ResultWindow::ResultWindow(const RunData &data, QWidget *parent)
    : QDialog(parent)
    , m_data(data)
    , m_traces(data.perfTraces())
{
    const bool perf = (m_data.mode == RunData::Mode::Performance);
    setWindowTitle(kResultWindowTitleFmt.arg(QFileInfo(m_data.path).fileName(),
                                             perf ? kModeNamePerformance
                                                  : kModeNameReliability));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1000, 720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // ---- 顶部：这是哪个模式、哪份文件 ----
    auto *modeLabel = new QLabel(
        kResultModeLabelFmt.arg(perf ? kModeNamePerformance : kModeNameReliability), this);
    QFont modeFont = modeLabel->font();
    modeFont.setBold(true);
    modeFont.setPointSizeF(modeFont.pointSizeF() + 1.5);
    modeLabel->setFont(modeFont);
    root->addWidget(modeLabel);

    // 老文件没有 test_mode：明确说一句，免得用户以为程序读错了模式
    if (!m_data.modeExplicit) {
        auto *legacy = new QLabel(kResultLegacyModeNote, this);
        legacy->setStyleSheet(QStringLiteral("color:#8a6d00;"));
        root->addWidget(legacy);
    }

    auto *sourceLabel = new QLabel(
        kResultSourceFmt.arg(QDir::toNativeSeparators(
            QFileInfo(m_data.path).absoluteFilePath())), this);
    sourceLabel->setStyleSheet(QStringLiteral("color:#666;"));
    sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(sourceLabel);

    // ---- 摘要 ----
    auto *infoBox = new QGroupBox(this);
    auto *info = new QGridLayout(infoBox);
    info->setContentsMargins(8, 6, 8, 6);
    info->setHorizontalSpacing(12);
    info->setVerticalSpacing(2);
    root->addWidget(infoBox);

    // 摘要按**行**组织，每行两对"标题 + 值"。
    //
    // 两种模式关心的东西不一样，所以行内容分开写，不做"拼一个列表再每两个一行"
    // 那种通用写法——那样想调一行就得把整串数一遍，很容易数错。
    QVector<QVector<QPair<QString, QString>>> rowsBacking;

    if (perf) {
        // 性能：一行是"哪块盘 + 两个均速"，一行是"测试数据 + 已用时"。
        //
        // 读写模式/块大小/采样点数不在这里显示：性能测试的读写模式由测试项决定
        // （顺序项就是顺序、随机项就是随机），写一个笼统的"顺序读写"反而误导；
        // 块大小同理（4M 与 4K 混在一起）；采样点数属于诊断信息，不是结果。
        // 均速带"顺序"二字，因为它统计的是整轮采样、而不是某一项。
        const double bytes = m_data.paramDouble(QStringLiteral("perf_data_bytes"), 0.0);
        QVector<QPair<QString, QString>> row1;
        row1 << qMakePair(kResultSummaryTarget, m_data.param(QStringLiteral("target")))
             << qMakePair(kResultSummarySeqWrite,
                          RunData::formatSpeed(m_data.averageWriteMiBps()))
             << qMakePair(kResultSummarySeqRead,
                          RunData::formatSpeed(m_data.averageReadMiBps()));
        QVector<QPair<QString, QString>> row2;
        row2 << qMakePair(kResultSummaryData,
                          bytes > 0.0 ? RunData::formatBytes(qint64(bytes)) : QString())
             << qMakePair(kResultSummaryElapsed,
                          RunData::formatDuration(qint64(m_data.elapsedSeconds())));
        rowsBacking << row1 << row2;
    } else {
        // 可靠性：盘和被测参数在前，均速、已用时在后。
        const int rounds = int(m_data.paramDouble(QStringLiteral("rounds"), 1.0));
        const int fileCount = int(m_data.paramDouble(QStringLiteral("file_count"), 0.0));
        const double fileSize = m_data.paramDouble(QStringLiteral("file_size_bytes"), 0.0);
        const double planned = m_data.paramDouble(QStringLiteral("planned_bytes"), 0.0);

        QVector<QPair<QString, QString>> row1;
        row1 << qMakePair(kResultSummaryTarget, m_data.param(QStringLiteral("target")))
             << qMakePair(kResultSummaryMode, m_data.param(QStringLiteral("mode")));
        QVector<QPair<QString, QString>> row2;
        row2 << qMakePair(kResultSummaryBlock, m_data.param(QStringLiteral("block_size")))
             << qMakePair(kResultSummarySamples, QString::number(m_data.sampleCount()));
        QVector<QPair<QString, QString>> row3;
        row3 << qMakePair(kResultSummaryRounds, QString::number(qMax(1, rounds)));
        if (fileCount > 0 && fileSize > 0.0) {
            row3 << qMakePair(kResultSummaryPlan,
                              QStringLiteral("%1 × %2 = %3")
                                  .arg(fileCount)
                                  .arg(RunData::formatBytes(qint64(fileSize)))
                                  .arg(RunData::formatBytes(qint64(planned))));
        }
        QVector<QPair<QString, QString>> row4;
        row4 << qMakePair(kResultSummaryAvgWrite,
                          RunData::formatSpeed(m_data.averageWriteMiBps()))
             << qMakePair(kResultSummaryAvgRead,
                          RunData::formatSpeed(m_data.averageReadMiBps()));
        QVector<QPair<QString, QString>> row5;
        row5 << qMakePair(kResultSummaryElapsed,
                          RunData::formatDuration(qint64(m_data.elapsedSeconds())));
        rowsBacking << row1 << row2 << row3 << row4 << row5;
    }

    // 坏块信息：这是"这块盘到底坏没坏"的结论，比任何速度数字都重要，
    // 所以紧跟摘要、并且单独用红字，而不是混在速度那几项里。
    //
    // 三种情况要分开：
    //   * 记了 N > 0 处          → 报红，结论是"盘坏了"
    //   * 记了 0 处、也没有明细   → 不显示（不能凭空报红）
    //   * 计数这一项读不出来      → **也要报**，但说的是"读不出来"而不是"0 处"。
    //     以前这一种会落到 0，也就是把一个损坏的字段当成了"没有坏块"。
    const bool verifyKnown = m_data.verifyFailuresKnown();
    const int verifyCount = m_data.verifyFailures();
    const bool hasVerifyInfo = !verifyKnown || verifyCount > 0
                               || !m_data.verifyFailuresText().isEmpty();

    for (int row = 0; row < rowsBacking.size(); ++row) {
        const QVector<QPair<QString, QString>> &rowEntries = rowsBacking.at(row);
        for (int k = 0; k < rowEntries.size(); ++k) {
            const int column = k * 2;
            auto *titleLabel = new QLabel(rowEntries.at(k).first, infoBox);
            titleLabel->setStyleSheet(QStringLiteral("color:#666;"));
            auto *valueLabel = new QLabel(rowEntries.at(k).second.isEmpty()
                                              ? kTextNone
                                              : rowEntries.at(k).second,
                                          infoBox);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            info->addWidget(titleLabel, row, column);
            info->addWidget(valueLabel, row, column + 1);
            info->setColumnStretch(column + 1, 1);
        }
    }

    // ---- 坏块结论：和实时图表上那条红框同款 ----
    if (hasVerifyInfo) {
        // 提示语**和实时那条一字不差**（同一个 kVerifyBannerHead + kVerifyBannerNote）：
        // 同一件事在两个地方用两套措辞，用户会以为是两回事。
        // 这里不再列"首处偏移量"——运行日志里有逐处明细，图上还有黄色色带定位。
        QString verifyText;
        QString verifyNote;
        if (verifyKnown) {
            verifyText = kVerifyBannerHead.arg(verifyCount);
            verifyNote = kVerifyBannerNote;
            // 告警说 N 处、文件里却只有几条明细时说明一句，否则会让人
            // 以为"就只有这几处坏"（图上能画的色带也只到明细那么多）。
            const int details = m_data.verifyFailuresText().size();
            if (verifyCount > details && details > 0)
                verifyNote += QLatin1Char('\n') + kResultVerifyDetailsShort.arg(details).arg(verifyCount);
        } else {
            verifyText = kVerifyCountUnreadable;
            verifyNote = kVerifyCountUnreadableNote;
        }
        auto *verifyLabel = new QLabel(this);
        verifyLabel->setText(verifyText + QLatin1Char('\n') + verifyNote);
        verifyLabel->setWordWrap(true);
        verifyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        verifyLabel->setStyleSheet(
            QStringLiteral("color:#d32020; font-weight:bold; border:2px solid #d32020;"
                           " border-radius:4px; padding:6px; background:#fff2f0;"));
        root->addWidget(verifyLabel);
    }

    // 解析时跳过的采样行：这份结果**不完整**，必须说出来 ——
    // 静默少画几个点，用户会拿一条缺口的曲线当成真实曲线。
    if (m_data.droppedSampleCount() > 0) {
        auto *skipped = new QLabel(kStatusSkippedLines.arg(m_data.droppedSampleCount()), this);
        skipped->setStyleSheet(QStringLiteral("color:#8a6d00;"));
        skipped->setWordWrap(true);
        root->addWidget(skipped);
    }

    // ---- 图表 ----
    // 顺序要紧：buildChartStyleRow() 里要把初始值写进 m_chart，
    // 所以必须**先建图表**再建那一行；但加进布局的顺序反过来，
    // 那一行才会显示在图表上方。
    m_chart = new SpeedChart(this);
    buildChartStyleRow(root);
    root->addWidget(m_chart, 1);

    if (perf)
        buildPerfTable(root);

    m_hintLabel = new QLabel(perf ? kResultPerfHint : kResultReliabilityHint, this);
    m_hintLabel->setStyleSheet(QStringLiteral("color:#666;"));
    m_hintLabel->setWordWrap(true);
    root->addWidget(m_hintLabel);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *closeButton = new QPushButton(kBtnClose, this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    // 数据全部就位后再画：appendSample 的抽稀会重设画笔，
    // 颜色必须放在画完之后，否则画出来还是默认色。
    if (perf) {
        const QVector<int> present = m_data.perfItemsPresent();
        if (present.isEmpty()) {
            // 老文件没有分项信息：只能画一条合起来的读/写曲线
            rebuildPerfCombinedChart();
        } else {
            rebuildPerfChart(present.first());
        }
    } else {
        rebuildReliabilityChart();
    }

    // 坏块位置用黄色背景色带标出来，和主界面实时图表里的画法一致。
    // 横坐标在解析时就算好了（要用到文件序号 + 单文件大小，
    // 光有 fio 报的"文件内偏移"定位不到盘上的位置）。
    // 必须放在画完曲线**之后**：addVerifyMarker 会触发一次重绘，
    // 放在前面会被上面的 clear() 冲掉（clear 会清空标记列表）。
    for (double x : m_data.verifyAxisX()) {
        if (x >= 0.0)
            m_chart->addVerifyMarker(x);
    }
}

void ResultWindow::rebuildReliabilityChart()
{
    // 渲染只有一份实现（SpeedChart::plotRunData）：主窗口加载历史数据走的是
    // 同一个函数，同一份文件在两个窗口里画出来必然一样。
    // 密度用上面那组单选控件的当前值（它初始化时已经取自文件）。
    SpeedChart::plotRunData(m_data, m_chart,
                            m_densityLowRadio->isChecked() ? SpeedChart::Density::Low
                                                           : SpeedChart::Density::High);
}

void ResultWindow::buildChartStyleRow(QVBoxLayout *root)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    auto *styleLabel = new QLabel(kLabelPlotStyle, this);
    m_lineRadio = new QRadioButton(kPlotLine, this);
    m_scatterRadio = new QRadioButton(kPlotScatter, this);
    // 显式互斥组：不依赖"同一个父窗口"这条隐式规则
    auto *styleGroup = new QButtonGroup(this);
    styleGroup->setExclusive(true);
    styleGroup->addButton(m_lineRadio);
    styleGroup->addButton(m_scatterRadio);

    auto *densityLabel = new QLabel(kLabelDensity, this);
    m_densityHighRadio = new QRadioButton(kDensityHigh, this);
    m_densityLowRadio = new QRadioButton(kDensityLow, this);
    m_densityHighRadio->setToolTip(kDensityHighTip);
    m_densityLowRadio->setToolTip(kDensityLowTip);
    auto *densityGroup = new QButtonGroup(this);
    densityGroup->setExclusive(true);
    densityGroup->addButton(m_densityHighRadio);
    densityGroup->addButton(m_densityLowRadio);

    // 初始值：绘图方式取自文件（老文件没记就是折线图）；
    // 密度取自文件，老文件只有 line_step 时按"当年抽过稀 → 低密度"。
    const QString style = m_data.param(QStringLiteral("plot_style"));
    (style == QLatin1String("scatter") ? m_scatterRadio : m_lineRadio)->setChecked(true);
    (m_data.prefersLowDensity() ? m_densityLowRadio : m_densityHighRadio)->setChecked(true);
    m_chart->setPlotStyle(style == QLatin1String("scatter") ? SpeedChart::PlotStyle::Scatter
                                                            : SpeedChart::PlotStyle::Line);
    m_chart->setDensity(m_data.prefersLowDensity() ? SpeedChart::Density::Low
                                                   : SpeedChart::Density::High);

    const auto applyStyle = [this]() {
        m_chart->setPlotStyle(m_lineRadio->isChecked() ? SpeedChart::PlotStyle::Line
                                                       : SpeedChart::PlotStyle::Scatter);
    };
    const auto applyDensity = [this]() {
        m_chart->setDensity(m_densityLowRadio->isChecked() ? SpeedChart::Density::Low
                                                           : SpeedChart::Density::High);
    };
    connect(m_lineRadio, &QRadioButton::toggled, this, applyStyle);
    connect(m_scatterRadio, &QRadioButton::toggled, this, applyStyle);
    connect(m_densityHighRadio, &QRadioButton::toggled, this, applyDensity);
    connect(m_densityLowRadio, &QRadioButton::toggled, this, applyDensity);

    row->addWidget(styleLabel);
    row->addWidget(m_lineRadio);
    row->addWidget(m_scatterRadio);
    row->addSpacing(12);
    row->addWidget(densityLabel);
    row->addWidget(m_densityHighRadio);
    row->addWidget(m_densityLowRadio);
    row->addStretch(1);
    root->addLayout(row);
}

void ResultWindow::buildPerfTable(QVBoxLayout *root)
{
    // 这个循环按 allPerfItems() 的长度填 m_readCells / m_writeCells 这两个
    // 固定长度数组：项数一旦不是 4 就是数组外写。编译期先钉住，
    // 运行时再用 qMin 夹一次（allPerfItems() 不是 constexpr，钉不住它本身）。
    static_assert(kExpectedPerfCells == ResultWindow::kPerfCellCount,
                  "性能测试项数量变了：m_readCells/m_writeCells 的长度要跟着改");

    auto *box = new QGroupBox(kGroupPerfItems.arg(m_data.perfRuntimeSeconds()), this);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 2);
    grid->setColumnStretch(2, 2);

    auto *headRead = new QLabel(kPerfColRead, box);
    auto *headWrite = new QLabel(kPerfColWrite, box);
    for (QLabel *label : {headRead, headWrite}) {
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->setAlignment(Qt::AlignCenter);
    }
    grid->addWidget(headRead, 0, 1);
    grid->addWidget(headWrite, 0, 2);

    const QVector<RunData::Trace> &traces = m_traces;
    const QVector<FioConfig::PerfItem> all = FioConfig::allPerfItems();
    // 两个循环都走同一个上界：格子数（= 数组长度），再夹住轨迹条数，
    // 这样"填格子"和"设满量程"永远一致，也不会越界。
    const int cells = qMin(kPerfCellCount, qMin(all.size(), traces.size()));
    double scaleMax = 1.0;

    for (int i = 0; i < cells; ++i) {
        auto *name = new QLabel(FioConfig::perfItemName(all.at(i)), box);
        grid->addWidget(name, i + 1, 0);

        m_readCells[i] = new PerfCell(box);
        m_writeCells[i] = new PerfCell(box);
        grid->addWidget(m_readCells[i], i + 1, 1);
        grid->addWidget(m_writeCells[i], i + 1, 2);

        const RunData::Trace &trace = traces.at(i);
        const bool hasAny = trace.hasRead() || trace.hasWrite();
        m_readCells[i]->setValue(trace.hasRead() ? trace.readValue : -1.0);
        m_writeCells[i]->setValue(trace.hasWrite() ? trace.writeValue : -1.0);
        // 有轨迹才可点：点一下就在图表里单独看这一项
        m_readCells[i]->setClickable(hasAny);
        m_writeCells[i]->setClickable(hasAny);
        scaleMax = qMax(scaleMax, m_readCells[i]->value());
        scaleMax = qMax(scaleMax, m_writeCells[i]->value());

        connect(m_readCells[i], &PerfCell::activated, this, [this, i]() {
            rebuildPerfChart(i);
        });
        connect(m_writeCells[i], &PerfCell::activated, this, [this, i]() {
            rebuildPerfChart(i);
        });
    }

    // 所有格子共用一个满量程，条长才有可比性
    for (int i = 0; i < kPerfCellCount; ++i) {
        if (m_readCells[i])
            m_readCells[i]->setScaleMax(scaleMax);
        if (m_writeCells[i])
            m_writeCells[i]->setScaleMax(scaleMax);
    }

    root->addWidget(box);
}

void ResultWindow::rebuildPerfChart(int item)
{
    m_selectedItem = item;
    const QVector<FioConfig::PerfItem> all = FioConfig::allPerfItems();
    if (item < 0 || item >= all.size())
        return;

    // 渲染只有一份实现（SpeedChart::plotPerfItem）：主界面点结果格子走的是
    // 同一个函数，两处画出来必然一样。
    // 每项跑多少秒跟着**文件里记的**走，不是写死 10 秒：一份每项 30 秒的结果
    // 按 10 秒画，后 20 秒的曲线会被轴裁掉、看着像没测。
    // 密度**跟随用户选的那一档**，免得图上按高密度画、上面的单选却停在低密度。
    SpeedChart::plotPerfItem(m_data, m_chart, item,
                             m_densityLowRadio->isChecked() ? SpeedChart::Density::Low
                                                            : SpeedChart::Density::High);

    // 只显示这一项：图表上不可能同时画四条线，所以固定"一次一项"，
    // 和主界面点结果格子看到的是同一张图。
    m_hintLabel->setText(kResultShowOneFmt.arg(FioConfig::perfItemName(all.at(item))));
}

void ResultWindow::rebuildPerfCombinedChart()
{
    // 老文件没有分项信息：只能把整份数据画成一条合起来的读/写曲线。
    // 同样是 SpeedChart::plotRunData（它按数据模式自动用时间轴）。
    m_selectedItem = -1;
    SpeedChart::plotRunData(m_data, m_chart,
                            m_densityLowRadio->isChecked() ? SpeedChart::Density::Low
                                                           : SpeedChart::Density::High);
    m_hintLabel->setText(kResultPerfNoItemNote);
}

ResultWindow *ResultWindow::open(QWidget *parent, const QString &path)
{
    RunData data;
    QString error;
    if (!RunData::parse(path, &data, &error)) {
        if (parent)
            QMessageBox::warning(parent, kErrLoadDataFailed, error);
        return nullptr;
    }

    auto *window = new ResultWindow(data, parent);
    window->show();
    window->raise();
    window->activateWindow();
    return window;
}
