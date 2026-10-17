# -*- coding: utf-8 -*-
"""给 src/ 下所有源文件加 GPL 版权与许可证声明头。

GPL-3.0 第 5(a) 条要求修改版显著声明"有改动并给出日期"；更基本的是，
一份版权声明是别人知道"这代码归谁、按什么条款授权"的唯一依据——
仓库根目录有 LICENSE 只算覆盖整个项目，单个文件拿去用的人看不到。

用法：python tools/add_license_headers.py          # 加头
      python tools/add_license_headers.py --check  # 只检查，缺的报出来
加过的文件会被自动跳过（幂等），所以可以反复跑。
"""
import io
import os
import sys

SPDX = "SPDX-License-Identifier: GPL-3.0-or-later"
COPYRIGHT = "Copyright (C) 2026 MOONLIGHT"

# 每个文件是干什么的：一行摘要，不给就是纯声明。
DESC = {
    "main.cpp":           "程序入口：解析命令行参数，装配主窗口。",
    "mainwindow.h":       "主窗口：磁盘列表、测试参数、实时图表与日志的装配与联动。",
    "mainwindow.cpp":     "主窗口实现：测试流程编排、采样入图、结果保存与自检。",
    "fioconfig.h":        "参数模型：两种测试模式的配置项与 fio 参数拼装。",
    "fioconfig.cpp":      "参数模型实现：校验、派生量计算与文件计划（每个文件 ≤ 2 GiB）。",
    "fiorunner.h":        "fio 子进程的启动与输出解析接口。",
    "fiorunner.cpp":      "跑 fio：拼命令行、实时读输出、抽速度采样、统计校验失败。",
    "driveinfo.h":        "驱动器枚举与容量信息查询接口。",
    "driveinfo.cpp":      "驱动器枚举实现：列出逻辑盘、查总容量/剩余空间、扫描测试文件。",
    "prociocounter.h":    "进程 I/O 计数器接口（读 Windows 的 I/O 计数器拿实时吞吐）。",
    "prociocounter.cpp":  "进程 I/O 计数器实现。",
    "perfcell.h":         "性能结果格子控件接口（大号数字 + 进度条）。",
    "perfcell.cpp":       "性能结果格子实现。",
    "speedchart.h":       "图表控件接口：读写折线/散点、抽稀、坏块色带。",
    "speedchart.cpp":     "图表控件实现，含 VerifyBandChartView（把坏块画成背景色带）。",
    "rundata.h":          "结果 CSV 的数据模型与版本门槛。",
    "rundata.cpp":        "结果 CSV 的解析与格式化，含解析器自检。",
    "resultwindow.h":     "结果窗口接口（独立窗口回看历史结果）。",
    "resultwindow.cpp":   "结果窗口实现：按数据自身的模式渲染摘要与图表。",
    "uitext.h":           "全部界面文案：由 uitext.def 展开成函数，并带平台别名与文案目录。",
    "i18n.h":             "界面语言接口：简体 / 繁体 / 英文的切换、译文查找与自检。",
    "i18n.cpp":           "译文表加载（qrc 或 exe 旁的 i18n/）、语言判定与 --i18n-check。",
}

HEADER_EXTRA = """//
// 本程序是自由软件：你可以按自由软件基金会发布的 GNU 通用公共许可证
// （第 3 版或你选择的任何更高版本）条款重新分发和/或修改它。
//
// 本程序按"希望有用"分发，但**没有任何担保**，甚至不含适销性或特定用途
// 适用性的默示担保。详见 GNU 通用公共许可证。
//
// 你应当已随本程序收到一份 GNU 通用公共许可证副本（根目录 LICENSE）。
// 如果没有，见 <https://www.gnu.org/licenses/>。
"""


def build_header(stem: str) -> str:
    lines = ["// " + SPDX, "// " + COPYRIGHT]
    desc = DESC.get(stem)
    if desc:
        lines.append("//")
        lines.append("// " + desc)
    return "\n".join(lines) + "\n" + HEADER_EXTRA


def main() -> int:
    check_only = "--check" in sys.argv
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
    root = os.path.normpath(root)
    names = sorted(n for n in os.listdir(root) if n.endswith((".cpp", ".h")))

    missing = []
    changed = []
    for name in names:
        path = os.path.join(root, name)
        data = io.open(path, encoding="utf-8", newline="").read()
        if SPDX in data:
            continue
        missing.append(name)
        if check_only:
            continue
        # 文件都是 LF；加头时保持原样，不动任何既有字节。
        eol = "\r\n" if data.startswith("#") and "\r\n" in data.split("\n")[0] else "\n"
        header = build_header(os.path.splitext(name)[0])
        if eol != "\n":
            header = header.replace("\n", eol)
        io.open(path, "w", encoding="utf-8", newline="").write(header + eol + data)
        changed.append(name)

    for n in missing:
        print(("  缺少声明: " if check_only else "  已添加: ") + n)
    print("共 %d 个源文件，%d 个%s" % (
        len(names), len(missing), "缺少声明" if check_only else "新加了声明"))
    if check_only and missing:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
