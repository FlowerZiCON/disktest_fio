# 磁盘测试 By MOONLIGHT v3.1.0

## 新增

1. **Windows ARM64 原生版本**：ARM 版 Windows（骁龙本、Windows Dev Kit 之类）现在有
   **原生编译**的包，不再依赖 x64 模拟层。
   - 安装包 `disktest-3.1.0-win-arm64-setup.exe`、绿色版
     `disktest-3.1.0-win-arm64-portable.7z`，内容和 x64 包一致
     （Qt 运行库 + MSVC 运行库 + fio + 许可证全带）；
   - exe 的「属性 → 详细信息」里写的是 `win-arm64`，程序「关于」里也会显示
     `win-arm64`，不会和 x64 包混淆；
   - 从源码构建：`build.bat package /arch arm64`，产物在 `build-arm64\`，
     与 x64 互不覆盖。
2. **随包带原生 ARM64 的 fio 3.43**：上游只为 Windows 发布 x64 构建，所以这份是用
   **同一个 3.43 源码**（未改动）配合 llvm-mingw 交叉编出来的。
   这一点很关键：ARM64 机器上用 x64 模拟层跑 fio 是能跑起来的，**但这是测磁盘性能的
   工具**，模拟层的开销会让测出来的数字失去意义。现在包里那份是原生 ARM64。
3. **Linux 版本同步到 3.1.0**：amd64 / arm64 的 deb 与 tar.gz 都重新构建
   （Ubuntu 22.04 + Qt 6.2.4，`g++ -Wall -Wextra` 全量重建 0 warning）。

## 改进

1. **所有跟架构有关的打包逻辑改成"看 PE 头"而不是看文件名**：
   MSVC 运行库、Qt DLL、fio 都核对 PE 的 Machine 字段，架构不匹配就跳过并打印原因。
   这类问题以前是"包里混进另一个架构的库 → 编译打包全都不报错 → 只在目标机上崩"，
   现在会在构建阶段就被挡下或明确提示。
2. **安装器支持按架构出包**：`disktest.iss` 的架构、目录、文件名后缀都可以在编译时
   覆盖，默认行为（x64 + 老文件名）保持不变；ARM64 包自动带 `-arm64` 后缀，
   两种包可以放在同一个目录里。
3. **文档**：README 与 `docs/readme.md` 补了 Windows ARM64 的构建步骤和四个坑
   （ARM64 的 Qt 不带 host 工具、`vcvarsamd64_arm64.bat` 可能不存在、
   windeployqt 会静默拷错架构、VS 的 arm64 Redist 里混着一个 x64 的
   `vcruntime140_1.dll`）；`docs/fio-build-assessment.md` 加了 ARM64 fio 的完整配方。

## 下载

| 平台 | 文件 | 说明 |
| --- | --- | --- |
| Windows x64 | `disktest-3.1.0-win64-setup.exe` | 安装版，推荐 |
| Windows x64 | `disktest-3.1.0-win64-portable.7z` | 绿色版**全量包**：含 Qt / MSVC 运行库与 fio，干净系统解压即用 |
| Windows x64 | `disktest-3.1.0-win64-single.7z` | 绿色版**精简包**：只有本体，机器上需已有 Qt 6 运行库与 fio |
| Windows ARM64 | `disktest-3.1.0-win-arm64-setup.exe` | 安装版，**ARM 版 Windows 请选这个** |
| Windows ARM64 | `disktest-3.1.0-win-arm64-portable.7z` | 绿色版**全量包**：含 Qt / MSVC 运行库与**原生 ARM64 的 fio** |
| Linux x86_64 | `disktest-3.1.0-linux-amd64.tar.gz` | 解压即用，依赖系统 Qt 6.2+ 与 fio 3.x |
| Linux aarch64 | `disktest-3.1.0-linux-arm64.tar.gz` | 同上 |
| Ubuntu / Debian amd64 | `disktest_3.1.0-1_amd64.deb` | 安装版：`sudo apt install ./disktest_3.1.0-1_amd64.deb` |
| Ubuntu / Debian arm64 | `disktest_3.1.0-1_arm64.deb` | 同上 |

Linux tar.gz 包内容：`disktest`（已 strip）+ `README.md` + `licenses/` + `docs/images/`；
deb 包把程序装到 `/usr/bin/disktest`，另外带 `.desktop`、hicolor 图标、
文档与许可证（`/usr/share/doc/disktest`）。
Qt 运行库由发行版提供，fio 用 `PATH` 里的（程序优先找 `<程序目录>/fio/fio`）：

```bash
sudo apt install libqt6charts6 fio          # 只运行；构建才需要 -dev 包

# tar.gz
tar xzf disktest-3.1.0-linux-amd64.tar.gz
./disktest

# deb
sudo apt install ./disktest_3.1.0-1_amd64.deb
disktest
```

无图形界面的机器（服务器 / CI）可以这样验证：

```bash
QT_QPA_PLATFORM=offscreen ./disktest --i18n-check       # 译文覆盖率，PASS 时退出码 0
QT_QPA_PLATFORM=offscreen ./disktest --selftest-parse   # 结果 CSV 解析器自检
```

## 校验和

```
16c97a7133842a071a5b36b7351ffd181e1a171c6d1c994890d854540a4f78ba  disktest_3.1.0-1_amd64.deb
51a9e9d0ec5dc5e953b9841dc88a7ce7dfd24494e63ae3ab51f743ee650376d3  disktest_3.1.0-1_arm64.deb
dfb282313b83d10c0d5fa4014356ffe6c2ec568921a8e2bf4980b04c54014adb  disktest-3.1.0-linux-amd64.tar.gz
b3e017d042fe6d0def6bb273842a9b0d82559de8dca03ce314710233e8b15c96  disktest-3.1.0-linux-arm64.tar.gz
e4a3d12e1f9f473d127735f422dc00d08cfb70035ba3da828f0c052094631d7d  disktest-3.1.0-win-arm64-portable.7z
20e1c91ee44dd21e63ade22eca366ec512d048f7e9a5c225bb3d72473254d7b3  disktest-3.1.0-win-arm64-setup.exe
211fae9bc94f333b69e0fc720d168d23d3d36936c1317b429fa48e1835f9e14c  disktest-3.1.0-win64-portable.7z
d847dca230e3d6c1a497fc64cab5917f806859a6bf230a354260fad25a6be6a7  disktest-3.1.0-win64-single.7z
40526b6f80bf98c0884149135ff2d3cdbabcc97b88241f075eb129ebbcf770fd  disktest-3.1.0-win64-setup.exe
```

## 说明与已知问题

- **安装包没有代码签名**：首次双击可能会碰到 SmartScreen「Windows 已保护你的电脑」，
  点「更多信息 → 仍要运行」即可。
- **Windows ARM64 包不能在 x64 机器上运行**，`build.bat run /arch arm64` 与
  `selftest /arch arm64` 会直接拒绝并提示；验证要拿到 ARM64 机器上做。
- **Windows ARM64 包已在真实 ARM64 机器上验证可正常运行**（2026-11-02）；
  完整「写满整盘 + 逐块校验」的可靠性测试尚未在那台机器上跑过。
- **Windows ARM64 的 fio 是本项目自己编的**（同版本 3.43 源码，未改动）。
  它只依赖系统 DLL，`--enghelp` 的 17 个引擎与 x64 官方版一致；
  但上游从未在 Windows ARM64 上测试过 fio，属于"能跑、已验证基本功能"，
  遇到异常请反馈。CRC 校验在 ARM64 Windows 上走的是软件查表法
  （硬件 CRC 探测只在 Linux/macOS 成立），功能正确、校验环节略慢。
- **精简包只提供 x64 版**（`win64-single.7z`，里面只有 `disktest.exe`）：
  选它要求机器上已有 Qt 6 运行库与 fio。ARM64 只提供全量包——
  ARM64 的 Qt 运行库与原生 fio 都不是系统自带的，精简包在那种机器上没有意义。
- **繁體中文**由 `tools/make_zh_tw.py` 从简体生成（逐字符转换 + 台湾用语表：
  檔案 / 資料 / 網路 / 快取 / 佇列 / 效能 / 輪 …），术语按台湾 Windows 习惯统一，
  但**尚未经母语者全文复核**，措辞欢迎反馈。
- **English** 为人工翻译，术语统一（disk / partition / file system / verify / round …），
  句式仍可打磨。
- `--selftest`、`--selftest-parse` 的**开发者诊断输出**（断言名、逐项结果）保持中文，
  不随界面语言变；自检报告里的字段名是翻译的。
- exe 版本资源里的产品名固定为「磁盘测试By MOONLIGHT」，与界面语言无关。
- **Linux arm64 包**在 x86_64 机器上用 `qemu-aarch64` 跑过 `--version`、
  `--selftest-parse`、`--i18n-check`（均 PASS），**尚未在真机 aarch64 上跑整盘测试**；
  **Linux amd64 包**在 Ubuntu 22.04 上原生跑过同样三项（均 PASS），
  **未跑完整 `--selftest`**（需要一块可写的测试分区）。
- 升级：3.0.8 → 3.1.0 不需要迁移任何数据。结果 CSV 格式未变，
  `2.2.7` 及以上版本保存的结果都能打开。
