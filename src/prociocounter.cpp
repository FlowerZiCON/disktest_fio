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

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

ProcessIoCounter::ProcessIoCounter() = default;

ProcessIoCounter::~ProcessIoCounter()
{
    detach();
}

bool ProcessIoCounter::attach(qint64 processId)
{
    detach();
#ifdef Q_OS_WIN
    if (processId <= 0)
        return false;

    const DWORD pid = static_cast<DWORD>(processId);
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle == nullptr)
        handle = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    m_handle = handle;
    return handle != nullptr;
#else
    Q_UNUSED(processId);
    return false;
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
    m_handle = nullptr;
#endif
}

bool ProcessIoCounter::isAttached() const
{
    return m_handle != nullptr;
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
    Q_UNUSED(readBytes);
    Q_UNUSED(writeBytes);
    return false;
#endif
}
