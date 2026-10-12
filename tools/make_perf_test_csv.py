#!/usr/bin/env python3
"""生成一份**性能测试**格式的结果 CSV，用来验证「打开测试结果」窗口的性能分支。

结构与 MainWindow::saveRunDataTo()（性能模式）写出来的完全一致：
  * 参数段带 test_mode=performance / perf_data_bytes / perf_items
  * [samples] 段是 9 列，最后两列是 item,pass
  * read_GiB / write_GiB 两列在性能数据里装的是**秒**（横轴 = 时间）
"""
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "perf_test_data.csv"

ITEM_KEYS = ["seq_q8t1", "seq_q1t1", "rand_q32t1", "rand_q1t1"]
# 每一项的"真实"读写速度（MiB/s），用来生成可校验的曲线
READ_BASE = [2850.0, 1650.0, 420.0, 55.0]
WRITE_BASE = [2400.0, 1350.0, 380.0, 48.0]
RUNTIME = 10.0
STEP = 0.5
SAMPLES_PER_PASS = int(RUNTIME / STEP)   # 20 拍

lines = []
lines.append("# ml_disktest-run-data;1")
lines.append("# [run]")


def param(key, value):
    lines.append("{},{}".format(key, value))


param("test_mode", "performance")
param("mode", "顺序读写")
param("mode_key", "seq")
param("target", "G:/")
param("folder", "G:\\ml_disktest")
param("block_size", "4m")
param("ioengine", "windowsaio")
param("iodepth", "16")
param("rounds", "1")
param("direct", "1")
param("verify", "0")
param("do_write", "1")
param("do_read", "1")
param("perf_data_bytes", "1073741824")
param("perf_runtime_sec", "10")
param("perf_items", " ".join(ITEM_KEYS))
param("axis_max_GiB", "10.000000")
param("plot_style", "line")
param("line_step", "1")
param("density", "high")
for i, key in enumerate(ITEM_KEYS):
    param(key + "_read_MiBps", "{:.3f}".format(READ_BASE[i]))
    param(key + "_write_MiBps", "{:.3f}".format(WRITE_BASE[i]))

lines.append("# [samples]")
lines.append(
    "read_GiB,read_MiBps,has_read,write_GiB,write_MiBps,has_write,duration_sec,item,pass"
)

# 波动用一个确定的锯齿，方便人眼核对曲线不是一条死直线
for item in range(4):
    for pseudo_pass in (0, 1):
        for k in range(SAMPLES_PER_PASS):
            t = k * STEP
            wobble = ((k % 5) - 2) * 0.01
            if pseudo_pass == 0:
                read_v = READ_BASE[item] * (1.0 + wobble)
                write_v = 0.0
                has_read, has_write = 1, 0
            else:
                read_v = 0.0
                write_v = WRITE_BASE[item] * (1.0 + wobble)
                has_read, has_write = 0, 1
            lines.append(
                "{:.6f},{:.3f},{},{:.6f},{:.3f},{},{:.3f},{},{}".format(
                    t, read_v, has_read, t, write_v, has_write, STEP, item, pseudo_pass
                )
            )

# 每一项收尾补的终点（值沿用最后一拍）——和 onJobFinished 里的补点一致
for item in range(4):
    for pseudo_pass in (0, 1):
        if pseudo_pass == 0:
            read_v, write_v, has_read, has_write = READ_BASE[item], 0.0, 1, 0
        else:
            read_v, write_v, has_read, has_write = 0.0, WRITE_BASE[item], 0, 1
        lines.append(
            "{:.6f},{:.3f},{},{:.6f},{:.3f},{},{:.3f},{},{}".format(
                RUNTIME, read_v, has_read, RUNTIME, write_v, has_write, 0.0, item, pseudo_pass
            )
        )

with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
    fh.write("\n".join(lines) + "\n")

print("wrote {} ({} sample rows)".format(OUT, len(lines) - 30))
