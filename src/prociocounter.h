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

#include <QtGlobal>

/// 读取某个进程的累计 I/O 字节数。
///
/// Windows 上用 GetProcessIoCounters()，拿到的是该进程（含其所有线程）
/// 累计的读/写传输字节数。fio 在 Windows 上以线程模式跑测试，所有 I/O 都
/// 发生在同一个进程里，所以按 0.5 s 采样这个计数再差分，就能得到**真正
/// 500 ms 分辨率**的实时读写速度 —— 比解析 fio 自己 1 Hz 的 ETA 输出更细，
/// 也避开了 fio ETA 前 3 秒不输出的空窗期。
///
/// Linux 上等价物是 /proc/<pid>/io：read_bytes / write_bytes 就是块层的
/// 实际传输字节数。/proc/<pid>/io 是**整个线程组**的合计（内核在 tgid
/// 视角上会把每个线程的 ioac 加起来），所以和 Windows 一样，前提是 fio
/// 必须以线程模式跑 —— 这正是 job 命令行里固定带 --thread 的原因
/// （见 FioConfig::commonExtraArguments）。要是哪天把 --thread 去掉，
/// Linux 上 fio 会 fork 出子进程做 I/O，父进程的计数就恒为 0 了。
class ProcessIoCounter
{
public:
    ProcessIoCounter();
    ~ProcessIoCounter();

    ProcessIoCounter(const ProcessIoCounter &) = delete;
    ProcessIoCounter &operator=(const ProcessIoCounter &) = delete;

    /// 按 pid 打开进程并取得查询权限（Linux 上就是记下 pid 并试读一次）。
    bool attach(qint64 processId);
    void detach();
    bool isAttached() const;

    /// 读取累计字节数；返回 false 表示这次读不到（进程已退出 / 无权限）。
    bool read(quint64 *readBytes, quint64 *writeBytes) const;

private:
    void *m_handle = nullptr;   ///< Windows：进程句柄
    qint64 m_pid = -1;          ///< Linux：/proc/<pid>/io 的那个 pid
};
