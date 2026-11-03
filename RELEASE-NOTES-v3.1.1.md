# 磁盘测试 By MOONLIGHT v3.1.1

## 修复

1. **深色模式下漏白的控件补齐**：
   - 「选择磁盘」列表改成深灰底、深蓝选中行 + 白字，表头一起变深；
   - 「浏览…」「浏览目录…」「刷新」「开始/停止」以及结果窗口、消息框的按钮
     不再是在深色背景上刺眼的白底；
   - 卡片标题/数字、目标磁盘说明、状态卡、运行日志、磁盘占用读数、校验失败
     告警条、结果窗口摘要和性能格子全部跟着深浅色走。
2. **运行中切换系统深浅色即时生效，不用重启**：
   - Windows 版监听系统的颜色模式变化，切换时自动换样式表并重刷所有写死的颜色；
   - 已经打开的「测试结果」窗口也会同步刷新，性能格子重新绘制；
   - 磁盘列表的选中行、使用率 95%/85% 的红色/橙色、进度条、日志颜色都会重算。
3. **深色下的可读性**：原来写死的 #333 / #666 灰字、浅红/浅绿读数、浅色告警条
   在深色下都换成了对应的高对比度颜色；浅色模式保持原样。

## 下载

| 平台 | 文件 | 说明 |
| --- | --- | --- |
| Windows x64 | `disktest-3.1.1-setup.exe` | 安装版，推荐 |
| Windows x64 | `disktest-3.1.1-win64-portable.7z` | 绿色版**全量包**：含 Qt / MSVC 运行库与 fio，干净系统解压即用 |
| Windows x64 | `disktest-3.1.1-win64-single.7z` | 绿色版**精简包**：只有本体，机器上需已有 Qt 6 运行库与 fio |
| Windows ARM64 | `disktest-3.1.1-arm64-setup.exe` | 安装版，**ARM 版 Windows 请选这个** |
| Windows ARM64 | `disktest-3.1.1-arm64-portable.7z` | 绿色版**全量包**：含 Qt / MSVC 运行库与**原生 ARM64 的 fio** |
| Linux x86_64 | `disktest-3.1.1-linux-amd64.tar.gz` | 解压即用，依赖系统 Qt 6.2+ 与 fio 3.x |
| Linux aarch64 | `disktest-3.1.1-linux-arm64.tar.gz` | 同上 |
| Ubuntu / Debian amd64 | `disktest_3.1.1-1_amd64.deb` | 安装版：`sudo apt install ./disktest_3.1.1-1_amd64.deb` |
| Ubuntu / Debian arm64 | `disktest_3.1.1-1_arm64.deb` | 同上 |

Linux tar.gz 包内容：`disktest`（未 strip）+ `README.md` + `licenses/` + `docs/images/`；
deb 包把程序装到 `/usr/bin/disktest`，另外带 `.desktop`、hicolor 图标、
文档与许可证（`/usr/share/doc/disktest`）。
Qt 运行库由发行版提供，fio 用 `PATH` 里的（程序优先找 `<程序目录>/fio/fio`）：

```bash
sudo apt install libqt6charts6 fio          # 只运行；构建才需要 -dev 包

# tar.gz
tar xzf disktest-3.1.1-linux-amd64.tar.gz
./disktest

# deb
sudo apt install ./disktest_3.1.1-1_amd64.deb
disktest
```

无图形界面的机器（服务器 / CI）可以这样验证：

```bash
QT_QPA_PLATFORM=offscreen ./disktest --i18n-check       # 译文覆盖率，PASS 时退出码 0
QT_QPA_PLATFORM=offscreen ./disktest --selftest-parse   # 结果 CSV 解析器自检
```

## 校验和

```
d5ee276698bdcff82bbd7b6f62d169073433252dd5cb5ccb630395ce2f293c17  disktest_3.1.1-1_amd64.deb
7e8c83965ea41bf935e7b8eee13932f7e0b06fe8c2e218dad28a242d1f1e235e  disktest_3.1.1-1_arm64.deb
32cd2d32983c28d3a8c457c41a005d810a6bd9ff84d573e2e572a6c870ea9a3a  disktest-3.1.1-arm64-portable.7z
134074aee24dd1a20edfbb21f34f71fd2f931780e0a2495445aa27665751b53b  disktest-3.1.1-arm64-setup.exe
faa13769c38874e83218b1f6707aa351cf4c974d0757a99918a3659ec049a530  disktest-3.1.1-linux-amd64.tar.gz
1eddc86f98c51356ffd4a1209370e8fdefabce8d9e5e9f88d58acc888e324ec3  disktest-3.1.1-linux-arm64.tar.gz
32fbcd07effce1890de4d3572f6300ad21df5e7247b83f0e0f8bb1af9f2e92f4  disktest-3.1.1-setup.exe
30274e48ef2e65767ed723f35a7d6b459c9e98bdd94f3485c587db9b487e75e1  disktest-3.1.1-win64-portable.7z
617cd0a7efc94601faa6b1ba51681d7fda6782a68f8b7d61cb28162092a636fe  disktest-3.1.1-win64-single.7z
```

## 说明与已知问题

- **安装包没有代码签名**：首次双击可能会碰到 SmartScreen「Windows 已保护你的电脑」，
  点「更多信息 → 仍要运行」即可。
- **运行中切换主题**：Windows 版已支持（Qt 6.8 的 `colorSchemeChanged`）；
  Linux 包基于 Ubuntu 22.04 自带的 Qt 6.2.4 构建，没有这个信号，深浅色仍按
  **启动时**的系统配色判断，运行中切换需要重启程序。Linux 的原生深色样式本身
  由发行版主题提供。
- **图表绘图区仍保持白底**：深色下红/蓝曲线在白色绘图区更清楚；如果希望图表也
  跟随深色，可以后续单独调整。
- **Windows ARM64 包不能在 x64 机器上运行**，`build.bat run /arch arm64` 与
  `selftest /arch arm64` 会直接拒绝并提示；验证要拿到 ARM64 机器上做。
- **Windows ARM64 的 fio 是本项目自己编的**（同版本 3.43 源码，未改动）。
  它只依赖系统 DLL，`--enghelp` 的 17 个引擎与 x64 官方版一致；
  但上游从未在 Windows ARM64 上测试过 fio，属于「能跑、已验证基本功能」，
  遇到异常请反馈。CRC 校验在 ARM64 Windows 上走的是软件查表法
  （硬件 CRC 探测只在 Linux/macOS 成立），功能正确、校验环节略慢。
- **精简包只提供 x64 版**（`win64-single.7z`，里面只有 `disktest.exe`）：
  选它要求机器上已有 Qt 6 运行库与 fio。ARM64 只提供全量包。
- **Linux amd64 包**在 Ubuntu 22.04 上原生跑过 `--version`、`--selftest-parse`、
  `--i18n-check`（均 PASS）；**Linux arm64 包**在 x86_64 机器上用
  `qemu-aarch64` 跑过同样三项（均 PASS），**尚未在真机 aarch64 上跑整盘测试**。
- **繁體中文**由 `tools/make_zh_tw.py` 从简体生成，术语按台湾 Windows 习惯统一，
  但**尚未经母语者全文复核**，措辞欢迎反馈。
- 升级：3.1.0 → 3.1.1 不需要迁移任何数据。结果 CSV 格式未变，
  `2.2.7` 及以上版本保存的结果都能打开。
