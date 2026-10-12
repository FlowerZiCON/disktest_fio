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

#include "prociocounter.h"

#include <QByteArray>
#include <QString>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <cstring>
#endif

ProcessIoCounter::ProcessIoCounter() = default;

ProcessIoCounter::~ProcessIoCounter()
{
    detach();
}

bool ProcessIoCounter::attach(qint64 processId)
{
    detach();
    if (processId <= 0)
        return false;

#ifdef Q_OS_WIN
    const DWORD pid = static_cast<DWORD>(processId);
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle == nullptr)
        handle = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    m_handle = handle;
    return handle != nullptr;
#else
    // Linux 上没有"打开进程"这回事：每次采样现读 /proc/<pid>/io。
    // 这里记下 pid 之后先试读一次 —— **读得到才算真的能用**：
    // 内核没编 CONFIG_TASK_IO_ACCOUNTING 时文件存在但没有 read_bytes 字段，
    // 那种情况界面该老实报"拿不到计数"，而不是画一条恒为 0 的直线。
    m_pid = processId;
    quint64 probeRead = 0;
    quint64 probeWrite = 0;
    if (!read(&probeRead, &probeWrite)) {
        m_pid = -1;
        return false;
    }
    return true;
#endif
}

void ProcessIoCounter::detach()
{
#ifdef Q_OS_WIN
    if (m_handle != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
#else
    m_pid = -1;
#endif
}

bool ProcessIoCounter::isAttached() const
{
#ifdef Q_OS_WIN
    return m_handle != nullptr;
#else
    return m_pid > 0;
#endif
}

bool ProcessIoCounter::read(quint64 *readBytes, quint64 *writeBytes) const
{
#ifdef Q_OS_WIN
    if (m_handle == nullptr)
        return false;

    IO_COUNTERS counters;
    ZeroMemory(&counters, sizeof(counters));
    if (!::GetProcessIoCounters(static_cast<HANDLE>(m_handle), &counters))
        return false;

    if (readBytes != nullptr)
        *readBytes = static_cast<quint64>(counters.ReadTransferCount);
    if (writeBytes != nullptr)
        *writeBytes = static_cast<quint64>(counters.WriteTransferCount);
    return true;
#else
    if (m_pid <= 0)
        return false;

    // **这里必须用 POSIX 的 open/read，不能用 QFile。**
    // /proc/<pid>/io 的 st_size 恒为 0（procfs 不报大小），而 QFile::atEnd()
    // 对普通文件是按 "pos() >= size()" 判的 —— 于是循环一次都不进、
    // 直接当成"读不到计数"，实时曲线整条链路就废了。
    // 这个问题在 Linux 上实测踩过：attach() 永远返回 false，
    // 日志里刷满"无法读取 fio 进程 I/O 计数"。
    const QByteArray path = QStringLiteral("/proc/%1/io").arg(m_pid).toLocal8Bit();
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;

    // 内容只有一百多字节（7 行），一次读得完
    char buffer[512];
    const ssize_t count = ::read(fd, buffer, sizeof(buffer) - 1);
    ::close(fd);
    if (count <= 0)
        return false;
    buffer[count] = '\0';

    // 每行形如 "read_bytes: 12345"。
    // read_bytes / write_bytes 是**块层实际传输量**，与 Windows 的
    // GetProcessIoCounters 语义一致；rchar / wchar 会把命中页缓存的读写也算进去。
    // 本工具的 job 固定 direct=1（O_DIRECT），两者本该相等，所以只有在
    // 没有 *_bytes 字段（内核没开 I/O 记账）时才回退到 rchar / wchar。
    quint64 blockRead = 0;
    quint64 blockWrite = 0;
    quint64 charRead = 0;
    quint64 charWrite = 0;
    bool hasBlock = false;
    bool hasChar = false;

    const char *cursor = buffer;
    while (*cursor != '\0') {
        const char *eol = ::strchr(cursor, '\n');
        const size_t length = eol ? size_t(eol - cursor) : ::strlen(cursor);
        const char *colon = static_cast<const char *>(::memchr(cursor, ':', length));
        if (colon != nullptr) {
            const QByteArray key(cursor, int(colon - cursor));
            bool ok = false;
            const quint64 value =
                QByteArray(colon + 1, int(length - size_t(colon - cursor) - 1))
                    .trimmed()
                    .toULongLong(&ok);
            if (ok) {
                if (key == QByteArrayLiteral("read_bytes")) {
                    blockRead = value;
                    hasBlock = true;
                } else if (key == QByteArrayLiteral("write_bytes")) {
                    blockWrite = value;
                    hasBlock = true;
                } else if (key == QByteArrayLiteral("rchar")) {
                    charRead = value;
                    hasChar = true;
                } else if (key == QByteArrayLiteral("wchar")) {
                    charWrite = value;
                    hasChar = true;
                }
            }
        }
        if (eol == nullptr)
            break;
        cursor = eol + 1;
    }

    if (!hasBlock && !hasChar)
        return false;

    if (readBytes != nullptr)
        *readBytes = hasBlock ? blockRead : charRead;
    if (writeBytes != nullptr)
        *writeBytes = hasBlock ? blockWrite : charWrite;
    return true;
#endif
}
