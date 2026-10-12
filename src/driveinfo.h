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

#include <QList>
#include <QString>

/// 一个能被"文件模式"测试的文件系统。
///
/// Linux 上就是一个已挂载的分区（也可能是 LVM / RAID / 回环上的文件系统）；
/// Windows 上就是一个有盘符的卷。**两个平台统一按"文件系统"这一层来选** ——
/// 因为本工具只能测文件系统，裸设备一律拒绝（见 FioConfig::looksLikeRawDevice）。
struct FileSystemEntry
{
    QString device;         ///< "/dev/nvme0n1p2"；Windows 上是 "C:"
    QString kernelName;     ///< "nvme0n1p2"；Windows 上是 "C:"
    QString fileSystem;     ///< "ext4" / "vfat" / "NTFS"
    QString mountPoint;     ///< "/" / "/home"；Windows 上是 "C:/"；空 = 未挂载
    QString label;          ///< 卷标，常常是空的
    quint64 bytesTotal = 0;
    quint64 bytesFree  = 0;
    bool    readOnly   = false;
    bool    isNetwork  = false;

    /// 这个文件系统上"可以放 ml_disktest 的可写目录"。
    /// 空 = 文件模式测不了，原因在 unavailableReason（"只读挂载" / "未挂载" / "没有可写目录"）。
    ///
    /// **为什么不是一个挂载点而是一个目录**：挂载点的根目录经常只有 root 能写，
    /// 系统盘的 "/" 就是典型。而"测试系统盘"和"往 / 里写文件"本来就是两件事 ——
    /// 同一个文件系统里的**任意一个可写目录**都能把整块盘测到（容量、剩余空间、
    /// 写满的对象都是这个文件系统，不是那个目录）。
    QString suggestedDir;
    QString unavailableReason;

    bool    testable() const { return !suggestedDir.isEmpty(); }
    quint64 bytesUsed() const { return bytesTotal > bytesFree ? bytesTotal - bytesFree : 0; }
    double  usedPercent() const;
    /// 列表里那一行的主文本，例如 "nvme0n1p2  ext4  /"
    QString displayLine() const;
};

/// 一块物理磁盘（也可能是 RAID / 加密 / 回环这类块设备），下面挂着它的文件系统。
struct DiskEntry
{
    QString kernelName;     ///< "nvme0n1" / "sda" / "loop6"；Windows 上是 "PhysicalDrive0"
    QString model;          ///< "Samsung SSD 990 PRO 1TB"，拿不到就是空串
    quint64 bytesTotal = 0;
    bool    isSystemDisk = false;   ///< 承载 "/" 的那块，界面上要标出来
    bool    isVirtual    = false;   ///< 回环 / 设备映射 / RAID：不是一块真盘
    bool    isNetwork    = false;   ///< 网络位置（映射的网络驱动器 / nfs 之类）
    QList<FileSystemEntry> fileSystems;

    /// 至少有一个文件系统能被测（否则整块盘在列表里就没有意义）。
    bool testable() const;
    /// "内核名 + 型号"，例如 "nvme0n1  Samsung SSD 990 PRO 1TB"。
    /// 列表里第一列用这个 —— 标记（[系统盘] 之类）单独占一列，
    /// 否则列宽一紧就先被省略号吃掉，而那恰恰是最该看见的东西。
    QString nameLine() const;
    /// nameLine() 再加上 [系统盘] / [虚拟设备] 这些标记，给自检报告用（那边不缺宽度）。
    QString displayLine() const;
};

/// 枚举本机的物理磁盘，以及它们上面能被文件模式测试的文件系统。
///
/// **网络驱动器和虚拟磁盘一律不列**（而且**任何权限下都不列**）：
///   - Windows：映射的网络驱动器（`GetDriveTypeW` 返回 `DRIVE_REMOTE`）、内存盘，
///     以及拿不到物理磁盘号的卷（映射盘 / subst 这类重定向器）。
///     这些卷在普通权限下会凭空出现在列表里、却一个字节都写不进去
///     （建目录直接报 ERROR_INVALID_OWNER / 拒绝访问），提权之后又会消失 ——
///     同一个东西在两种权限下表现不一致本身就是 bug，所以干脆两边都不列。
///   - Linux：网络文件系统（nfs / cifs / sshfs …）不列。
///     但**回环 / LVM / RAID 这些块设备要保留**：LVM 常常就是系统盘本身，
///     回环设备也是"拿镜像当盘测"的正常用法，它们都是真的能测的。
///
/// 被排除掉的卷会写进 *ignored（形如 "X:（网络驱动器）"），
/// 由自检报告原样打出来 —— 免得用户只看到"我的盘怎么不见了"，无从判断原因。
///
/// 其余实现细节：Linux 上以 `lsblk -J` 为准（一次调用就能拿到
/// "磁盘 → 分区 → 文件系统 → 挂载点"的层次，顺带滤掉 snap 那种只读回环）；
/// lsblk 不可用时退回按挂载点列。tmpfs / proc 这类没有块设备的不算盘。
/// Windows 上按物理磁盘号分组，用只读的 IOCTL 取型号。
QList<DiskEntry> enumerateDisks(QStringList *ignored = nullptr);

/// 在 <mountPoint> 所在的文件系统上找一个"可以建 ml_disktest"的可写目录。
///
/// 顺序：挂载点本身 → 用户主目录 → /var/tmp → /tmp（后两个只在
/// 与 mountPoint **同属一个文件系统**时才有意义，多数机器上它们是独立 tmpfs）。
/// 每个候选都要真的能建出目录才算数 —— 不能用 QFileInfo::isWritable()：
/// Windows 上它只看只读属性，"C:\" 会被误判成可写，实际建目录是 Access denied。
///
/// 找不到就返回空串，并把原因写进 *reason。
QString suggestTestDir(const QString &mountPoint, QString *reason);

/// 两个路径是不是在同一个文件系统上。
/// Linux 比 st_dev（bind mount、LVM、btrfs 子卷都判得准）；Windows 比卷根。
bool sameFileSystem(const QString &a, const QString &b);

/// 这个文件系统类型名是不是网络文件系统（nfs / cifs / smb / sshfs …）。
/// 这类目标照样能测，但测到的是网络而不是盘，界面上要提醒一句。
bool isNetworkFileSystem(const QString &fsType);

/// 这个目录现在能不能真的在里面建出东西（会试建一个临时目录再删掉）。
///
/// **不能用 QFileInfo::isWritable()**：Windows 上它只看文件的只读属性，
/// "C:\" 会被判成可写，实际 mkdir 是 Access denied（非提权进程建不了）。
bool canUseAsTestDir(const QString &dir);

/// 查询某个路径所在文件系统的容量（轻量，可以在测试过程中每秒调用）。
/// 传目录或挂载点都可以。
bool queryVolumeSpace(const QString &path, quint64 *bytesTotal, quint64 *bytesFree);

/// 路径所在文件系统的名字。Windows 上是 "NTFS" / "exFAT" / "FAT32"，
/// Linux 上是 "ext4" / "vfat" / "exfat" / "btrfs" …。失败返回空串。
QString queryFileSystem(const QString &path);

/// 这个文件系统名是不是 FAT 家族（FAT12/16/32）。
///
/// 两个平台的叫法不一样：Windows 报 "FAT32" / "FAT"，Linux 报 "vfat" / "msdos"。
/// **exFAT 不算**：它没有 4 GiB 单文件上限，而 Linux 上它报的名字是 "exfat"，
/// 用 contains("fat") 去判会把它一起拦下来（那种误拦比不拦更糟：
/// 用户在大容量 exFAT 盘上连 8 GiB 的性能测试都跑不了）。
bool isFatFileSystem(const QString &fsName);

/// 目标目录所在文件系统是否支持直接 I/O（Linux 上是 O_DIRECT）。
///
/// tmpfs / ramfs / overlayfs（/dev/shm、/run、容器根目录）可能不支持 O_DIRECT，
/// 而本工具的 job 固定 direct=1，撞上这类文件系统 fio 会直接报
/// "Operation not supported"，光看日志很难反应过来是文件系统的问题。
/// 所以开测前先在目标目录里拿一个 4 KiB 对齐缓冲区试写一次再删掉。
/// Windows 走 windowsaio，不需要这个检查，恒返回 true。
bool supportsDirectIo(const QString &folder, QString *error);

/// 测试目录里已有的测试文件集合。
struct TestFileSet
{
    QStringList names;                 ///< 文件名（只含名字）
    /// 每个文件的字节数，与 names 一一对应。
    /// 只读校验时要靠它把 fio 报的"第 N 个文件的第 off 字节"换算成盘上位置；
    /// **不能假设每个文件一样大**——现成的测试文件可能是别的大小组合
    /// （比如上一次用的是别的测试数据尺寸），末个文件还常常是零头。
    QVector<qint64> sizes;
    qint64      totalBytes = 0;        ///< 全部文件大小之和
    int         count = 0;
    bool        isEmpty() const { return count == 0; }
};

/// 扫描 <folder> 下形如 testfile_* 的文件。
TestFileSet scanTestFiles(const QString &folder);

/// 当前进程是否以管理员（Linux 上是 root）身份运行。
bool isProcessElevated();
