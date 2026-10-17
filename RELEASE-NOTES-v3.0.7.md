# 磁盘测试 By MOONLIGHT v3.0.7



1. **界面支持三种语言**：简体中文、繁體中文、English。
   - 首次启动**跟随系统语言**：`zh-CN` 系 → 简体中文；`zh-TW` / `zh-HK` / `zh-MO` →
     繁體中文；其它（含英文系统）→ English。
   - **「帮助 → 语言 / Language」**里随时切换。切换会**重启一次程序**；
     **测试运行中不允许切换**，会提示先点「停止」。
   - 命令行：`disktest.exe --lang zh_CN|zh_TW|en|auto`。
     优先级：系统语言 < 上次保存的选择 < `--lang`。

1. **语言设置跟着程序走**：存在 exe 旁边的 `disktest.ini`，整个文件夹拷到别的机器，设置一起带走。那个目录**确实写不进去**时才回退到用户目录
   （`%APPDATA%\disktest` / `~/.config/disktest`）。

2. **译文可以热更新**：把 `i18n/en.tsv`、`i18n/zh_TW.tsv` 放到 exe 目录下的 `i18n\` 里，
   会优先于编进程序的那份——改一行译文不用重新编译。

3. **翻译自检**：`disktest.exe --i18n-check` 检查每种语言的覆盖率、多余的键、
   以及 `%1 %2` 占位符是否与中文原文一致；有问题退出码非 0。

4. **新增Windows 安装包**：
   - 向导支持简体中文 / 繁體中文 / English，`/LANG=` 和静默安装不弹语言选择；
   - 默认「仅为我安装」（`%LOCALAPPDATA%\Programs\Disktest`），也可选「所有用户」
     （`Program Files`）；
   - 开始菜单 / 桌面快捷方式、注册 `App Paths`（Win+R 里直接敲 `disktest`）、
     卸载时清理语言设置。

## 修复

1. **校验失败告警的识别不再依赖中文措辞**。
2. fio 的问题标签表原本在 `main()` 之前就被固化成中文，换语言不生效；改成运行时取值。
3. 性能模式配置摘要里漏在代码里的裸中文并入文案表，不再有翻不到的一行。

## 下载

| 平台 | 文件 | 说明 |
| --- | --- | --- |
| Windows x64 | `disktest-3.0.7-win64-setup.exe` | 安装版，推荐 |
| Windows x64 | `disktest-3.0.7-win64-full.7z` | 绿色版**全量包**：含 Qt / MSVC 运行库与 fio，干净系统解压即用 |
| Windows x64 | `disktest-3.0.7-win64.7z` | 绿色版**精简包**：只有本体，机器上需已有 Qt 6 运行库与 fio |
| Linux x86_64 | `disktest-3.0.7-linux-amd64.tar.gz` | 解压即用，依赖系统 Qt 6.2+ 与 fio 3.x |
| Linux aarch64 | `disktest-3.0.7-linux-arm64.tar.gz` | 同上 |
| Ubuntu / Debian amd64 | `disktest_3.0.7-1_amd64.deb` | 安装版：`sudo apt install ./disktest_3.0.7-1_amd64.deb` |
| Ubuntu / Debian arm64 | `disktest_3.0.7-1_arm64.deb` | 同上 |

Linux 包内容：`disktest`（已 strip）+ `README.md` + `licenses/` + `docs/images/`。
Qt 运行库由发行版提供，fio 用 `PATH` 里的（程序优先找 `<程序目录>/fio/fio`）：

```bash
sudo apt install libqt6charts6 fio          # 只运行；构建才需要 -dev 包
tar xzf disktest-3.0.7-linux-amd64.tar.gz
./disktest-3.0.7-linux-amd64/disktest
```

无图形界面的机器（服务器 / CI）可以这样验证：

```bash
QT_QPA_PLATFORM=offscreen ./disktest --i18n-check    # 译文覆盖率，PASS 时退出码 0
```

## 校验和

```
8eccf5b9cbbeb1d3878abfab2b1e607bd8563ec2011f9dd769b59f31efc192da  disktest-3.0.7-linux-amd64.tar.gz
c446d66d76166fc899fea232879e671613fc6568275de5d6ea9e5f39f7613153  disktest-3.0.7-linux-arm64.tar.gz
8215680a314ee97115b23c7556b0555e75962353a7ecbf0ea5a7d120052b2b21  disktest_3.0.7-1_amd64.deb
4eedb12ac643eb91ad1b6f1870c2ab350324c55b03f4cb820db836a3cbf8e1f4  disktest_3.0.7-1_arm64.deb
ae4c234b6866b634724e7fb47a8c6721b217519e3720fc4adbf2f7e8ba140096 disktest-3.0.7-win64.7z
c1e73d440241293d880559a5daab77a767b5f144d2bd74c82774c806eaa0f5a4 disktest-3.0.7-win64-full.7z
9f252a8474771990c6ec2ad54ea742dfed96d85945b9c4eb19e92a339de4caf1 disktest-3.0.7-win64-setup.7z
```

## 说明与已知问题

- **安装包没有代码签名**：首次双击可能会碰到 SmartScreen「Windows 已保护你的电脑」，
  点「更多信息 → 仍要运行」即可。
- **繁體中文**由 `tools/make_zh_tw.py` 从简体生成（逐字符转换 + 台湾用语表：
  檔案 / 資料 / 網路 / 快取 / 佇列 / 效能 / 輪 …），术语按台湾 Windows 习惯统一，
  但**尚未经母语者全文复核**，措辞欢迎反馈。
- **English** 为人工翻译，术语统一（disk / partition / file system / verify / round …），
  句式仍可打磨。
- `--selftest`、`--selftest-parse` 的**开发者诊断输出**（断言名、逐项结果）保持中文，
  不随界面语言变；自检报告里的字段名是翻译的。
- exe 版本资源里的产品名固定为「磁盘测试By MOONLIGHT」，与界面语言无关。
- **arm64 包**在 x86_64 机器上用 `qemu-aarch64` 跑过 `--version` 与 `--i18n-check`（PASS），
  **尚未在真机 aarch64 上跑完整盘测试**；amd64 包在 Ubuntu 22.04 上原生验证过这两项。
- 升级：3.0.6 → 3.0.7 不需要迁移任何数据；结果 CSV 格式未变，
  `2.2.7` 及以上版本保存的结果都能打开。
