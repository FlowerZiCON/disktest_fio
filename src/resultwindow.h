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
#include <QDialog>

#include "rundata.h"

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QLabel;
class QRadioButton;
QT_END_NAMESPACE

class SpeedChart;
class PerfCell;

/// 打开一份历史测试结果用的独立窗口。
///
/// **它绝不碰主界面**：不动主图表、不动卡片、不动"当前选中的盘"。
/// 历史数据可能来自一块当前机器上根本不存在的盘（比如昨天插的 G:），
/// 把它灌进主界面会让卡片和磁盘进度条显示"当前盘"的信息，与数据来源对不上。
///
/// 窗口里的内容按**数据自身的模式**渲染：
///   * 可靠性数据 → 横轴为"已处理容量（GiB）"的双线图（读红 / 写蓝）
///   * 性能数据   → 横轴为"时间（秒）"的结果表（四项），每项可点开看读写曲线
class ResultWindow : public QDialog
{
    Q_OBJECT

public:
    /// 打开一份结果文件。
    /// 解析失败时弹一个提示框并返回 nullptr；parent 为空时不弹框（自检用）。
    static ResultWindow *open(QWidget *parent, const QString &path);

    /// 自检用：当前图表画的是哪一项（-1 = 合起来的曲线）。
    int selectedItem() const { return m_selectedItem; }
    SpeedChart *chart() const { return m_chart; }

private:
    explicit ResultWindow(const RunData &data, QWidget *parent = nullptr);

    void rebuildReliabilityChart();
    /// 图表上方的「绘图方式 / 密度」一行。和主界面同一套控件 ——
    /// 主界面能切的东西，回看窗口也应该能切，否则同一份数据在两个地方
    /// 的"可看性"不一样（尤其是几千点的高密度数据）。
    void buildChartStyleRow(QVBoxLayout *root);
    void buildPerfTable(QVBoxLayout *root);
    /// 只画某一项的读写两条线（颜色按测试项：顺序项红/蓝，随机项黄/绿）。
    void rebuildPerfChart(int item);
    /// 老文件没有分项信息时，画一条合起来的读/写曲线。
    void rebuildPerfCombinedChart();

    /// 结果表里有几个格子（= 性能测试项的数量）。
    /// **必须是这一个常量**：以前"填格子"的循环走 allPerfItems().size()、
    /// 而"设满量程"的循环写死 4，两个数一旦不一致就是越界写。
    static constexpr int kPerfCellCount = 4;

    RunData m_data;
    /// 解析出来的四项轨迹，构造时算一次就够：m_data 之后不再变，
    /// 结果表和图表都要反复用它，没必要每次都重算。
    QVector<RunData::Trace> m_traces;
    SpeedChart *m_chart     = nullptr;
    QLabel     *m_hintLabel = nullptr;
    QRadioButton *m_lineRadio = nullptr;
    QRadioButton *m_scatterRadio = nullptr;
    QRadioButton *m_densityHighRadio = nullptr;
    QRadioButton *m_densityLowRadio = nullptr;
    /// 性能数据：当前单独显示的测试项下标（-1 = 合起来的曲线）。
    int m_selectedItem = -1;
    /// 结果表里的大号数字格子（每项：读取、写入）。
    PerfCell *m_readCells[kPerfCellCount]  = {};
    PerfCell *m_writeCells[kPerfCellCount] = {};
};
