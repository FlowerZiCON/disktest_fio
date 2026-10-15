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
#include <QList>
#include <QPointF>
#include <QVector>
#include <QWidget>

#include "fioconfig.h"   // 测试项与颜色规则（画性能曲线要用）
#include "rundata.h"

// VerifyBandChartView 继承自 QChartView，这里必须要完整定义（不能只前置声明）
#include <QtCharts/QChartView>

QT_BEGIN_NAMESPACE
class QChart;
class QLineSeries;
class QScatterSeries;
class QTimer;
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
/// 折线图 / 散点图都可以抽稀，档位见 Density：高密度全画，低密度按**屏幕像素**
/// 定目标点数（和数据量无关）。不管哪一档，图表外的数据（CSV、报告）始终是全量。
///
/// **性能上有条硬规矩**：`appendSample()` 只往 m_history 里攒，绝不逐点动序列。
/// `QXYSeries::append()` 在显示中的图表上是 O(当前点数)，逐个加会让整条装载路径
/// 变成 O(N²)（实测 8214 点要 111 秒，改成批量 replace 后 0.13 秒）。
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

    void      setPlotStyle(PlotStyle style);
    PlotStyle plotStyle() const { return m_style; }

    /// 绘制密度。
    ///
    /// 这里**不再按容量分档**（原来 <256GiB 画 1/5、256GiB~1TiB 画 1/15、≥1TiB 画 1/30）。
    /// 那是错的：容量大不等于采样点多 —— 8000 个点的 117 GiB 和 8000 个点的 1 TiB
    /// 面临的是同一个渲染问题，而真正决定"能看出多少细节"的是**屏幕上有多少像素**。
    enum class Density {
        High,   ///< 高密度：每个采样点都画（忠实，代价随点数线性增长）
        Low     ///< 低密度：抽稀到"屏幕能分辨的点数"，见 targetPointCount()
    };

    void    setDensity(Density density);
    Density density() const { return m_density; }

    /// 低密度档的目标点数：**坐标区像素宽的一半**（约每 2 个像素列画 1 个点），
    /// 夹在 200~2000。
    ///
    /// 为什么不按"每列 1~2 个点"取：那会让抽稀后的曲线和高密度**肉眼分不出来**
    /// （实测 924px 的绘图区留 1848 点/条，而高密度是 8214 点，看不出区别，
    /// 绘制也只快了 30%），档位就等于没有。低密度这一档的意义是"明显更省"，
    /// 所以取到像素列的一半。和数据量、容量都无关：窗口拉宽 → 目标变大 → 细节变多。
    int targetPointCount() const;

    /// LTTB（Largest-Triangle-Three-Buckets）抽稀：把 src 压到最多 target 个点。
    ///
    /// 选它而不是"等间隔取样"是因为它**保尖峰** —— 磁盘测试里一次掉速就是一次尖峰，
    /// 而尖峰恰恰是最该看见的东西（坏块的直观表现）。首点和末点一定保留。
    /// 公开出来是为了让自检能直接断言"尖峰不会被抽掉"这条性质。
    static QList<QPointF> reduceLttb(const QList<QPointF> &src, int target);

    /// 等间隔取点（散点用）。首末点一定保留。
    static QList<QPointF> reduceEvenly(const QList<QPointF> &src, int target);

    // ---- 把一份结果画进图表：**两个渲染入口只在这里实现一次** ----
    //
    // 主窗口（加载历史数据）和结果窗口以前各写了一遍"定轴 → 设方向 → 逐点
    // appendSample → finishSamples"，连颜色规则和收尾刷新都是两份，
    // 两条路径画出来的东西可以不一致（也就真的不一致过）。
    // 现在两边都调下面这两个函数，密度由调用方给（它属于用户的界面状态）。

    /// 画**整份结果**：轴按数据自身的模式定（可靠性=容量轴，性能=时间轴），
    /// 读写两条线的颜色按"第一个有数据的测试项"取（可靠性用默认红/蓝）。
    static void plotRunData(const RunData &data, SpeedChart *chart, Density density);

    /// 画**某一个测试项的读写两条线**：横轴固定 0~runtimeSeconds 秒，
    /// 两趟的采样叠在同一段时间上。颜色按测试项取（顺序项红/蓝、随机项黄/绿）。
    static void plotPerfItem(const RunData &data, SpeedChart *chart, int itemIndex,
                             Density density);

    /// 画**一对已经算好的逐点轨迹**（主窗口实时采集攒下来的那一份，
    /// 没有 RunData 可给）。轴同样按时间定。
    static void plotPerfTraces(const QVector<QPointF> &readTrace,
                               const QVector<QPointF> &writeTrace,
                               double runtimeSeconds, FioConfig::PerfItem item,
                               SpeedChart *chart, Density density);

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
    ///
    /// **只进 m_history，不动任何序列。** 这一点是性能的关键：
    /// `QXYSeries::append()` 在显示中的图表上每次都是 O(当前点数)
    /// （实测：8214 个点逐个 append，散点要 165 秒、折线要 20 秒），
    /// 整条装载路径因此是 O(N²)。改成"先攒着、之后批量 replace"以后，
    /// 同样 8214 个点只要 0.15 秒。
    ///
    /// 攒到一定时候由内部定时器调 refreshPlot() 批量刷新（见 scheduleRefresh）。
    void appendSample(double readX, bool hasRead, double readMiBs,
                      double writeX, bool hasWrite, double writeMiBs);

    /// 立刻按当前密度把 m_history 重新画到序列上（批量 replace，一次调用一次刷新）。
    ///
    /// 实时采集那一路由内部定时器自动调；"一次性灌一批数据"的地方
    /// （加载 CSV、重画某一项）**必须**在灌完之后调一次 finishSamples()。
    void refreshPlot();

    /// 声明"点加完了"：强制刷新一次。
    ///
    /// 除了收尾刷新，它还有个副作用是必须的：末点一定要出现在曲线上
    /// （曲线末端要够到横轴右边缘）。两个抽稀函数都显式保留首末点，
    /// 所以这里只要刷新就一定满足。
    ///
    /// **凡是"一次性把一批数据灌进图表"的地方都要调**：加载 CSV、
    /// 重画某一项的曲线。实时逐拍采集那条路不用调（定时器会刷）。
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
    /// 色带数量的上限（addVerifyMarker 到顶之后就不再收）。
    static int maxVerifyMarkers() { return kMaxVerifyMarkers; }
    /// 因为超过上限而被丢弃的标记数。**不是 0 就说明图上画得比实际发现的少**，
    /// 调用方要据此提示用户一句 —— 否则"告警说 900 处、图上只有稀稀几条"
    /// 会被理解成"只有那几处坏"。
    int  droppedVerifyMarkerCount() const { return m_droppedMarkers; }

    int    sampleCount() const { return m_history.size(); }
    /// 折线序列里实际画了多少个点（高密度=全部，低密度=抽稀后）。
    int    linePointCount() const;
    /// 散点序列里的点数（同样受密度档约束）。
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

    /// 把"攒着"的刷新排上队。重复调用只会保留一个待执行的刷新 ——
    /// 一次性灌一批数据时，整个循环只会在末尾触发一次真正的重建。
    void scheduleRefresh();

    void updateYAxis();
    /// 按当前纵轴范围和坐标区把标记线竖直拉满（坐标区变化时要重算）。
    void refreshVerifyMarkers();

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
    /// 因超出上限被丢掉的标记数（见 droppedVerifyMarkerCount）
    int                  m_droppedMarkers = 0;

    QVector<Sample> m_history;
    /// 批量刷新的定时器（单次触发）。攒点的代价是刷新有一点点延迟，
    /// 换来的是"动序列的次数"从 N 降到"每次刷新一次"。
    QTimer         *m_plotTimer = nullptr;
    PlotStyle m_style     = PlotStyle::Line;
    Density   m_density   = Density::High;
    /// 上一次用的目标点数：坐标区变化时只有它真的变了才值得重画。
    mutable int m_lastTarget = 0;
    /// 最多画多少条色带。超出的不再收。
    /// 定得比"逐处明细上限"宽，免得这里先截断、把结果文件里有的位置漏掉；
    /// 屏幕上真正画出来多少由渲染时的像素级合并决定（见 paintEvent）。
    static constexpr int kMaxVerifyMarkers = 2000;
    bool      m_showRead  = true;
    bool      m_showWrite = true;
    double    m_maxY      = 1.0;
    double    m_lastX     = 0.0;
    double    m_axisMaxGB = 1.0;
    AxisMode  m_axisMode  = AxisMode::Capacity;
};
