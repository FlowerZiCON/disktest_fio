# 第三方组件与许可证声明

本程序（磁盘测试 / 磁盘测试By MOONLIGHT）自身按 **GNU 通用公共许可证第 3 版
或任何更高版本**（`GPL-3.0-or-later`）发布，全文见仓库根目录 [`LICENSE`](../LICENSE)。

本程序**分发**或**使用**了下列第三方组件。它们各自的版权与许可证条款如下，
与本程序自身的许可证相互独立。

---

## 1. Qt 6（动态链接）

| 项目 | 内容 |
| --- | --- |
| 版本 | 6.8.3（`msvc2022_64`，构建时要求 ≥ 6.5） |
| 使用的模块 | Qt6Core / Qt6Gui / Qt6Widgets / Qt6Charts |
| 许可证 | **LGPL-3.0**（另有 GPL-3.0 与商业许可可选） |
| 链接方式 | **动态链接**（发布包内为 `Qt6*.dll`，由 `windeployqt` 收集） |
| 许可证全文 | [`licenses/Qt-LGPLv3.txt`](Qt-LGPLv3.txt) |

**为什么用 LGPL 而不是 GPL**：LGPL-3.0 允许本程序在**动态链接** Qt 的前提下
采用自己的许可证，而不被 Qt 的许可证"传染"。本程序发布包里的 Qt 是 DLL 形式，
满足 LGPL-3.0 第 4 条关于"使用合适的共享库机制"的要求。

按 LGPL-3.0 的要求，你**有权**替换本程序所使用的 Qt 版本：把发布包里的
`Qt6*.dll` 换成你自己编译或从 Qt 官方取得的、ABI 兼容的同版本 DLL 即可，
无需重新编译本程序。

Qt 的版权归 The Qt Company Ltd. 及相关贡献者所有。

---

## 2. fio（独立进程调用 + 随包分发）

| 项目 | 内容 |
| --- | --- |
| 版本 | 3.43 |
| 许可证 | **GPL-2.0**（仅第 2 版） |
| 来源 | <https://github.com/axboe/fio> |
| 调用方式 | **独立可执行文件**（`fio/fio.exe`），由本程序通过子进程启动 |
| 许可证全文 | [`licenses/fio-GPLv2.txt`](fio-GPLv2.txt) |
| 上游版权文件 | [`third_party/fio/COPYING.txt`](../third_party/fio/COPYING.txt) |

**为什么本程序可以自选许可证**：fio 是以**独立进程**方式调用的，本程序既不
链接 fio 的代码、也不包含其头文件，因此两者构成 GPL 所说的"聚合"（mere
aggregation）而非"衍生作品"。本程序自身代码不因此受 GPL-2.0 约束。

**随包分发 fio 时你需要遵守**（发布包 `fio/` 目录下已附上游许可文件）：

- 保留 fio 的版权声明与 GPL-2.0 全文；
- 提供 fio 的**完整对应源码**，或按 GPL-2.0 第 3 条给出获取源码的书面承诺。
  fio 源码可从上游仓库按上述版本号取得；
- 由于 GPL-2.0 与 GPL-3.0 不兼容，**不要**把 fio 的代码与本程序的代码
  编译或链接进同一个可执行文件。

fio 的版权归 Jens Axboe 及相关贡献者所有。

`third_party/fio/MORAL-LICENSE.txt` 是作者附的道德声明，**不构成**许可证条款，
但其中一条值得遵守：发表使用 fio 得到的结果时，应注明使用了 fio 及具体版本。

---

## 3. Microsoft Visual C++ 运行库（随包分发）

| 项目 | 内容 |
| --- | --- |
| 文件 | `vcruntime140*.dll` / `msvcp140*.dll` / `concrt140.dll` / `vccorlib140.dll` / `vc_redist.x64.exe` |
| 许可证 | **Microsoft 可再分发代码条款**（Microsoft Software License Terms） |

Qt 与 MSVC 均以 `/MD` 方式构建，因此本程序必须动态链接 VC++ 运行库；
发布包内直接从 Visual Studio 安装目录的 `Redist` 收集这些 DLL，
以满足"目标机器上没有安装 VC++ 运行库"的场景。

这些文件的再分发受 Microsoft 许可证约束，**不属于**本程序的 GPL 授权范围。
版权归 Microsoft Corporation 所有。

---

## 4. 汇总

| 组件 | 许可证 | 与本程序代码的关系 | 随包分发 |
| --- | --- | --- | --- |
| 本程序自身 | GPL-3.0-or-later | — | 是（`disktest.exe`） |
| Qt 6.8.3 | LGPL-3.0 | 动态链接 → 独立于本程序许可证 | 是（`Qt6*.dll`） |
| fio 3.43 | GPL-2.0 | 独立进程 → 聚合，非衍生 | 是（`fio/fio.exe`） |
| MSVC 运行库 | Microsoft 可再分发条款 | 系统级运行库 | 是 |

发布包内 `licenses/` 目录即为依据本文件收集的许可证文本集合。
