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

#include <QWidget>

/// 性能测试结果表里的一个格子：大号数字 + 底部的绿色进度条。
///
/// 排版照 example.png（CrystalDiskMark 风格）：白底、圆角边框、绿色条从左往右长，
/// 长度按"当前值 / 本次已出现的最大值"画，最后没测的显示 "--"。
class PerfCell : public QWidget
{
    Q_OBJECT

public:
    explicit PerfCell(QWidget *parent = nullptr);

    /// 填入结果（MiB/s）。传负数表示"还没测"，显示 --。
    void setValue(double mbps);
    double value() const { return m_value; }

    /// 进度条的满量程。所有格子共用同一个上限，条长才有可比性。
    void setScaleMax(double max);

    void clear();

    /// 测试未结束时禁止点击（还没数据可看）。
    void setClickable(bool clickable);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /// 用户点了这个格子 —— 打开这一项的速度曲线。
    void activated();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    double m_value    = -1.0;
    double m_scaleMax = 1.0;
    bool   m_clickable = false;
    bool   m_hover = false;
};
