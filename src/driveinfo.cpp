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

#include "driveinfo.h"

#include "uitext.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QtGlobal>

#include <algorithm>

#ifndef Q_OS_WIN
#  include <unistd.h>
#endif

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winioctl.h>
#endif

namespace {

QString formatBytes(quint64 bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', unit == 0 ? 0 : 1)
        .arg(QString::fromLatin1(units[unit]));
}

#ifdef Q_OS_WIN

/// 以"不申请任何访问权限"的方式打开一个设备路径；只用于发只读 IOCTL。
HANDLE openDeviceForQuery(const QString &devicePath)
{
    return ::CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()),
                         0, // 不要读也不要写权限
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, 0, nullptr);
}

QString volumeLabel(const QString &rootPath)
{
    const QString path = rootPath.left(2) + QLatin1Char('\\'); // "C:\"
    wchar_t nameBuffer[MAX_PATH + 1] = {0};
    if (!::GetVolumeInformationW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                 nameBuffer, MAX_PATH, nullptr, nullptr, nullptr,
                                 nullptr, 0)) {
        return QString();
    }
    return QString::fromWCharArray(nameBuffer).trimmed();
}

QString volumeFileSystem(const QString &rootPath)
{
    const QString path = rootPath.left(2) + QLatin1Char('\\');
    wchar_t fsBuffer[MAX_PATH + 1] = {0};
    if (!::GetVolumeInformationW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                 nullptr, 0, nullptr, nullptr, nullptr,
                                 fsBuffer, MAX_PATH)) {
        return QString();
    }
    return QString::fromWCharArray(fsBuffer).trimmed();
}

QString driveTypeText(const QString &rootPath)
{
    const QString path = rootPath.left(2) + QLatin1Char('\\');
    switch (::GetDriveTypeW(reinterpret_cast<LPCWSTR>(path.utf16()))) {
    case DRIVE_REMOVABLE: return kDriveTypeRemovable;
    case DRIVE_FIXED:     return kDriveTypeFixed;
    case DRIVE_REMOTE:    return kDriveTypeRemote;
    case DRIVE_CDROM:     return kDriveTypeCdrom;
    case DRIVE_RAMDISK:   return kDriveTypeRamdisk;
    default:              return kDriveTypeUnknown;
    }
}

/// 这块卷落在几号物理磁盘上（拿不到返回 -1）。
int volumeDiskNumber(const QString &rootPath)
{
    const QString devicePath = QStringLiteral("\\\\.\\") + rootPath.left(2);
    HANDLE handle = openDeviceForQuery(devicePath);
    if (handle == INVALID_HANDLE_VALUE)
        return -1;

    // 一个卷可能跨多块盘，给足空间免得 DeviceIoControl 返回 ERROR_MORE_DATA
    QByteArray buffer(int(sizeof(VOLUME_DISK_EXTENTS)) + int(sizeof(DISK_EXTENT)) * 16, 0);
    DWORD returned = 0;
    int diskNumber = -1;
    if (::DeviceIoControl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                          nullptr, 0, buffer.data(), DWORD(buffer.size()),
                          &returned, nullptr)) {
        const auto *extents = reinterpret_cast<const VOLUME_DISK_EXTENTS *>(buffer.constData());
        if (extents->NumberOfDiskExtents > 0)
            diskNumber = int(extents->Extents[0].DiskNumber);
    }
    ::CloseHandle(handle);
    return diskNumber;
}

/// 物理磁盘型号，例如 "Predator SSD GM7000 1TB NVMe"。
QString physicalDiskModel(int diskNumber)
{
    if (diskNumber < 0)
        return QString();

    const QString devicePath = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(diskNumber);
    HANDLE handle = openDeviceForQuery(devicePath);
    if (handle == INVALID_HANDLE_VALUE)
        return QString();

    STORAGE_PROPERTY_QUERY query;
    ZeroMemory(&query, sizeof(query));
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    QByteArray buffer(2048, 0);
    DWORD returned = 0;
    QString model;
    if (::DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY,
                          &query, sizeof(query),
                          buffer.data(), DWORD(buffer.size()),
                          &returned, nullptr)) {
        const auto *descriptor =
            reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR *>(buffer.constData());

        const auto readString = [&buffer](DWORD offset) -> QString {
            if (offset == 0 || offset >= DWORD(buffer.size()))
                return QString();
            const char *start = buffer.constData() + offset;
            const int maxLength = int(buffer.size() - offset);
            return QString::fromLatin1(start, int(qstrnlen(start, maxLength))).trimmed();
        };

        const QString vendor = readString(descriptor->VendorIdOffset);
        const QString product = readString(descriptor->ProductIdOffset);
        model = QStringList{vendor, product}.join(QLatin1Char(' ')).simplified();
    }
    ::CloseHandle(handle);
    return model;
}

#endif // Q_OS_WIN

} // namespace

double VolumeEntry::usedPercent() const
{
    if (bytesTotal == 0)
        return 0.0;
    return double(bytesUsed()) * 100.0 / double(bytesTotal);
}

QString VolumeEntry::displayLine() const
{
    return kVolumeRowLine
        .arg(letter, deviceName, formatBytes(bytesUsed()), formatBytes(bytesTotal))
        .arg(usedPercent(), 0, 'f', 1);
}

QList<VolumeEntry> enumerateVolumes()
{    QList<VolumeEntry> entries;

    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    std::sort(volumes.begin(), volumes.end(), [](const QStorageInfo &a, const QStorageInfo &b) {
        return a.rootPath() < b.rootPath();
    });

    for (const QStorageInfo &volume : volumes) {
        if (!volume.isValid() || !volume.isReady())
            continue;
        if (volume.bytesTotal() <= 0)
            continue;

        VolumeEntry entry;
        entry.rootPath = volume.rootPath();
        entry.letter = entry.rootPath.left(2);
        entry.bytesTotal = quint64(volume.bytesTotal());
        entry.bytesFree = quint64(volume.bytesAvailable());

#ifdef Q_OS_WIN
        entry.label = volumeLabel(entry.rootPath);
        entry.fileSystem = volumeFileSystem(entry.rootPath);
        entry.typeText = driveTypeText(entry.rootPath);
        entry.physicalModel = physicalDiskModel(volumeDiskNumber(entry.rootPath));
#else
        entry.label = volume.displayName();
        entry.fileSystem = volume.fileSystemType();
        entry.typeText = kDriveTypeVolume;
#endif

        // "设备名称"首选物理磁盘型号 —— 卷标在很多机器上根本是空的，
        // 而型号才能真正区分是哪块盘（这本来就是个测盘的工具）。
        if (!entry.physicalModel.isEmpty())
            entry.deviceName = entry.physicalModel;
        else if (!entry.label.isEmpty())
            entry.deviceName = entry.label;
        else
            entry.deviceName = entry.typeText;

        entries.append(entry);
    }

    return entries;
}

bool queryVolumeSpace(const QString &rootPath, quint64 *bytesTotal, quint64 *bytesFree)
{
#ifdef Q_OS_WIN
    QString path = rootPath;
    if (!path.endsWith(QLatin1Char('\\')) && !path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('\\');

    ULARGE_INTEGER freeForCaller;
    ULARGE_INTEGER total;
    ULARGE_INTEGER totalFree;
    if (!::GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(path.utf16()),
                               &freeForCaller, &total, &totalFree)) {
        return false;
    }
    if (bytesTotal)
        *bytesTotal = total.QuadPart;
    // 用"调用者可用"而不是"卷上空闲"：配额等因素也要算进去
    if (bytesFree)
        *bytesFree = freeForCaller.QuadPart;
    return true;
#else
    const QStorageInfo info(rootPath);
    if (!info.isValid() || !info.isReady())
        return false;
    if (bytesTotal)
        *bytesTotal = quint64(info.bytesTotal());
    if (bytesFree)
        *bytesFree = quint64(info.bytesAvailable());
    return true;
#endif
}

TestFileSet scanTestFiles(const QString &folder)
{
    TestFileSet set;
    if (folder.isEmpty())
        return set;

    QDir dir(folder);
    if (!dir.exists())
        return set;

    const QFileInfoList entries = dir.entryInfoList(
        QStringList{QStringLiteral("testfile_*")},
        QDir::Files | QDir::NoSymLinks, QDir::Name);

    for (const QFileInfo &info : entries) {
        set.names << info.fileName();
        set.sizes << info.size();
        set.totalBytes += info.size();
        ++set.count;
    }
    return set;
}

bool isProcessElevated()
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation = {};
    DWORD returned = 0;
    const BOOL ok = ::GetTokenInformation(token, TokenElevation, &elevation,
                                          sizeof(elevation), &returned);
    ::CloseHandle(token);
    return ok != FALSE && elevation.TokenIsElevated != 0;
#else
    return ::geteuid() == 0;
#endif
}

QString queryFileSystem(const QString &rootPath)
{
    if (rootPath.isEmpty())
        return QString();

    QString native = QDir::toNativeSeparators(rootPath);
    if (!native.endsWith(QLatin1Char('\\')))
        native += QLatin1Char('\\');

    wchar_t fsName[MAX_PATH + 1] = {};
    const std::wstring wide = native.toStdWString();
    if (!GetVolumeInformationW(wide.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
                               fsName, MAX_PATH + 1)) {
        return QString();
    }
    return QString::fromWCharArray(fsName);
}