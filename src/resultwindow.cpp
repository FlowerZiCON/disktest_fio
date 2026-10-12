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
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

namespace {

/// 折线抽稀：结果窗口里的点比实时采集少得多，按点数量级选一档就够。
int stepForCount(int count)
{
    return count > 600 ? 5 : 3;
}

} // namespace

void ResultWindow::perfItemColors(int itemIndex, QColor *readColor, QColor *writeColor)
{
    // 顺序项红/蓝，随机项黄/绿。**只按测试项分**，不按"读取/写入"分——
    // 和主界面点结果格子得到的颜色是同一套规则。
    const FioConfig::PerfItem item = FioConfig::allPerfItems().value(
        itemIndex, FioConfig::PerfItem::SeqQ8T1);
    if (FioConfig::perfItemIsRandom(item)) {
        *readColor = QColor(0xf0, 0xc0, 0x00);
        *writeColor = QColor(0x2e, 0xaa, 0x4a);
    } else {
        *readColor = QColor(220, 30, 30);
        *writeColor = QColor(30, 70, 220);
    }
}

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
    // 没有坏块信息（0 处且无明细）就不显示——不能凭空报红。
    const bool hasVerifyInfo =
        m_data.verifyFailures() > 0 || !m_data.verifyFailuresText().isEmpty();

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
        auto *verifyLabel = new QLabel(this);
        verifyLabel->setText(kVerifyBannerHead.arg(m_data.verifyFailures())
                             + QLatin1Char('\n') + kVerifyBannerNote);
        verifyLabel->setWordWrap(true);
        verifyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        verifyLabel->setStyleSheet(
            QStringLiteral("color:#d32020; font-weight:bold; border:2px solid #d32020;"
                           " border-radius:4px; padding:6px; background:#fff2f0;"));
        root->addWidget(verifyLabel);
    }

    // ---- 图表 ----
    m_chart = new SpeedChart(this);
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
    // 放在前面会被后面的 clear()/appendSample 冲掉。
    for (double x : m_data.verifyAxisX()) {
        if (x >= 0.0)
            m_chart->addVerifyMarker(x);
    }
}

void ResultWindow::rebuildReliabilityChart()
{
    m_chart->clear();
    m_chart->setCapacityAxis(m_data.axisMaxGiB());
    m_chart->setLineStep(stepForCount(m_data.sampleCount()));

    const bool plotRead = std::any_of(m_data.flags.cbegin(), m_data.flags.cend(),
                                      [](quint8 f) { return (f & 1) != 0; });
    const bool plotWrite = std::any_of(m_data.flags.cbegin(), m_data.flags.cend(),
                                       [](quint8 f) { return (f & 2) != 0; });
    m_chart->setDirections(plotRead, plotWrite);

    for (int i = 0; i < m_data.sampleCount(); ++i) {
        const quint8 f = m_data.flags.value(i, 0);
        m_chart->appendSample(m_data.readX.value(i, 0.0), (f & 1) != 0,
                              m_data.readV.value(i, 0.0),
                              m_data.writeX.value(i, 0.0), (f & 2) != 0,
                              m_data.writeV.value(i, 0.0));
    }
    // 末点必须画出来：抽稀按"第几个点"算，末点不是 step 的整数倍时会被抽掉，
    // 曲线末端就够不到横轴右边缘（实测：最后一点缺失）。
    m_chart->finishSamples();
}

void ResultWindow::buildPerfTable(QVBoxLayout *root)
{
    auto *box = new QGroupBox(kGroupPerfItems.arg(FioConfig::perfRuntimeSeconds()), this);
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
    double scaleMax = 1.0;

    for (int i = 0; i < all.size(); ++i) {
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
    for (int i = 0; i < 4; ++i) {
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

    // 按值取：perfTraces() 返回的是临时对象，用 const& 接会立刻悬空。
    const RunData::Trace trace = m_traces.at(item);
    QColor readColor;
    QColor writeColor;
    perfItemColors(item, &readColor, &writeColor);

    m_chart->clear();
    m_chart->setTimeAxis(FioConfig::perfRuntimeSeconds());
    m_chart->setLineStep(stepForCount(qMax(trace.read.size(), trace.write.size())));
    m_chart->setDirections(!trace.read.isEmpty(), !trace.write.isEmpty());

    const int count = qMax(trace.read.size(), trace.write.size());
    for (int i = 0; i < count; ++i) {
        const bool hasRead = i < trace.read.size();
        const bool hasWrite = i < trace.write.size();
        m_chart->appendSample(hasRead ? trace.read.at(i).x() : 0.0, hasRead,
                              hasRead ? trace.read.at(i).y() : 0.0,
                              hasWrite ? trace.write.at(i).x() : 0.0, hasWrite,
                              hasWrite ? trace.write.at(i).y() : 0.0);
    }
    // 末点必须画出来：每一趟的末尾是 10.0 秒那个收尾补点，
    // 它落在第 21 个位置上、不是 step 的整数倍，光靠抽稀会被丢掉，
    // 曲线就停在 9.5 秒、右边缘空一格。
    m_chart->finishSamples();
    // 颜色必须放在**画完点之后**：appendSample 的抽稀逻辑会重设画笔，
    // 放前面会被覆盖，画出来还是默认的红蓝。
    m_chart->setSeriesColors(readColor, writeColor);

    // 只显示这一项：图表上不可能同时画四条线，所以固定"一次一项"，
    // 和主界面点结果格子看到的是同一张图。
    m_hintLabel->setText(kResultShowOneFmt.arg(FioConfig::perfItemName(all.at(item))));
}

void ResultWindow::rebuildPerfCombinedChart()
{
    m_selectedItem = -1;
    m_chart->clear();
    m_chart->setTimeAxis(FioConfig::perfRuntimeSeconds());
    m_chart->setLineStep(stepForCount(m_data.sampleCount()));
    m_chart->setDirections(true, true);
    for (int i = 0; i < m_data.sampleCount(); ++i) {
        const quint8 f = m_data.flags.value(i, 0);
        m_chart->appendSample(m_data.readX.value(i, 0.0), (f & 1) != 0,
                              m_data.readV.value(i, 0.0),
                              m_data.writeX.value(i, 0.0), (f & 2) != 0,
                              m_data.writeV.value(i, 0.0));
    }
    m_chart->finishSamples();   // 末点必须保留，理由同上
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
