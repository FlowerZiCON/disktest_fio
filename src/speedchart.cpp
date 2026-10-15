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
#include <QTimer>
#include <QVBoxLayout>
#include <QValueAxis>

#include <algorithm>
#include <cmath>

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

    // 批量刷新的定时器。攒点是性能的关键（见 appendSample），
    // 250 ms 的延迟在实时曲线上完全看不出来，却把"动序列的次数"
    // 从"每个采样点一次"降到"每次刷新一次"。
    m_plotTimer = new QTimer(this);
    m_plotTimer->setSingleShot(true);
    m_plotTimer->setInterval(250);
    connect(m_plotTimer, &QTimer::timeout, this, &SpeedChart::refreshPlot);

    // 标记色带要**贯穿整个坐标区**（从纵轴底到顶），所以坐标区一变就得重画。
    // 顺带：坐标区宽度变了，"屏幕能分辨的点数"也变了 —— 但只有目标点数真的
    // 跨档时才值得重画（拖窗口会连续触发 plotAreaChanged，别每次都重建）。
    connect(m_chart, &QChart::plotAreaChanged, this, [this](const QRectF &) {
        refreshVerifyMarkers();
        if (targetPointCount() != m_lastTarget)
            scheduleRefresh();
    });

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
    if (m_plotTimer)
        m_plotTimer->stop();
    m_history.clear();
    m_readLine->clear();
    m_readScatter->clear();
    m_writeLine->clear();
    m_writeScatter->clear();
    clearVerifyMarkers();
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

void SpeedChart::setDensity(Density density)
{
    if (m_density == density)
        return;
    m_density = density;
    // 密度一改就要重画（高密度=全画，低密度=抽稀）
    refreshPlot();
}

int SpeedChart::targetPointCount() const
{
    // 坐标区的像素宽 ≈ 这里能分辨的列数。
    int px = int(m_chart ? m_chart->plotArea().width() : 0);
    if (px <= 0 && m_view)
        px = m_view->viewport()->width();
    // 低密度是"明显更省"的那一档：**每 2 个像素列画 1 个点**。
    //
    // 这里最初写的是"每列 2 个点"。那个目标是"抽稀之后和高密度看起来一模一样"——
    // 实测 924px 的绘图区会留 1848 点/条（高密度是 8214 点），两条曲线肉眼分不出来，
    // 绘制也只从 20.8ms 降到 14.6ms：**档位在，但等于没起作用**。
    // 改成每 2 列 1 个点后点数掉到约 1/4，曲线明显更简、也真的更流畅；
    // 折线是 LTTB 选点，大起伏仍然保得住（要看每一个点就切回高密度）。
    return qBound(200, px / 2, 2000);
}

QList<QPointF> SpeedChart::reduceEvenly(const QList<QPointF> &src, int target)
{
    const int n = src.size();
    if (target < 2 || target >= n)
        return src;
    QList<QPointF> out;
    out.reserve(target);
    for (int i = 0; i < target; ++i) {
        // 首末点一定取到：曲线两端要落在真实数据上
        const int index = int(qint64(i) * (n - 1) / qMax(1, target - 1));
        out.append(src.at(index));
    }
    return out;
}

QList<QPointF> SpeedChart::reduceLttb(const QList<QPointF> &src, int target)
{
    // LTTB（Largest-Triangle-Three-Buckets）。
    // 它按"桶内哪一点与前后桶构成最大三角形"来选点，因此**保留局部极值**——
    // 等间隔取样会把一次突然的掉速平均掉，而这个工具恰恰要靠曲线看掉速。
    const int n = src.size();
    if (target < 3 || target >= n)
        return src;

    QList<QPointF> out;
    out.reserve(target);

    const double every = double(n - 2) / double(target - 2);
    int a = 0;                       // 上一个被选中的点
    out.append(src.at(0));           // 首点必留

    for (int i = 0; i < target - 2; ++i) {
        // 下一个桶的平均点，作为三角形的第三个顶点
        int avgStart = int(std::floor((i + 1) * every)) + 1;
        int avgEnd = int(std::floor((i + 2) * every)) + 1;
        avgEnd = qMin(avgEnd, n);
        const int avgLen = avgEnd - avgStart;
        double avgX = 0.0;
        double avgY = 0.0;
        if (avgLen > 0) {
            for (int j = avgStart; j < avgEnd; ++j) {
                avgX += src.at(j).x();
                avgY += src.at(j).y();
            }
            avgX /= avgLen;
            avgY /= avgLen;
        } else {
            avgX = src.at(n - 1).x();
            avgY = src.at(n - 1).y();
        }

        // 当前桶：在其中挑出与 (a, 平均点) 构成最大三角形的那一个
        const int rangeStart = int(std::floor(i * every)) + 1;
        const int rangeEnd = qMin(int(std::floor((i + 1) * every)) + 1, n - 1);

        const double ax = src.at(a).x();
        const double ay = src.at(a).y();
        double maxArea = -1.0;
        int chosen = qMin(rangeStart, n - 2);
        for (int j = rangeStart; j < rangeEnd; ++j) {
            const double area = std::abs((ax - avgX) * (src.at(j).y() - ay)
                                         - (ax - src.at(j).x()) * (avgY - ay));
            if (area > maxArea) {
                maxArea = area;
                chosen = j;
            }
        }
        out.append(src.at(chosen));
        a = chosen;
    }

    out.append(src.at(n - 1));       // 末点必留
    return out;
}

void SpeedChart::scheduleRefresh()
{
    if (!m_plotTimer)
        return;
    // 已经在队列里就不再重复排 —— 一次性灌几万个点时只会真正重建一次
    if (!m_plotTimer->isActive())
        m_plotTimer->start();
}

void SpeedChart::refreshPlot()
{
    if (m_plotTimer)
        m_plotTimer->stop();

    const bool reduce = (m_density == Density::Low);
    const int target = targetPointCount();
    m_lastTarget = target;

    QList<QPointF> readPts;
    QList<QPointF> writePts;
    readPts.reserve(m_history.size());
    writePts.reserve(m_history.size());
    for (const Sample &sample : m_history) {
        if (sample.hasRead)
            readPts.append(QPointF(sample.readX, sample.readMiB));
        if (sample.hasWrite)
            writePts.append(QPointF(sample.writeX, sample.writeMiB));
    }

    // 先把纵轴定下来再灌点：否则这一拍的点会按旧范围画，末端被上边缘裁掉。
    // m_maxY 是 appendSample 一路维护的最大值，所以这里不需要扫 m_history。
    updateYAxis();

    // 关键：一次性 replace，而不是 clear() + 逐点 append()。
    // 实测 8214 点：折线 4 ms（对比逐点 append 20 秒）、散点 149 ms（对比 165 秒）。
    if (reduce) {
        m_readLine->replace(reduceLttb(readPts, target));
        m_writeLine->replace(reduceLttb(writePts, target));
        m_readScatter->replace(reduceEvenly(readPts, target));
        m_writeScatter->replace(reduceEvenly(writePts, target));
    } else {
        m_readLine->replace(readPts);
        m_writeLine->replace(writePts);
        m_readScatter->replace(readPts);
        m_writeScatter->replace(writePts);
    }
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
    // 强制刷一次。两个抽稀函数都显式保留首末点，所以"末点一定画出来"是白拿的。
    if (m_history.isEmpty()) {
        if (m_plotTimer)
            m_plotTimer->stop();
        return;
    }
    refreshPlot();
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

    if (hasRead) {
        m_maxY = qMax(m_maxY, readMiBs);
        m_lastX = qMax(m_lastX, readX);
    }
    if (hasWrite) {
        m_maxY = qMax(m_maxY, writeMiBs);
        m_lastX = qMax(m_lastX, writeX);
    }

    // 这里**不碰序列**，只排队一次批量刷新。
    // 逐点 append 是 O(当前点数) 的，整条装载路径因此是 O(N²)（实测 8214 点要 111 秒）。
    scheduleRefresh();
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
