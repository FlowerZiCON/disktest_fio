# 磁盘测试 By MOONLIGHT v3.0.8

## 新增

1. **图表下方的备注框**：实时速度图表正下方多了一个输入框，未输入时显示
   「请在此输入」，可以随手写一句备注（比如这块盘是从哪台机器拆下来的）。
   - 用「文件 → 保存测试数据…」保存结果时，备注会一起写进 CSV 的参数段：
     `note,<备注文字>`；
   - 用「文件 → 打开测试结果…」回看时，备注显示在**图表正下方**（只读），
     和主界面同一个位置、同一套字号；
   - 备注里**可以有逗号**（参数行按第一个逗号切分）；**不能有换行**，
     写入前会把换行换成空格（参数段是一行一条）；
   - 老文件没有这一项，读出来是空串，结果窗口就不显示备注框；
   - 程序本身**不记住**备注：不写配置 / ini，关掉重开就是空的。
     不保存成 CSV 的话，备注只存在于本次会话。

## 修复

1. **性能随机项的颜色残留到可靠性测试**：点开随机项（黄 / 绿）之后回到可靠性测试，
   新一轮的曲线还会是黄绿色。现在每次清空图表都会把颜色复位成默认红 / 蓝，
   可靠性测试永远是红 / 蓝。
2. **性能测试的平均卡片总是停在最后一项**：点结果表格里某一项时，
   上方的「平均读取 / 平均写入」现在会跟着换成**这一项**的读数，
   不再固定显示最后跑完那一项的数字。

## 下载

| 平台 | 文件 | 说明 |
| --- | --- | --- |
| Windows x64 | `disktest-3.0.8-win64-setup.exe` | 安装版，推荐 |
| Windows x64 | `disktest-3.0.8-win64-full.7z` | 绿色版**全量包**：含 Qt / MSVC 运行库与 fio，干净系统解压即用 |
| Windows x64 | `disktest-3.0.8-win64.7z` | 绿色版**精简包**：只有本体，机器上需已有 Qt 6 运行库与 fio |
| Linux x86_64 | `disktest-3.0.8-linux-amd64.tar.gz` | 解压即用，依赖系统 Qt 6.2+ 与 fio 3.x |
| Linux aarch64 | `disktest-3.0.8-linux-arm64.tar.gz` | 同上 |
| Ubuntu / Debian amd64 | `disktest_3.0.8-1_amd64.deb` | 安装版：`sudo apt install ./disktest_3.0.8-1_amd64.deb` |
| Ubuntu / Debian arm64 | `disktest_3.0.8-1_arm64.deb` | 同上 |

Linux tar.gz 包内容：`disktest`（已 strip）+ `README.md` + `licenses/` + `docs/images/`；
deb 包把程序装到 `/usr/bin/disktest`，另外带 `.desktop`、hicolor 图标、
文档与许可证（`/usr/share/doc/disktest`）。
Qt 运行库由发行版提供，fio 用 `PATH` 里的（程序优先找 `<程序目录>/fio/fio`）：

```bash
sudo apt install libqt6charts6 fio          # 只运行；构建才需要 -dev 包

# tar.gz
tar xzf disktest-3.0.8-linux-amd64.tar.gz
./disktest-3.0.8-linux-amd64/disktest

# deb
sudo apt install ./disktest_3.0.8-1_amd64.deb
disktest
```

无图形界面的机器（服务器 / CI）可以这样验证：

```bash
QT_QPA_PLATFORM=offscreen ./disktest --i18n-check       # 译文覆盖率，PASS 时退出码 0
QT_QPA_PLATFORM=offscreen ./disktest --selftest-parse   # 结果 CSV 解析器自检
```

## 校验和

```
2e23d169ee43727612d27964b3de3a71d62c801d34856bf6bc5b3755a840d13e  disktest-3.0.8-linux-amd64.tar.gz
3fb642b96ba971316048222cedc0f90421c52c67d8d7f79124648083624fc346  disktest-3.0.8-linux-arm64.tar.gz
8caafdb6f93cf44ce370bd30a0d4793c5520146759000b12405d73cd3fb0f9df  disktest_3.0.8-1_amd64.deb
c0645353195843b37cf6f414448cf2c4cc6fe6987cc1b8f15228c1c860f6cd90  disktest_3.0.8-1_arm64.deb
dc386a98eab9dc4e350e25e5cc6f453032ccc037861c99f4bac87542b33337b0  disktest-3.0.8-win64.7z
d81c37515b29206d7624b7368cafcd7768342ba597cc6af397d33a7d26bce23d  disktest-3.0.8-win64-full.7z
5815b3b54b92a6f4853ead7f216ff1ba4ae14ff6bc3fa05a0b5afc3585570448  disktest-3.0.8-win64-setup.exe
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
- **arm64 包**在 x86_64 机器上用 `qemu-aarch64` 跑过 `--version` 与 `--selftest-parse`
  （PASS），**尚未在真机 aarch64 上跑完整盘测试**；amd64 包在 Ubuntu 22.04 上
  原生跑过 `--version`、`--selftest-parse` 和完整 `--selftest`（含 offscreen 下
  用 fio 的读写 + 校验，结论 PASS）。
- 升级：3.0.7 → 3.0.8 不需要迁移任何数据；结果 CSV 格式未变，
  只是参数段多了一条可选的 `note`。`2.2.7` 及以上版本保存的结果都能打开，
  老版本读到多出来的 `note` 会原样忽略（未知参数不参与解析）。
