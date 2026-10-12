#!/usr/bin/env python3
"""把现有结果 CSV 转成两种典型 JSON 写法，量一下体积差。

用来回答"CSV 换 JSON 到底会不会变大"这个问题——不靠感觉，靠字节数。

写法 A：逐点对象（最自然的 JSON 写法）
    "samples": [{"read_GiB":0.0,"read_MiBps":0.0,...}, ...]
写法 B：列式（把每一列各放一个数组）
    "samples": {"read_GiB":[...],"read_MiBps":[...],...}

2026-10 实测（本机两份真实结果文件）：

    == ml_disktest-data.csv (997 samples) ==
      CSV                       44332 B     44.5 B/sample
      JSON 逐点对象(缩进1)     169093 B    169.6 B/sample   x3.81
      JSON 逐点对象(压紧)      128075 B    128.5 B/sample   x2.89
      JSON 列式(缩进1)          66553 B     66.8 B/sample   x1.50
      JSON 列式(压紧)           38447 B     38.6 B/sample   x0.87

    == ml_disktest-data-perf.csv (160 samples) ==
      CSV                        8219 B     51.4 B/sample
      JSON 逐点对象(缩进1)      31538 B    197.1 B/sample   x3.84
      JSON 逐点对象(压紧)       23249 B    145.3 B/sample   x2.83
      JSON 列式(缩进1)          12681 B     79.3 B/sample   x1.54
      JSON 列式(压紧)            6729 B     42.1 B/sample   x0.82

结论：**逐点对象的 JSON 会大 3~4 倍**（每行都要重复一遍键名）；
列式 + 压紧反而比 CSV 小一成多（不用重复表头，也不用给每个数字补位）。
"""
import io
import json
import sys
import os


def parse_csv(path):
    params = []
    header = None
    rows = []
    in_samples = False
    with io.open(path, encoding='utf-8') as fh:
        for line in fh:
            line = line.rstrip('\n').rstrip('\r')
            if not line:
                continue
            if line.startswith('#'):
                if '[samples]' in line:
                    in_samples = True
                continue
            if not in_samples:
                if ',' in line:
                    k, v = line.split(',', 1)
                    params.append((k.strip(), v.strip()))
                continue
            if header is None:
                header = line.split(',')
                continue
            rows.append(line.split(','))
    return params, header, rows


def to_columns(header, rows):
    """按表头把每列转成合适的类型（能当数字就当数字）"""
    cols = {}
    for i, name in enumerate(header):
        values = []
        for r in rows:
            cell = r[i] if i < len(r) else ''
            try:
                values.append(int(cell))
            except ValueError:
                try:
                    values.append(float(cell))
                except ValueError:
                    values.append(cell)
        cols[name] = values
    return cols


def main():
    for path in sys.argv[1:]:
        params, header, rows = parse_csv(path)
        cols = to_columns(header, rows)

        base = {
            'format': 'ml_disktest-result',
            'version': 1,
            'params': dict(params),
        }

        # 写法 A：逐点对象
        a = dict(base)
        a['samples'] = [dict(zip(header, [cols[h][i] for h in header]))
                        for i in range(len(rows))]

        # 写法 B：列式
        b = dict(base)
        b['samples'] = cols

        size_csv = os.path.getsize(path)
        size_a = len(json.dumps(a, ensure_ascii=False, indent=1).encode('utf-8'))
        size_a_min = len(json.dumps(a, ensure_ascii=False, separators=(',', ':')).encode('utf-8'))
        size_b = len(json.dumps(b, ensure_ascii=False, indent=1).encode('utf-8'))
        size_b_min = len(json.dumps(b, ensure_ascii=False, separators=(',', ':')).encode('utf-8'))

        n = len(rows)
        print('== {} ({} samples) =='.format(os.path.basename(path), n))
        print('  CSV                    {:>8} B   {:>6.1f} B/sample'.format(size_csv, size_csv / n))
        print('  JSON 逐点对象(缩进1)   {:>8} B   {:>6.1f} B/sample   x{:.2f}'.format(
            size_a, size_a / n, size_a / size_csv))
        print('  JSON 逐点对象(压紧)    {:>8} B   {:>6.1f} B/sample   x{:.2f}'.format(
            size_a_min, size_a_min / n, size_a_min / size_csv))
        print('  JSON 列式(缩进1)       {:>8} B   {:>6.1f} B/sample   x{:.2f}'.format(
            size_b, size_b / n, size_b / size_csv))
        print('  JSON 列式(压紧)        {:>8} B   {:>6.1f} B/sample   x{:.2f}'.format(
            size_b_min, size_b_min / n, size_b_min / size_csv))
        print()


main()
