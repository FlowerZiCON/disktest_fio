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

#include <QColor>
#include <QVector>
#include <QWidget>

// VerifyBandChartView 继承自 QChartView，这里必须要完整定义（不能只前置声明）
#include <QtCharts/QChartView>

QT_BEGIN_NAMESPACE
class QChart;
class QLineSeries;
class QScatterSeries;
class QValueAxis;
QT_END_NAMESPACE

/// 图表视图：把"校验失败位置"画成**背景色**。
///
/// 为什么不用 qchart 的序列来画：
///   * 一条 QLineSeries 会被当成折线连着画——把所有标记塞进一条，
///     第 N 条的顶端会和第 N+1 条的底端连起来，屏幕上就是一片头尾相连的斜线；
///   * 每处一条独立序列虽然能避免连线，但 QChart 只有 addSeries()，
///     没法把序列插到曲线**下面**，标记就会盖住数据点；
///     几百条序列本身也是笔不小的开销。
/// 所以改成在**图表自身绘制之前**先往视口上刷一层半透明色带：
/// 既是"背景色"，又永远在曲线之下，还不受序列数量限制。
class VerifyBandChartView : public QChartView
{
    Q_OBJECT

public:
    explicit VerifyBandChartView(QChart *chart, QWidget *parent = nullptr);

    /// 换掉要标的横坐标（x 值和纵轴的上下限）。调用后会自动重绘。
    void setBands(const QVector<double> &xs, double yMin, double yMax);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_xs;
    double m_yMin = 0.0;
    double m_yMax = 1.0;
};

/// 实时速度图：**读取 = 红色**，**写入 = 蓝色**，支持折线图 / 散点图两种画法。
///
/// 横轴不是时间，而是**已处理容量（GB）**：开测前就按本次要用的可用空间固定下来，
/// 之后只往右加点，不再扩轴。写入和读取各自从 0 开始累加（不是首尾相接）。
///
/// 折线图可以按步长抽稀（步长 1 = 全画）。步长通常由数据量自动决定，
/// 见 autoStepForBytes()。散点图不受影响，始终画全部点。
class SpeedChart : public QWidget
{
    Q_OBJECT

public:
    enum class PlotStyle {
        Line,     ///< 折线图（默认）
        Scatter   ///< 散点图
    };

    explicit SpeedChart(QWidget *parent = nullptr);

    void clear();

    void setDirections(bool showRead, bool showWrite);
    bool showsRead() const { return m_showRead; }
    bool showsWrite() const { return m_showWrite; }

    void      setPlotStyle(PlotStyle style);
    PlotStyle plotStyle() const { return m_style; }

    /// 折线图的抽稀步长：1 = 画全部点，3 = 每 3 个点画 1 个，以此类推。
    void setLineStep(int step);
    int  lineStep() const { return m_lineStep; }

    /// 按数据量自动选抽稀步长：
    /// < 128 GiB → 3；128 GiB ~ 1 TiB → 5；≥ 1 TiB → 10。
    static int autoStepForBytes(qint64 totalBytes);

    /// 横轴用什么单位：可靠性测试用"已处理容量"，性能测试用"时间"。
    enum class AxisMode { Capacity, Time };

    /// 固定横轴上限。开测时调一次，之后 appendSample 不再动横轴。
    void   setCapacityAxis(double totalGB);   ///< 容量轴（GB）
    void   setTimeAxis(double seconds);       ///< 时间轴（秒）
    double axisMax() const { return m_axisMaxGB; }
    AxisMode axisMode() const { return m_axisMode; }

    /// 换读/写两条线的颜色。性能模式里随机项要改成黄/绿。
    void setSeriesColors(const QColor &readColor, const QColor &writeColor);

    /// 追加一个采样点。readX / writeX 是各自**已处理的容量（GB）**。
    void appendSample(double readX, bool hasRead, double readMiBs,
                      double writeX, bool hasWrite, double writeMiBs);

    /// 声明"点加完了"：按当前步长重建折线，**保证最后一个点一定画出来**。
    ///
    /// 为什么需要它：appendSample 的抽稀规则是"每隔 step 个点保留一个"，
    /// 最后那个点落在哪一格是偶然的——不是 step 的整数倍时就被抽掉了，
    /// 曲线末端于是够不到横轴右边缘（实测：性能数据每趟停在 9.51 秒，
    /// 而轴上标的是 10 秒）。边加边画的时候没法知道哪一个是末点，
    /// 只能由调用方在加完之后说一声；rebuildLineSeries 里对末点有专门的保留逻辑。
    ///
    /// **凡是"一次性把一批数据灌进图表"的地方都要调**：加载 CSV、
    /// 重画某一项的曲线。实时逐拍采集那条路不用调（下一拍还会来，
    /// 而且收尾补点本身就在末尾）。
    void finishSamples();

    /// 在横坐标 x 处画一条**贯穿纵轴范围的黄色竖线**，用来标记"数据校验失败"的位置。
    ///
    /// 判读习惯：异常位置用竖直方向的一条线标出来（对准横轴的坐标），
    /// 而不是在曲线旁边放个记号——曲线本身是速度随容量变化的折线，
    /// 竖线不会和数据点混在一起，也不会挡住任何一个采样点。
    ///
    /// 线会被裁到坐标区内，所以永远"不高出纵轴范围"。
    void addVerifyMarker(double x);
    void clearVerifyMarkers();
    int  verifyMarkerCount() const;

    int    sampleCount() const { return m_history.size(); }
    /// 折线序列里实际画了多少个点（抽稀后约为采样数的 1/step）。
    int    linePointCount() const;
    /// 散点序列里的点数（不受抽稀影响）。
    int    scatterPointCount() const;
    double lastX() const { return m_lastX; }
    double firstX() const;

    QWidget *viewWidget() const;

private:
    /// 一次采样：两个方向各自的横坐标与速率。
    struct Sample
    {
        double readX  = 0.0;
        double readMiB = 0.0;
        double writeX = 0.0;
        double writeMiB = 0.0;
        bool   hasRead  = false;
        bool   hasWrite = false;
    };

    void updateYAxis();
    /// 按当前纵轴范围和坐标区把标记线竖直拉满（坐标区变化时要重算）。
    void refreshVerifyMarkers();
    /// 按当前步长（重新）生成折线序列；散点序列始终是全部点。
    void rebuildLineSeries();

    QChart              *m_chart        = nullptr;
    VerifyBandChartView *m_view         = nullptr;
    QLineSeries         *m_readLine     = nullptr;
    QScatterSeries      *m_readScatter  = nullptr;
    QLineSeries         *m_writeLine    = nullptr;
    QScatterSeries      *m_writeScatter = nullptr;
    QValueAxis          *m_axisX        = nullptr;
    QValueAxis          *m_axisY        = nullptr;
    /// 校验失败位置的横坐标。实际绘制在 VerifyBandChartView::paintEvent 里。
    QVector<double>      m_markerXs;

    QVector<Sample> m_history;
    PlotStyle m_style     = PlotStyle::Line;
    int       m_lineStep  = 1;
    /// 最多画多少条色带。超出的不再收。
    /// 定得比"逐处明细上限"宽，免得这里先截断、把结果文件里有的位置漏掉；
    /// 屏幕上真正画出来多少由渲染时的像素级合并决定（见 paintEvent）。
    static constexpr int kMaxVerifyMarkers = 2000;
    /// 每个方向**累计收到过**多少个采样点（不管有没有被抽稀保留）。
    /// 抽稀判定必须用它，不能用"已保留的点数"——那样计数器只在保留时前进，
    /// 会出现保留一次之后再也追不上步长的死循环。
    int       m_readSeen  = 0;
    int       m_writeSeen = 0;
    bool      m_showRead  = true;
    bool      m_showWrite = true;
    double    m_maxY      = 1.0;
    double    m_lastX     = 0.0;
    double    m_axisMaxGB = 1.0;
    AxisMode  m_axisMode  = AxisMode::Capacity;
};
