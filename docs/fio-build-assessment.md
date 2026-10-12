# fio：直接用现成的，还是从源码重新编译？

> **更新（清理后）**：本目录下的 `fio_src/`、`_probe/`、`build-fio-mingw.sh`、`.build-stub/`
> 已被删除，现在只剩 `fio\`（现成的官方 fio-3.43）。下文提到的自编产物与脚本路径均已不存在，
> 但**结论不变**：用现成的那份就够；要重新编时的可复现配方已固化进
> `windows-cpp-toolchain` 技能的 `references/autotools-on-windows.md` §6。

评估日期：2026-10-10　机器：本机（Windows + MinGW-Builds GCC 15.2.0 / Git Bash）
结论先行：**先直接用 `fio\fio.exe`**；它的来源可信、引擎齐全、JSON 可用。只有在你确实需要那几个新提交或要给 fio 打补丁时，才用 `build-fio-mingw.sh` 自己编（本机已验证可编）。

---

## 1. 两份东西到底是什么

| | `fio\fio.exe`（现成） | `fio_src\` 编出来 |
| --- | --- | --- |
| 版本 | **fio-3.43**（官方 release） | **fio-3.43-7-gdaf3f**（上游 master，2026-10-01） |
| 来源可信度 | 与 `C:\Program Files\fio\fio.exe`（官方 MSI 安装）**SHA256 完全一致**：`4415B4B6CB154FE1DE0A12FBB93BCD5FA71CA28FC044676F75E0B447DAC4FC21` | 本机 MinGW 编译，可复现 |
| 大小 | 6,825,043 B | 6,172,549 B |
| 运行库依赖 | `msvcrt.dll`（Win7+ 可用） | UCRT `api-ms-win-crt-*`（Win10+ 自带） |
| IO 引擎 | 17 个：windowsaio / page_fault / exec / filecreate / filestat / filedelete / dircreate / dirstat / dirdelete / ftruncate / net / null / sync / psync / vsync / mmap / cpuio | **完全相同的 17 个** |
| `--output-format=json` | 实测可用（`jobs[].read.iops` 等字段齐全） | 实测可用 |
| 得到它的成本 | 0 | Git Bash + `C:\mingw64` + 3 处绕过（脚本已固化） |

`fio_src` 不是定制分支：`git remote` 就是 `https://github.com/axboe/fio`，HEAD `daf3f3b8` 是上游 master（Vincent Fu 合入的提交），**树里没有任何 HSMR 代码**。所以「编译源码」的收益完全等于 `v3.43..master` 这 7 个提交。

## 2. 源码比现成的多了什么（7 个提交，共 124 行改动）

| 提交 | 在 Windows 上有意义吗 |
| --- | --- |
| `92bf6172 filesetup: don't open files O_RDWR for 100% read jobs` | **有，见 §3 实测** |
| `f726295c engines/http: fix curl_slist leak` | 无（Windows 构建没编 http 引擎） |
| `8445603d fio replay: replay_issue 选项` | 无（blktrace 是 Linux 的） |
| `270b6fb4 examples: add mobile storage test` | 仅多一个示例 job 文件 |
| 另外 3 个是 merge 提交 | 无 |

## 3. 实测差异：只读文件 + `--rw=randrw --rwmixread=100`

同一台机器、同一个只读属性文件、`--ioengine=psync --runtime=2 --time_based`：

| 命令 | 现成 fio-3.43 | 自编 fio-3.43-7 |
| --- | --- | --- |
| `--rw=randrw --rwmixread=100` | ❌ `err=13 Permission denied`（filesetup.c:800），任务失败 | ✅ `err= 0`，241k IOPS |
| `--rw=randwrite`（对照组） | ❌ `err=13` | ❌ `err=13` |

对照组说明：差别确实来自那个修复（"100% 读的混合读写任务不再以 O_RDWR 打开文件"），不是"自己编的更宽松"。
**什么时候会撞上**：只读 U 盘 / 光盘 / 只读分区 / 只读挂载目录上跑 `rwmixread=100` 的混合任务时，现成版本会直接报权限错误。

## 4. 用现成 fio 时必须知道的坑（都是实测，写 GUI 时直接用）

1. **`--output=<file>` 会把 stderr 一起重定向进同一个文件**（`init.c:2771`：`f_err = f_out = tmp;`）。
   实测 `out.json` 第一行是 `fio: this platform does not support process shared mutexes, ...`，于是整个文件不是合法 JSON，`ConvertFrom-Json` 直接报错。
   → **不要用 `--output`**，改为抓 stdout，并从第一个 `{` 开始截取；或加 `--thread` 把那条警告消掉。
2. Windows 上 fio 强制 thread 模式，加 `--thread` 可去掉上面那条警告。
3. **`--client` / `--server` 在这份 Windows 构建上不可用**：`fio --server` 能起来，但 client 用 job 文件时会一直挂住，用命令行参数时报 `fio: connect: No such file or directory` / `failed to connect to 127.0.0.1:8765` 且 exit=1。
   → GUI 一次任务 spawn 一个 fio 进程，别走 client/server。
4. **绝对路径里的盘符冒号会被当成“多文件分隔符”**（这个坑最隐蔽，job 文件和命令行都一样，现成版与自编版行为一致）：`filename` 是用 `:` 分隔多文件的（`options.c:1563` `get_next_str()`），只有 `\:` 才算转义。
   实测 `filename=C:\Users\...\seed.dat` → fio 认为是 **两个文件** `C` 和 `\Users\...\seed.dat`：既在当前目录凭空生成一个名叫 `C` 的文件，又把 `size`/`nrfiles` 平摊到两个目标上（`--size=4k` 时两个文件各 2048 B），报告里 `error` 仍是 0。
   → 写绝对路径时把盘符冒号转义成 `\:`：`filename=C\:\Users\...\seed.dat` 或 `filename=C\:/Users/.../seed.dat`（两种都实测正确：只动目标文件、`error=0`、size 不缩水）；更省事的做法是把进程工作目录设到目标目录、job 里写相对路径。
5. **`windowsaio` 打不开文件时仍可能报 `err= 0` 并给出看似正常的 IOPS**：在只读文件上它刷了约 37,000 行 `windowsaio: failed to open file`，报告却是 `err= 0`、`READ: bw=72.2MiB/s`。现成版与自编版**行为一致**（属于 fio 在 Windows 上的老毛病，与提交无关）。
   → GUI 不能只看 JSON 的 `error` 字段，要同时扫文本/stderr 里的 `failed to open`、`io_u error`、`Permission denied`。

## 5. 要自己编的话（本机已验证）

```bash
bash build-fio-mingw.sh        # 产物：fio_src/fio.exe（6,172,549 B，fio-3.43-7-gdaf3f）
```

脚本里的 3 个绕过点，全部来自本机 MinGW 工具链（`C:\mingw64` = MinGW-Builds 15.2.0 UCRT），不是 fio 的问题：

1. **`clock_gettime` 只在 winpthreads 里**（`libmingwex.a` 没有它）→ configure 的探测直接 `fatal "clock_gettime is not supported"`；用 `LDFLAGS=-lpthread ./configure` 即可通过。
2. fio 的 Makefile 给 CYGWIN 目标加了 **`-lssp`**，本机工具链**没有 libssp** → 最后链接 `cannot find -lssp`；脚本先造一个空 stub `libssp.a` 再 `-L` 过去。
3. Windows 上生成的 **`.d` 依赖文件是 CRLF**，GNU make 会报 `crc/crc32c-arm64.d:19: *** missing separator` → 每次增量构建前 `rm -f *.d */*.d */*/*.d`（上游 CI 也是这么干的）。

首次尝试（不加任何绕过）确实连 configure 都过不去，日志留在 `_probe\build_mingw.log`、`_probe\build_mingw2.log`、`_probe\build_mingw3.log`。

**官方支持的 Windows 构建路径**（供长期维护参考）：Cygwin + `mingw64-x86_64-{gcc-core,headers,runtime,binutils,zlib}`，官方 release 的 MSI 就是这么构建的；上游 CI 另有一条 MSYS2 + **clang** 路线。本机既没有 Cygwin 也没有 MSYS2，所以走的是"Git Bash + MinGW-Builds"这条非官方但已验证可行的路。

## 6. 建议

- **现在**：GUI 先用 `fio\fio.exe`，把 fio 可执行文件路径做成可替换的配置项（不要硬编码版本行为）。
- **需要时**（只读目标、要给 fio 打补丁、想锁定 master 上的修复）：跑 `bash build-fio-mingw.sh` 换成自编的那份，其余代码不用改。
- **如果打算长期自己维护 fio**：装 Cygwin + mingw64-x86_64 工具链按官方路径构建，比在本机 MinGW-Builds 上继续打绕过补丁更稳。

## 7. 本次改动与清理

- `fio_src\` 里现在有构建产物（`config-host.mak`、`config-host.h`、约 116 个 `.o`、`FIO-VERSION-FILE`、`fio.exe`、`t/*.exe`）；`git status` 仍然干净（生成物都在忽略列表里）。要还原成纯源码：`make clean`，或 `git clean -xdf`（后者会连 `fio.exe` 一起删）。
- `_probe\` 下是本次的探针脚本与日志（`build_mingw*.sh/.log`、`cg.c`、`opentest.c`、`cftest.c` 等），可以直接整个删掉。
- 没有修改 fio 的任何源码；`build-fio-mingw.sh` 通过命令行变量传参，不落补丁到仓库。
