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

#include "speedchart.h"

#include "uitext.h"

#include <QChart>
#include <QChartView>
#include <QLegend>
#include <QLineSeries>
#include <QMargins>
#include <QPainter>
#include <QPen>
#include <QScatterSeries>
#include <QVBoxLayout>
#include <QValueAxis>

#include <algorithm>

namespace {

constexpr double kEpsilon = 1e-6;

const QColor kReadColor(220, 30, 30);
const QColor kWriteColor(30, 70, 220);
/// 校验失败位置：黄色，半透明。
/// 半透明是必须的——它是铺在背景上的一层色带，曲线和数据点要能透出来，
/// 不能把它们盖住。
const QColor kVerifyBandColor(0xf5, 0xc0, 0x00, 90);
/// 一条色带的宽度（像素）。3 像素够看清是一条带，又不会粗到糊成一片。
constexpr double kVerifyBandWidth = 3.0;
/// 两条色带之间至少留这么多像素，否则算同一条。
constexpr double kVerifyBandGap = 2.0;

bool nearlyEqual(double a, double b)
{
    return qAbs(a - b) < kEpsilon;
}

} // namespace

VerifyBandChartView::VerifyBandChartView(QChart *chart, QWidget *parent)
    : QChartView(chart, parent)
{
}

void VerifyBandChartView::setBands(const QVector<double> &xs, double yMin, double yMax)
{
    m_xs = xs;
    m_yMin = yMin;
    m_yMax = yMax;
    viewport()->update();
}

void VerifyBandChartView::paintEvent(QPaintEvent *event)
{
    // 先让基类把图表（坐标轴、曲线、数据点）画完。
    //
    // 为什么不能在它**之前**画：QGraphicsView::paintEvent 会先把自己的背景
    // （视口底色）刷一遍，先画的色带会被这次填充整个擦掉——实测画了但看不见。
    //
    // 那会不会盖住数据点？不会：色带是**半透明**的（alpha 约 90/255），
    // 曲线和数据点透过它照常看得清，这也是它非半透明不可的原因。
    QChartView::paintEvent(event);

    if (m_xs.isEmpty() || !chart())
        return;

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kVerifyBandColor);

    // 上下端点直接由**纵轴的上下限**映射过来：色带正好从坐标区底边拉到顶边，
    // 纵轴怎么缩放都不会画到坐标区外面。
    const QRectF area = chart()->plotArea();
    double yTop = area.top();
    double yBottom = area.bottom();
    if (m_yMax > m_yMin) {
        const double a = chart()->mapToPosition(QPointF(m_xs.first(), m_yMax)).y();
        const double b = chart()->mapToPosition(QPointF(m_xs.first(), m_yMin)).y();
        if (qAbs(b - a) > 1.0) {
            yTop = qMin(a, b);
            yBottom = qMax(a, b);
        }
    }

    // 画之前按**像素**收一遍：挨得太近的色带在屏幕上是同一条，
    // 多画几遍只会让那一列越来越实、最后糊成一片把曲线压住。
    //
    // 去重放在这里（渲染时）而不是 addVerifyMarker 里（收数据时），是因为
    // 像素位置要用到**最终的**横轴范围，而实时测试过程中横轴范围是会变的：
    //   实时那条路：轴范围还是初始值时就算过一次 → 容差偏大 → 合并过头
    //   加载那条路：轴范围已经是从文件里读出来的最终值 → 容差正常
    // 同一个位置在两条路上画出来不一样（实测：实时图比回看图少一大截色带）。
    // 移到渲染时算，两条路就必然一致。
    double lastPx = -1e9;
    for (double x : m_xs) {
        const double px = chart()->mapToPosition(QPointF(x, m_yMin)).x();
        if (px - lastPx < kVerifyBandWidth + kVerifyBandGap)
            continue;   // 和上一条挤在一起，跳过
        lastPx = px;
        painter.drawRect(QRectF(px - kVerifyBandWidth / 2.0, yTop,
                                kVerifyBandWidth, yBottom - yTop));
    }
}

SpeedChart::SpeedChart(QWidget *parent)
    : QWidget(parent)
{
    const QString readName = kSeriesRead;
    const QString writeName = kSeriesWrite;

    m_readLine = new QLineSeries(this);
    m_readLine->setName(readName);
    QPen readPen(kReadColor);
    readPen.setWidth(2);
    m_readLine->setPen(readPen);

    m_writeLine = new QLineSeries(this);
    m_writeLine->setName(writeName);
    QPen writePen(kWriteColor);
    writePen.setWidth(2);
    m_writeLine->setPen(writePen);

    m_readScatter = new QScatterSeries(this);
    m_readScatter->setName(readName);
    m_readScatter->setMarkerSize(5.0);
    m_readScatter->setColor(kReadColor);
    m_readScatter->setBorderColor(kReadColor);

    m_writeScatter = new QScatterSeries(this);
    m_writeScatter->setName(writeName);
    m_writeScatter->setMarkerSize(5.0);
    m_writeScatter->setColor(kWriteColor);
    m_writeScatter->setBorderColor(kWriteColor);

    // 校验失败的位置标记（黄色背景色带）**不走序列**：
    // 它由 VerifyBandChartView 在图表画完之后直接刷在视口上，
    // 这样既能保证在曲线之下（不遮数据点），又不受序列数量限制。
    // 坐标存在 m_markerXs 里，见 addVerifyMarker / refreshVerifyMarkers。

    m_chart = new QChart;
    m_chart->addSeries(m_readLine);
    m_chart->addSeries(m_readScatter);
    m_chart->addSeries(m_writeLine);
    m_chart->addSeries(m_writeScatter);
    // 实时刷新的图表必须关掉动画，否则每加一个点都会重播动画，又卡又难看
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->setBackgroundRoundness(0);
    m_chart->setMargins(QMargins(8, 8, 8, 8));
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    // 横轴 = 已处理容量，单位 GB，开测前固定
    m_axisX = new QValueAxis;
    m_axisX->setTitleText(kAxisCapacity);
    m_axisX->setLabelFormat(QStringLiteral("%.1f"));
    m_axisX->setRange(0, 1);
    m_axisX->setTickCount(6);

    m_axisY = new QValueAxis;
    m_axisY->setTitleText(kAxisSpeed);
    m_axisY->setLabelFormat(QStringLiteral("%.0f"));
    m_axisY->setRange(0, 100);
    m_axisY->setTickCount(6);

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    for (QXYSeries *series : {static_cast<QXYSeries *>(m_readLine),
                              static_cast<QXYSeries *>(m_readScatter),
                              static_cast<QXYSeries *>(m_writeLine),
                              static_cast<QXYSeries *>(m_writeScatter)}) {
        series->attachAxis(m_axisX);
        series->attachAxis(m_axisY);
    }

    // 标记色带要**贯穿整个坐标区**（从纵轴底到顶），所以坐标区一变就得重画。
    connect(m_chart, &QChart::plotAreaChanged, this,
            [this](const QRectF &) { refreshVerifyMarkers(); });

    m_view = new VerifyBandChartView(m_chart);
    m_view->setRenderHint(QPainter::Antialiasing, true);
    m_view->setMinimumHeight(280);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    // setVisible(false) 会同时把图例里的那一项去掉
    m_readLine->setVisible(m_showRead);
    m_readScatter->setVisible(false);
    m_writeLine->setVisible(m_showWrite);
    m_writeScatter->setVisible(false);
}

QWidget *SpeedChart::viewWidget() const
{
    return m_view;
}

int SpeedChart::linePointCount() const
{
    return m_readLine->count() + m_writeLine->count();
}

int SpeedChart::scatterPointCount() const
{
    return m_readScatter->count() + m_writeScatter->count();
}

double SpeedChart::firstX() const
{
    // 两个方向都要看：写入阶段只有写序列有点，只查读序列会得出错误的"首点"。
    bool has = false;
    double first = 0.0;
    for (const Sample &sample : m_history) {
        if (sample.hasRead) {
            first = has ? qMin(first, sample.readX) : sample.readX;
            has = true;
        }
        if (sample.hasWrite) {
            first = has ? qMin(first, sample.writeX) : sample.writeX;
            has = true;
        }
    }
    return first;
}

void SpeedChart::clear()
{
    m_history.clear();
    m_readLine->clear();
    m_readScatter->clear();
    m_writeLine->clear();
    m_writeScatter->clear();
    clearVerifyMarkers();
    m_readSeen = 0;
    m_writeSeen = 0;
    m_maxY = 1.0;
    m_lastX = 0.0;
    m_axisY->setRange(0, 100);
}

void SpeedChart::addVerifyMarker(double x)
{
    // 只收数据，不做去重：像素级的合并留到渲染时按**最终的**横轴范围算
    // （见 VerifyBandChartView::paintEvent）。这里若按当前轴范围去重，
    // 实时测试时轴范围还没定下来，会合并过头，和回看图对不上。
    if (m_markerXs.size() >= kMaxVerifyMarkers)
        return;
    m_markerXs << x;
    refreshVerifyMarkers();
}

void SpeedChart::clearVerifyMarkers()
{
    m_markerXs.clear();
    if (m_view)
        m_view->setBands(m_markerXs, 0.0, 1.0);
}

int SpeedChart::verifyMarkerCount() const
{
    return m_markerXs.size();
}

void SpeedChart::refreshVerifyMarkers()
{
    if (!m_view)
        return;
    // **必须先排序**：色带是按"从左到右"的顺序去重的（挨得太近的并成一条），
    // 而这个数组是**按校验失败被发现的先后**追加的，跟位置没有关系
    // （一块盘上第 3 个文件先报错，它的位置就排在最前面）。
    // 不排序直接去重，后一条的位置可能比前一条小，"间距"算出来是负数，
    // 于是绝大多数色带都被当成"挤在一起"丢掉——实测 843 个标记只画出 23 条，
    // 而且剩下的全是最右边那一撮。
    std::sort(m_markerXs.begin(), m_markerXs.end());
    // 色带的实际绘制在 VerifyBandChartView::paintEvent 里：
    // 把横坐标和纵轴上下限交给视图，它会在图表之后刷上背景色。
    m_view->setBands(m_markerXs, m_axisY->min(), m_axisY->max());
}

void SpeedChart::setDirections(bool showRead, bool showWrite)
{
    m_showRead = showRead;
    m_showWrite = showWrite;
    const bool lineMode = (m_style == PlotStyle::Line);
    m_readLine->setVisible(lineMode && m_showRead);
    m_readScatter->setVisible(!lineMode && m_showRead);
    m_writeLine->setVisible(lineMode && m_showWrite);
    m_writeScatter->setVisible(!lineMode && m_showWrite);
}

void SpeedChart::setPlotStyle(PlotStyle style)
{
    if (m_style == style)
        return;
    m_style = style;
    // setVisible 会一并处理图例项，图例里永远只有当前该显示的两项
    const bool lineMode = (m_style == PlotStyle::Line);
    m_readLine->setVisible(lineMode && m_showRead);
    m_readScatter->setVisible(!lineMode && m_showRead);
    m_writeLine->setVisible(lineMode && m_showWrite);
    m_writeScatter->setVisible(!lineMode && m_showWrite);
}

void SpeedChart::setLineStep(int step)
{
    const int wanted = qMax(1, step);
    if (m_lineStep == wanted)
        return;
    m_lineStep = wanted;
    // 抽稀只影响折线；散点序列本来就是全部点，不动
    rebuildLineSeries();
}

int SpeedChart::autoStepForBytes(qint64 totalBytes)
{
    // 按实际要写入/读取的数据量决定折线抽稀步长。
    // 盘越大点数越多，抽得越狠；散点图和 CSV 始终是全量。
    constexpr qint64 kGiB = 1024LL * 1024 * 1024;
    constexpr qint64 kTiB = 1024LL * kGiB;
    if (totalBytes >= kTiB)
        return 10;
    if (totalBytes >= 128 * kGiB)
        return 5;
    return 3;
}
void SpeedChart::setTimeAxis(double seconds)
{
    m_axisMode = AxisMode::Time;
    m_axisMaxGB = qMax(0.1, seconds);
    m_axisX->setTitleText(kChartTitleTime);
    m_axisX->setRange(0, m_axisMaxGB);
    m_axisX->setLabelFormat(QStringLiteral("%.0f"));
    m_axisX->setTickCount(6);
}

void SpeedChart::setSeriesColors(const QColor &readColor, const QColor &writeColor)
{
    QPen readPen(readColor);
    readPen.setWidth(2);
    m_readLine->setPen(readPen);
    m_readScatter->setColor(readColor);
    m_readScatter->setBorderColor(readColor);

    QPen writePen(writeColor);
    writePen.setWidth(2);
    m_writeLine->setPen(writePen);
    m_writeScatter->setColor(writeColor);
    m_writeScatter->setBorderColor(writeColor);
}

void SpeedChart::setCapacityAxis(double totalGB)
{
    m_axisMode = AxisMode::Capacity;
    m_axisX->setTitleText(kChartTitleCapacity);
    m_axisMaxGB = qMax(0.01, totalGB);
    m_axisX->setRange(0, m_axisMaxGB);
    // 刻度按量级调：小测试留一位小数，大盘用整数
    m_axisX->setLabelFormat(m_axisMaxGB < 20.0 ? QStringLiteral("%.1f")
                                               : QStringLiteral("%.0f"));
    m_axisX->setTickCount(qBound(3, int(m_axisMaxGB * 2.0) + 1, 11));
}

void SpeedChart::finishSamples()
{
    // 重建折线：rebuildLineSeries 里有"末点一定保留"的规则，
    // 而 appendSample 的抽稀只看"第几个"，看不出哪个是末点。
    if (m_history.isEmpty())
        return;
    rebuildLineSeries();
}

void SpeedChart::rebuildLineSeries()
{
    m_readLine->clear();
    m_writeLine->clear();
    m_readSeen = 0;
    m_writeSeen = 0;

    const int count = m_history.size();
    const int step = qMax(1, m_lineStep);
    for (int i = 0; i < count; ++i) {
        const Sample &sample = m_history.at(i);
        // 抽稀按**每条序列各自见过的采样数**计数，不能用全局下标：
        // 全局下标会被上一个阶段挤偏，导致本阶段的首点（容量 0 那个起点锚）被丢掉，
        // 曲线就不再是从头开始的了。末点始终保留，否则曲线末端够不到轴右边缘。
        const bool last = (i == count - 1);
        if (sample.hasRead) {
            if (step == 1 || (m_readSeen % step) == 0 || last)
                m_readLine->append(sample.readX, sample.readMiB);
            ++m_readSeen;
        }
        if (sample.hasWrite) {
            if (step == 1 || (m_writeSeen % step) == 0 || last)
                m_writeLine->append(sample.writeX, sample.writeMiB);
            ++m_writeSeen;
        }
    }
}

void SpeedChart::appendSample(double readX, bool hasRead, double readMiBs,
                              double writeX, bool hasWrite, double writeMiBs)
{
    Sample sample;
    sample.readX = readX;
    sample.readMiB = readMiBs;
    sample.hasRead = hasRead;
    sample.writeX = writeX;
    sample.writeMiB = writeMiBs;
    sample.hasWrite = hasWrite;
    m_history.append(sample);

    // 散点序列永远是全部点，不受密度影响
    if (hasRead) {
        m_readScatter->append(readX, readMiBs);
        m_maxY = qMax(m_maxY, readMiBs);
        m_lastX = qMax(m_lastX, readX);
    }
    if (hasWrite) {
        m_writeScatter->append(writeX, writeMiBs);
        m_maxY = qMax(m_maxY, writeMiBs);
        m_lastX = qMax(m_lastX, writeX);
    }

    // 折线序列按步长抽稀。计数用**该方向见过的采样数**：
    // 每次采样都 +1，被抽掉的那些也计入，这样步长才不会"卡住"；
    // 同时每条线自己的首点（容量 0 的起点锚）一定被保留，曲线必然从最左边开始。
    const int step = qMax(1, m_lineStep);
    if (hasRead) {
        if (step <= 1 || (m_readSeen % step) == 0)
            m_readLine->append(readX, readMiBs);
        ++m_readSeen;
    }
    if (hasWrite) {
        if (step <= 1 || (m_writeSeen % step) == 0)
            m_writeLine->append(writeX, writeMiBs);
        ++m_writeSeen;
    }

    // 横轴在 setCapacityAxis() 时就已经定死，这里只调纵轴
    updateYAxis();
}

void SpeedChart::updateYAxis()
{
    const double top = qMax(1.0, m_maxY * 1.15);
    if (!nearlyEqual(m_axisY->max(), top))
        m_axisY->setRange(0, top);
    // 纵轴缩放后，标记线要跟着重新拉满（否则会停在旧的上下限）
    if (!m_markerXs.isEmpty())
        refreshVerifyMarkers();
}
