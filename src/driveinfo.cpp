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
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QtGlobal>

#include <algorithm>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winioctl.h>
#else
// Linux：sameFileSystem 用 stat 的 st_dev，suggestTestDir 要试建目录，
// supportsDirectIo 用 O_DIRECT 探针
#  include <sys/stat.h>
#  include <unistd.h>
#  include <cerrno>
#  include <cstdlib>
#  include <cstring>
#  include <fcntl.h>
#endif

namespace {

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

/// 这个卷落在几号物理磁盘上（拿不到返回 -1）。
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

/// 给一个文件系统条目补上"容量 / 剩余 / 建议测试目录"三件事。
void fillFileSystemFacts(FileSystemEntry *entry)
{
    const QStorageInfo info(entry->mountPoint);
    if (info.isValid() && info.isReady()) {
        entry->bytesTotal = quint64(info.bytesTotal());
        entry->bytesFree = quint64(info.bytesAvailable());
        entry->readOnly = entry->readOnly || info.isReadOnly();
    }

    if (entry->readOnly) {
        entry->unavailableReason = kFsReadOnly;
        return;
    }
    // 网络文件系统（nfs / cifs）在这一层是**照给目录**的：它照样能测，
    // 只是测的是网络不是盘，界面会另外挂一句提醒（见 kFsNetworkWarning）。
    // 以前这里写成 if (!isNetwork) {...} else {...}，两个分支内容一模一样，
    // 纯属噪音（也让人以为两种情况会走不同的逻辑）。
    entry->suggestedDir = suggestTestDir(entry->mountPoint, &entry->unavailableReason);
}

QList<DiskEntry> disksFromWindowsVolumes(QStringList *ignored)
{
    QList<DiskEntry> disks;
    QList<int> diskNumbers;   // 与 disks 一一对应

    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    std::sort(volumes.begin(), volumes.end(), [](const QStorageInfo &a, const QStorageInfo &b) {
        return a.rootPath() < b.rootPath();
    });

    for (const QStorageInfo &volume : volumes) {
        if (!volume.isValid() || !volume.isReady())
            continue;
        if (volume.bytesTotal() <= 0)
            continue;

        const QString rootPath = volume.rootPath();

        // ---- 网络驱动器 / 虚拟磁盘：任何权限下都不列 ----
        //
        // 判据一：Win32 的驱动器类型。映射的网络驱动器就是 DRIVE_REMOTE；
        //         内存盘（DRIVE_RAMDISK）也不是"一块盘"。
        // 判据二：拿不到物理磁盘号。\\.\X: 不响应
        //         IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS 的，就不是挂在某块物理盘上的
        //         本地卷 —— 映射盘、subst 之类都落在这条里。
        //
        // 为什么必须"两边都不列"：这类卷在普通权限下会出现在列表里却完全不可用
        // （建目录报 ERROR_INVALID_OWNER / 拒绝访问），提权之后又消失，
        // 同一个东西两种表现，怎么解释都是 bug。
        QString why;
        int diskNumber = -1;
        {
            const QString root = rootPath.left(2) + QLatin1Char('\\');   // "X:\"
            const UINT driveType =
                ::GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()));
            if (driveType == DRIVE_REMOTE) {
                why = kVolumeWhyNetwork;
            } else if (driveType == DRIVE_RAMDISK) {
                why = kVolumeWhyRamdisk;
            } else if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_UNKNOWN) {
                why = kVolumeWhyNoRoot;
            } else {
                // **只问一次**：volumeDiskNumber() 是 CreateFile + DeviceIoControl
                // 两次内核往返。以前上面问一次、下面又问一次，每个卷白跑一遍。
                diskNumber = volumeDiskNumber(rootPath);
                if (diskNumber < 0)
                    why = kVolumeWhyNoPhysicalDisk;
            }
        }
        if (!why.isEmpty()) {
            if (ignored)
                *ignored << kIgnoredVolumeFmt.arg(rootPath.left(2), why);
            continue;
        }

        // 到这里 diskNumber 一定 >= 0：为负的那些上面已经 continue 掉了。
        // （以前这里还留着一个 ?: 兜底，那一支永远走不到。）
        int index = diskNumbers.indexOf(diskNumber);
        if (index < 0) {
            DiskEntry disk;
            disk.kernelName = QStringLiteral("PhysicalDrive%1").arg(diskNumber);
            disk.model = physicalDiskModel(diskNumber);
            disks.append(disk);
            diskNumbers.append(diskNumber);
            index = disks.size() - 1;
        }

        FileSystemEntry entry;
        entry.kernelName = rootPath.left(2);          // "C:"
        entry.device = entry.kernelName;
        entry.mountPoint = rootPath;
        entry.fileSystem = volumeFileSystem(rootPath);
        entry.label = volumeLabel(rootPath);
        entry.readOnly = volume.isReadOnly();
        fillFileSystemFacts(&entry);

        disks[index].bytesTotal += entry.bytesTotal;
        disks[index].fileSystems.append(entry);
    }

    const QString systemRoot = QDir::rootPath();
    for (DiskEntry &disk : disks) {
        for (const FileSystemEntry &fs : disk.fileSystems) {
            if (fs.mountPoint.compare(systemRoot, Qt::CaseInsensitive) == 0)
                disk.isSystemDisk = true;
        }
    }
    return disks;
}

#else // !Q_OS_WIN

/// 读一个 /sys 属性文件（不存在或读不到就返回空串）。
QString readSysFs(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll()).trimmed();
}

/// Linux 上的物理磁盘型号，例如 "Samsung SSD 860 EVO 500GB"。
///
/// 输入是设备节点名或路径（"sda1" / "/dev/nvme0n1p2"）。
/// 分区节点在 /sys/block 下没有 device/ 目录，得先退到整盘：
///   sda1     → sda
///   nvme0n1p2→ nvme0n1p → nvme0n1
///   mmcblk0p1→ mmcblk0p → mmcblk0
/// 做法是不断砍掉尾部的数字和分隔用的 'p'，每砍一次就去 /sys 里问一次，
/// 问到就停。全读不到就返回空串（界面会退回到内核名）。
QString linuxPhysicalModel(const QString &device)
{
    QString name = device.startsWith(QLatin1String("/dev/"))
                       ? QFileInfo(device).fileName()
                       : device;
    for (int attempt = 0; attempt < 4 && !name.isEmpty(); ++attempt) {
        const QString base = QStringLiteral("/sys/block/%1/device/").arg(name);
        const QString vendor = readSysFs(base + QStringLiteral("vendor"));
        const QString model = readSysFs(base + QStringLiteral("model"));
        if (!model.isEmpty() || !vendor.isEmpty())
            return QStringList{vendor, model}.join(QLatin1Char(' ')).simplified();

        // 退一级：去掉尾部的数字，以及 nvme/mmcblk 用来分隔分区的那个 'p'
        int cut = name.size();
        while (cut > 0 && name.at(cut - 1).isDigit())
            --cut;
        if (cut > 0 && name.at(cut - 1) == QLatin1Char('p'))
            --cut;
        if (cut <= 0 || cut == name.size())
            break;
        name = name.left(cut);
    }
    return QString();
}

QString jsonString(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    // 同一个设备挂载在多个位置时，有的 lsblk 版本给的是数组
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            if (item.isString() && !item.toString().isEmpty())
                return item.toString();
        }
    }
    return QString();
}

bool jsonBool(const QJsonValue &value, bool fallback = false)
{
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toInt() != 0;
    return fallback;
}

quint64 jsonBytes(const QJsonValue &value)
{
    if (value.isDouble())
        return quint64(value.toDouble());
    if (value.isString())
        return value.toString().toULongLong();
    return 0;
}

/// 递归收集一棵 lsblk 子树里的文件系统。层次可能是
///   sda → sda3 → vg-root（LVM）
/// 这种三层，所以必须递归，不能只看一层。
void collectFileSystems(const QJsonObject &node, DiskEntry *disk, bool *sawRoot)
{
    const QString fsType = jsonString(node.value(QStringLiteral("fstype")));
    // swap 没有文件、放不了测试文件，直接不列
    if (!fsType.isEmpty() && fsType != QLatin1String("swap")) {
        FileSystemEntry entry;
        entry.kernelName = node.value(QStringLiteral("name")).toString();
        entry.device = QStringLiteral("/dev/") + entry.kernelName;
        entry.fileSystem = fsType;
        entry.mountPoint = jsonString(node.value(QStringLiteral("mountpoint")));
        entry.label = jsonString(node.value(QStringLiteral("label")));
        entry.readOnly = jsonBool(node.value(QStringLiteral("ro")));

        if (entry.mountPoint.isEmpty()) {
            // 未挂载：文件模式够不着它，但列出来能解释"为什么这块分区测不了"
            entry.bytesTotal = jsonBytes(node.value(QStringLiteral("size")));
            entry.unavailableReason = kFsNotMounted;
        } else {
            if (entry.mountPoint == QLatin1String("/"))
                *sawRoot = true;
            const QStorageInfo info(entry.mountPoint);
            if (info.isValid() && info.isReady()) {
                entry.bytesTotal = quint64(info.bytesTotal());
                entry.bytesFree = quint64(info.bytesAvailable());
                entry.readOnly = entry.readOnly || info.isReadOnly();
            }
            entry.isNetwork = isNetworkFileSystem(entry.fileSystem);
            if (entry.readOnly)
                entry.unavailableReason = kFsReadOnly;
            else
                entry.suggestedDir = suggestTestDir(entry.mountPoint, &entry.unavailableReason);
        }
        disk->fileSystems.append(entry);
    }

    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children)
        collectFileSystems(child.toObject(), disk, sawRoot);
}

QList<DiskEntry> disksFromLsblk(const QString &json, QStringList *ignored)
{
    QList<DiskEntry> disks;

    const QJsonArray devices =
        QJsonDocument::fromJson(json.toUtf8()).object()
            .value(QStringLiteral("blockdevices")).toArray();

    for (const QJsonValue &value : devices) {
        const QJsonObject node = value.toObject();
        const QString type = node.value(QStringLiteral("type")).toString();

        // 光驱：只读，测不了
        if (type == QLatin1String("rom"))
            continue;

        // 回环设备上的只读 squashfs（snap 快照）既不是盘也不是可测目标。
        // 一台机器上能有十几个，全是噪音 —— 直接不列。
        // 但用户自己用 losetup 挂的 ext4 回环要留着（那正是"拿镜像当盘测"的用法）。
        if (type == QLatin1String("loop")
            && (jsonBool(node.value(QStringLiteral("ro")))
                || jsonString(node.value(QStringLiteral("fstype"))) == QLatin1String("squashfs"))) {
            if (ignored) {
                *ignored << kIgnoredVolumeFmt.arg(
                    node.value(QStringLiteral("name")).toString(),
                    jsonString(node.value(QStringLiteral("fstype"))));
            }
            continue;
        }

        DiskEntry disk;
        disk.kernelName = node.value(QStringLiteral("name")).toString();
        disk.bytesTotal = jsonBytes(node.value(QStringLiteral("size")));
        disk.isVirtual = type == QLatin1String("loop") || type == QLatin1String("lvm")
                         || type == QLatin1String("crypt")
                         || type.startsWith(QLatin1String("raid"));

        const QString vendor = jsonString(node.value(QStringLiteral("vendor"))).trimmed();
        QString model = jsonString(node.value(QStringLiteral("model"))).trimmed();
        if (model.isEmpty()) {
            model = linuxPhysicalModel(disk.kernelName);
        } else if (!vendor.isEmpty() && !model.contains(vendor, Qt::CaseInsensitive)) {
            model = vendor + QLatin1Char(' ') + model;
        }
        disk.model = model.simplified();

        bool sawRoot = false;
        collectFileSystems(node, &disk, &sawRoot);

        // 一个文件系统都没有的块设备（裸盘、swap、未格式化的分区）
        // 文件模式测不了，不占列表位置。
        if (disk.fileSystems.isEmpty())
            continue;

        disk.isSystemDisk = sawRoot;
        disks.append(disk);
    }
    return disks;
}

/// lsblk 不可用时的兜底：一个挂载点当"一块盘"。
/// 丢掉"哪块物理盘"这一层，但至少还能选、还能测。
QList<DiskEntry> disksFromMountedVolumes(QStringList *ignored)
{
    QList<DiskEntry> disks;

    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    std::sort(volumes.begin(), volumes.end(), [](const QStorageInfo &a, const QStorageInfo &b) {
        return a.rootPath() < b.rootPath();
    });

    for (const QStorageInfo &volume : volumes) {
        if (!volume.isValid() || !volume.isReady())
            continue;
        if (volume.bytesTotal() <= 0)
            continue;

        // 没有块设备的伪文件系统不是"盘"，列出来只会淹没有用的信息；
        // 网络文件系统（nfs / cifs / sshfs）测的是网络不是盘，同样不列。
        const QString fsType = volume.fileSystemType().toLower();
        if (isNetworkFileSystem(fsType)) {
            if (ignored)
                *ignored << kIgnoredVolumeFmt.arg(volume.rootPath(), kVolumeWhyNetwork);
            continue;
        }
        if (fsType == QLatin1String("tmpfs") || fsType == QLatin1String("devtmpfs")
            || fsType == QLatin1String("squashfs") || fsType == QLatin1String("proc")
            || fsType == QLatin1String("sysfs") || fsType == QLatin1String("cgroup2")
            || fsType == QLatin1String("overlay") || fsType == QLatin1String("ramfs")) {
            continue;
        }

        DiskEntry disk;
        disk.kernelName = volume.device().isEmpty() ? volume.rootPath() : volume.device();
        disk.model = linuxPhysicalModel(volume.device());
        disk.bytesTotal = quint64(volume.bytesTotal());
        disk.isSystemDisk = volume.rootPath() == QLatin1String("/");

        FileSystemEntry entry;
        entry.kernelName = volume.device().isEmpty() ? volume.rootPath() : volume.device();
        entry.device = volume.device();
        entry.mountPoint = volume.rootPath();
        entry.fileSystem = volume.fileSystemType();
        entry.label = volume.displayName();
        entry.bytesTotal = quint64(volume.bytesTotal());
        entry.bytesFree = quint64(volume.bytesAvailable());
        entry.readOnly = volume.isReadOnly();
        entry.isNetwork = isNetworkFileSystem(entry.fileSystem);
        if (entry.readOnly)
            entry.unavailableReason = kFsReadOnly;
        else
            entry.suggestedDir = suggestTestDir(entry.mountPoint, &entry.unavailableReason);

        disk.fileSystems.append(entry);
        disks.append(disk);
    }
    return disks;
}

#endif // Q_OS_WIN

} // namespace

double FileSystemEntry::usedPercent() const
{
    if (bytesTotal == 0)
        return 0.0;
    return double(bytesUsed()) * 100.0 / double(bytesTotal);
}

QString FileSystemEntry::displayLine() const
{
    const QString where = mountPoint.isEmpty() ? kFsNotMounted : mountPoint;
    return kFsRowLine.arg(kernelName, fileSystem, where);
}

QString FileSystemEntry::fsLine() const
{
    // 只给文件系统；拿不到类型才退回设备名，免得整行空白
    return fileSystem.isEmpty() ? kernelName : fileSystem;
}

bool DiskEntry::testable() const
{
    for (const FileSystemEntry &fs : fileSystems) {
        if (fs.testable())
            return true;
    }
    return false;
}

QString DiskEntry::nameLine() const
{
    if (model.isEmpty())
        return kernelName;
    return kernelName + QStringLiteral("  ") + model;
}

QString DiskEntry::modelLine() const
{
    // 型号是这一行唯一该显示的东西；没有型号才退回内核名，免得整行空白
    return model.isEmpty() ? kernelName : model;
}

QString DiskEntry::displayLine() const
{
    QString line = nameLine();
    if (isSystemDisk)
        line += QLatin1Char(' ') + kDiskSystemTag;
    if (isVirtual)
        line += QLatin1Char(' ') + kDiskVirtualTag;
    return line;
}

QList<DiskEntry> enumerateDisks(QStringList *ignored)
{
    if (ignored)
        ignored->clear();

#ifdef Q_OS_WIN
    return disksFromWindowsVolumes(ignored);
#else
    // 首选 lsblk：一次调用就能拿到"磁盘 → 分区 → 文件系统 → 挂载点"的完整层次，
    // 还能区分真盘和回环、知道哪些是只读的。util-linux 从 2.27 起就有 -J。
    QProcess lsblk;
    lsblk.start(QStringLiteral("lsblk"),
                QStringList{QStringLiteral("-J"), QStringLiteral("-b"),
                            QStringLiteral("-o"),
                            QStringLiteral("NAME,TYPE,SIZE,MODEL,VENDOR,FSTYPE,"
                                           "MOUNTPOINT,LABEL,RO")});
    if (lsblk.waitForStarted(2000) && lsblk.waitForFinished(4000)) {
        QList<DiskEntry> disks =
            disksFromLsblk(QString::fromUtf8(lsblk.readAllStandardOutput()), ignored);
        if (!disks.isEmpty()) {
            // 系统盘排最前（用户最可能想测它），其余按内核名排
            std::stable_sort(disks.begin(), disks.end(), [](const DiskEntry &a, const DiskEntry &b) {
                if (a.isSystemDisk != b.isSystemDisk)
                    return a.isSystemDisk;
                return a.kernelName < b.kernelName;
            });
            return disks;
        }
    }
    return disksFromMountedVolumes(ignored);
#endif
}

QString suggestTestDir(const QString &mountPoint, QString *reason)
{
    const auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return QString();
    };

    if (mountPoint.isEmpty())
        return fail(kFsNotMounted);

    QStringList candidates;
    candidates << mountPoint;
    const QString home = QDir::homePath();
    if (!home.isEmpty() && home != mountPoint)
        candidates << home;
#ifndef Q_OS_WIN
    // Windows 上没有这种"全局可写目录"，到用户目录为止
    candidates << QStringLiteral("/var/tmp") << QStringLiteral("/tmp");
#endif

    for (const QString &candidate : candidates) {
        // 必须和目标是**同一个文件系统**，否则写进去测的就是另一个盘了
        if (!sameFileSystem(candidate, mountPoint))
            continue;
        if (!QFileInfo(candidate).isDir())
            continue;
        if (!canUseAsTestDir(candidate))
            continue;
        return QDir::cleanPath(candidate);
    }
    return fail(kFsNoWritableDir);
}

bool sameFileSystem(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;

#ifdef Q_OS_WIN
    const QString rootA = QStorageInfo(a).rootPath();
    const QString rootB = QStorageInfo(b).rootPath();
    return !rootA.isEmpty() && rootA.compare(rootB, Qt::CaseInsensitive) == 0;
#else
    struct stat statA;
    struct stat statB;
    if (::stat(QFile::encodeName(a).constData(), &statA) != 0)
        return false;
    if (::stat(QFile::encodeName(b).constData(), &statB) != 0)
        return false;
    return statA.st_dev == statB.st_dev;
#endif
}

bool isNetworkFileSystem(const QString &fsType)
{
    const QString t = fsType.trimmed().toLower();
    return t.startsWith(QLatin1String("nfs"))
        || t == QLatin1String("cifs") || t == QLatin1String("smbfs")
        || t == QLatin1String("smb3") || t == QLatin1String("fuse.sshfs")
        || t == QLatin1String("sshfs") || t == QLatin1String("afs")
        || t == QLatin1String("9p") || t == QLatin1String("ceph")
        || t == QLatin1String("glusterfs");
}

bool canUseAsTestDir(const QString &dir)
{
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return false;
    // **不能用 QFileInfo::isWritable()**：Windows 上它只看文件的只读属性，
    // "C:\" 会被判成可写，实际 mkdir 是 Access denied（非提权进程建不了）。
    // 所以这里用 QTemporaryDir 真建一个再让它自己删掉。
    QTemporaryDir probe(QDir(dir).absoluteFilePath(QStringLiteral(".qtfiogui_probe_XXXXXX")));
    return probe.isValid();
}

bool queryVolumeSpace(const QString &path, quint64 *bytesTotal, quint64 *bytesFree)
{
    if (path.isEmpty())
        return false;

#ifdef Q_OS_WIN
    QString native = path;
    if (!native.endsWith(QLatin1Char('\\')) && !native.endsWith(QLatin1Char('/')))
        native += QLatin1Char('\\');

    ULARGE_INTEGER freeForCaller;
    ULARGE_INTEGER total;
    ULARGE_INTEGER totalFree;
    if (!::GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(native.utf16()),
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
    const QStorageInfo info(path);
    if (!info.isValid() || !info.isReady())
        return false;
    if (bytesTotal)
        *bytesTotal = quint64(info.bytesTotal());
    if (bytesFree)
        *bytesFree = quint64(info.bytesAvailable());
    return true;
#endif
}

QString queryFileSystem(const QString &path)
{
    if (path.isEmpty())
        return QString();

#ifdef Q_OS_WIN
    QString native = QDir::toNativeSeparators(path);
    if (!native.endsWith(QLatin1Char('\\')))
        native += QLatin1Char('\\');

    wchar_t fsName[MAX_PATH + 1] = {};
    const std::wstring wide = native.toStdWString();
    if (!GetVolumeInformationW(wide.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
                               fsName, MAX_PATH + 1)) {
        return QString();
    }
    return QString::fromWCharArray(fsName);
#else
    // Linux 上 GetVolumeInformation 不存在，等价的信息在 QStorageInfo 里：
    // fileSystemType() 返回 statfs 的类型名（"ext4" / "vfat" / "exfat" / "btrfs" …）。
    const QStorageInfo info(path);
    if (!info.isValid() || !info.isReady())
        return QString();
    return info.fileSystemType();
#endif
}

bool isFatFileSystem(const QString &fsName)
{
    const QString fs = fsName.trimmed().toLower();
    if (fs.isEmpty())
        return false;

    // Windows: "fat" / "fat32"（GetVolumeInformation 的返回）
    if (fs.startsWith(QLatin1String("fat")))
        return true;
    // Linux: statfs 的 type 名
    return fs == QLatin1String("vfat") || fs == QLatin1String("msdos");
    // 注意这里**故意不用 contains("fat")**：Linux 上 exFAT 报的是 "exfat"，
    // 含 "fat" 但它没有 4 GiB 单文件上限，误拦会让大容量 exFAT 盘跑不了性能测试。
}

bool supportsDirectIo(const QString &folder, QString *error)
{
#ifdef Q_OS_WIN
    // Windows 上走 windowsaio，能不能直接 I/O 由 fio 自己判断，
    // 这里不做预检（也不该在别人的目录里凭空建探针文件）。
    Q_UNUSED(folder);
    Q_UNUSED(error);
    return true;
#else
    if (folder.isEmpty()) {
        if (error)
            *error = QStringLiteral("测试目录为空");
        return false;
    }

    const QString path = QDir(folder).absoluteFilePath(
        QStringLiteral(".qtfiogui_odirect_probe"));
    const QByteArray native = QFile::encodeName(path);

    // O_DIRECT 要求缓冲区地址、长度、文件偏移都按逻辑块大小对齐。
    // 512 是最小值，但有些设备/文件系统要 4 KiB，这里直接按 4 KiB 对齐。
    constexpr size_t kAlign = 4096;
    const int fd = ::open(native.constData(),
                          O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0600);
    if (fd < 0) {
        if (error)
            *error = QString::fromLocal8Bit(::strerror(errno));
        return false;
    }

    void *buffer = nullptr;
    bool ok = false;
    int savedErrno = 0;
    if (::posix_memalign(&buffer, kAlign, kAlign) == 0) {
        ::memset(buffer, 0, kAlign);
        const ssize_t written = ::write(fd, buffer, kAlign);
        savedErrno = errno;
        ok = (written == ssize_t(kAlign));
        ::free(buffer);
    } else {
        ok = true;   // 内存对齐失败是本地问题，不该判成"文件系统不支持"
    }
    ::close(fd);
    ::unlink(native.constData());

    if (!ok && error)
        *error = QString::fromLocal8Bit(::strerror(savedErrno));
    return ok;
#endif
}

QString testFilePrefix()
{
    return QStringLiteral("testfile_");
}

bool isTestFileName(const QString &name)
{
    if (!name.startsWith(testFilePrefix()))
        return false;
    const QString suffix = name.mid(testFilePrefix().size());
    if (suffix.isEmpty())
        return false;
    // 纯数字后缀（job 里是 filename_format=testfile_$filenum）。
    // 按整数解析来判断，不依赖 glob 的字符类支持。
    for (const QChar c : suffix) {
        if (!c.isDigit())
            return false;
    }
    return true;
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
        QStringList{QStringLiteral("testfile_*"), QStringLiteral("*.state")},
        QDir::Files | QDir::NoSymLinks, QDir::Name);

    for (const QFileInfo &info : entries) {
        const QString name = info.fileName();
        if (isTestFileName(name)) {
            set.names << name;
            set.sizes << info.size();
            set.totalBytes += info.size();
            ++set.count;
            continue;
        }
        // fio 的校验状态文件：实测叫 local-<job>-<n>-verify.state，
        // 和 testfile_* 完全不沾边，但确实落在测试目录里，清理时要删。
        //
        // 判据必须是"以 .state 结尾"这一条。**别把"没匹配上测试文件"当成
        // "那就是状态文件"**：那样任何第三个文件（testfile_backup.dat、
        // 用户的说明文件…）都会被归进"要删的"那一类。实测踩过这个坑：
        // 清理路径会照着这个分类删文件。
        if (name.endsWith(QStringLiteral(".state"), Qt::CaseInsensitive)) {
            set.stateNames << name;
            set.stateBytes += info.size();
        }
        // 其它文件一律**不归类、不动**：既不是我们生成的，也不是 fio 留下的。
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
