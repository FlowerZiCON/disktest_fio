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

/// 一个可用卷（盘符）的信息。
struct VolumeEntry
{
    QString rootPath;       ///< 形如 "C:/"
    QString letter;         ///< 形如 "C:"
    QString deviceName;     ///< 界面上的"设备名称"：物理型号 > 卷标 > 类型描述
    QString physicalModel;  ///< 物理磁盘型号，例如 "Predator SSD GM7000 1TB NVMe"
    QString label;          ///< 卷标，例如 "MOONLIGHT"（常常是空的）
    QString fileSystem;     ///< 文件系统，例如 "NTFS"
    QString typeText;       ///< "本地磁盘" / "可移动磁盘" / "网络驱动器" …
    quint64 bytesTotal = 0;
    quint64 bytesFree  = 0;

    quint64 bytesUsed() const { return bytesTotal > bytesFree ? bytesTotal - bytesFree : 0; }
    double  usedPercent() const;
    /// 需求指定的显示格式：盘符：设备名字 已用空间/总空间 使用量%
    QString displayLine() const;
};

/// 枚举本机所有可用卷（按盘符排序）。
///
/// 注意：为了拿到"这块卷落在哪个物理设备上"，实现里会以 **0 访问权限**
/// 打开 \\.\C: 和 \\.\PhysicalDriveN 并发送两个只读的 IOCTL 查询
/// （IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS / IOCTL_STORAGE_QUERY_PROPERTY）。
/// 这只是读取设备属性字符串，不读也不写任何扇区数据 ——
/// 性能测试本身永远只作用于普通文件。
QList<VolumeEntry> enumerateVolumes();

/// 查询某个卷的容量（轻量，可以在测试过程中每秒调用）。
bool queryVolumeSpace(const QString &rootPath, quint64 *bytesTotal, quint64 *bytesFree);
/// 卷的文件系统名，例如 "NTFS" / "exFAT" / "FAT32"。失败返回空串。
QString queryFileSystem(const QString &rootPath);

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

/// 当前进程是否以管理员身份运行（Windows 上查访问令牌的 TokenElevation）。
bool isProcessElevated();

