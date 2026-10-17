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

#include "i18n.h"

#include "uitext.h"   // ui::catalog()

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QLocale>
#include <QSet>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextStream>

#include <algorithm>

namespace i18n {
namespace {

/// 设置文件里只存这一个键（测试参数依然不落盘，见 MainWindow::resetToDefaultSettings）。
const char *const kSettingsKey = "ui/language";

Language g_language = Language::SimplifiedChinese;

using Table = QHash<QByteArray, QString>;

struct Tables
{
    Table english;
    Table traditionalChinese;
};

LoadReport &reportStorage()
{
    static LoadReport report;
    return report;
}

/// 把一行里的 `\n` `\t` `\\` 换成真正的字符。
/// 译文里必须能写换行——界面文案里有几十条多行提示框正文。
QString unescape(const QString &raw)
{
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c != QLatin1Char('\\') || i + 1 >= raw.size()) {
            out.append(c);
            continue;
        }
        const QChar next = raw.at(i + 1);
        if (next == QLatin1Char('n')) {
            out.append(QLatin1Char('\n'));
        } else if (next == QLatin1Char('t')) {
            out.append(QLatin1Char('\t'));
        } else if (next == QLatin1Char('\\')) {
            out.append(QLatin1Char('\\'));
        } else {
            out.append(c);   // 其它转义原样保留，别悄悄吃掉
            out.append(next);
        }
        if (next == QLatin1Char('n') || next == QLatin1Char('t') || next == QLatin1Char('\\'))
            ++i;
    }
    return out;
}

/// 读一份译文表。文件格式（UTF-8）：
///    # 注释
///    kBtnStart<TAB>Start Test
/// 空行和注释行跳过；键和值两列；值里的 \n 是换行。
void loadTable(const QString &fileName, Table *table, LoadReport *report)
{
    const QStringList candidates = {
        // 程序目录旁边的 i18n/ 优先：想改一行译文就不用重新编译
        QCoreApplication::applicationDirPath() + QStringLiteral("/i18n/") + fileName,
        // 否则用编进 exe 的那份（qrc）
        QStringLiteral(":/i18n/") + fileName,
    };

    QString usedPath;
    QByteArray data;
    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.exists() || !file.open(QIODevice::ReadOnly))
            continue;
        data = file.readAll();
        usedPath = path;
        break;
    }
    if (data.isEmpty()) {
        report->warnings.append(QStringLiteral("cannot read i18n table: ") + fileName);
        return;
    }
    if (data.startsWith("\xEF\xBB\xBF"))   // 记事本存的 UTF-8 带 BOM
        data.remove(0, 3);

    const QString content = QString::fromUtf8(data);
    const QStringList lines = content.split(QLatin1Char('\n'));
    int lineNo = 0;
    for (QString line : lines) {
        ++lineNo;
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0) {
            report->warnings.append(QStringLiteral("%1:%2: no tab separator")
                                        .arg(fileName).arg(lineNo));
            continue;
        }
        const QString key = line.left(tab).trimmed();
        const QString value = unescape(line.mid(tab + 1));
        if (key.isEmpty()) {
            report->warnings.append(QStringLiteral("%1:%2: empty key")
                                        .arg(fileName).arg(lineNo));
            continue;
        }
        const QByteArray rawKey = key.toLatin1();
        if (table->contains(rawKey)) {
            report->warnings.append(QStringLiteral("%1:%2: duplicate key %3")
                                        .arg(fileName).arg(lineNo).arg(key));
            continue;   // 先出现的赢：改译文时新行写在下面也能看到旧行为
        }
        table->insert(rawKey, value);
    }
    report->loadedFiles.append(usedPath);
}

/// 译文表：第一次用到时才读，读完就一直留在内存里。
/// 先看程序目录的 i18n/，找不到再用 qrc 里那份。
Tables &tables()
{
    static Tables instance;
    static bool loaded = false;
    if (loaded)
        return instance;
    loaded = true;

    loadTable(QStringLiteral("en.tsv"), &instance.english, &reportStorage());
    loadTable(QStringLiteral("zh_TW.tsv"), &instance.traditionalChinese, &reportStorage());
    return instance;
}

const Table *currentTable()
{
    switch (g_language) {
    case Language::English:
        return &tables().english;
    case Language::TraditionalChinese:
        return &tables().traditionalChinese;
    case Language::SimplifiedChinese:
        break;
    }
    return nullptr;
}

/// 占位符集合（%1 %2 … %99）。译文里的集合必须和中文原文完全一样，
/// 少一个就会让 .arg() 留下一个没换掉的 "%2" 在界面上。
QSet<int> placeholders(const QString &text)
{
    QSet<int> out;
    for (int i = 0; i + 1 < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('%'))
            continue;
        int j = i + 1;
        int value = 0;
        int digits = 0;
        while (j < text.size() && text.at(j).isDigit() && digits < 2) {
            value = value * 10 + text.at(j).digitValue();
            ++j;
            ++digits;
        }
        if (digits > 0 && value > 0)
            out.insert(value);
    }
    return out;
}

QString joinNumbers(const QSet<int> &numbers)
{
    QList<int> sorted = numbers.values();
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    for (int n : sorted)
        parts.append(QLatin1Char('%') + QString::number(n));
    return parts.join(QLatin1Char(' '));
}

QString settingsOrgName()
{
    const QString org = QCoreApplication::organizationName();
    return org.isEmpty() ? QStringLiteral("disktest") : org;
}

QString settingsAppName()
{
    const QString app = QCoreApplication::applicationName();
    return app.isEmpty() ? QStringLiteral("disktest") : app;
}

/// 首选位置：exe 旁边。安装包装出来的版本能写（installer 给 {app} 加了 users-modify），
/// 绿色版放在用户自己的目录里也能写 —— 这样"设置跟着程序走"，整个文件夹拷走就带走了。
QString exeSideSettingsPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/disktest.ini");
}

/// 兜底位置：用户目录（Windows: %APPDATA%\disktest\disktest.ini，
/// Linux: ~/.config/disktest/disktest.ini）。只有 exe 旁边写不进去时才用它。
QString userSettingsPath()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     settingsOrgName(), settingsAppName()).fileName();
}

/// 真的建一个临时文件再删掉：QFileInfo::isWritable() 在 Windows 上看的是
/// "只读属性"而不是 ACL，Program Files 那种目录它会骗人说可写。
bool exeDirWritable()
{
    QTemporaryFile probe(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/.disktest-write-probe-XXXXXX"));
    return probe.open();
}

} // namespace

// ---------------------------------------------------------------- 语言选择
Language current()
{
    return g_language;
}

void setCurrent(Language language)
{
    g_language = language;
}

bool languageFromTag(const QString &tag, Language *out)
{
    QString t = tag.trimmed().toLower();
    t.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (t.isEmpty())
        return false;

    Language value;
    if (t == QLatin1String("zh_cn") || t == QLatin1String("zh_hans")
        || t == QLatin1String("zh_hans_cn") || t == QLatin1String("zh_sg")
        || t == QLatin1String("zh_hans_sg") || t == QLatin1String("cn")
        || t == QLatin1String("chs") || t == QLatin1String("zh"))
        value = Language::SimplifiedChinese;
    else if (t == QLatin1String("zh_tw") || t == QLatin1String("zh_hant")
             || t == QLatin1String("zh_hant_tw") || t == QLatin1String("zh_hk")
             || t == QLatin1String("zh_hant_hk") || t == QLatin1String("zh_mo")
             || t == QLatin1String("tw") || t == QLatin1String("cht")
             || t == QLatin1String("hant"))
        value = Language::TraditionalChinese;
    else if (t.startsWith(QLatin1String("en")))
        value = Language::English;
    else
        return false;

    if (out)
        *out = value;
    return true;
}

QString tag(Language language)
{
    switch (language) {
    case Language::TraditionalChinese:
        return QStringLiteral("zh_TW");
    case Language::English:
        return QStringLiteral("en");
    case Language::SimplifiedChinese:
        break;
    }
    return QStringLiteral("zh_CN");
}

QString nativeName(Language language)
{
    switch (language) {
    case Language::TraditionalChinese:
        return QStringLiteral("繁體中文");
    case Language::English:
        return QStringLiteral("English");
    case Language::SimplifiedChinese:
        break;
    }
    return QStringLiteral("简体中文");
}

Language systemLanguage()
{
    // uiLanguages() 是按用户偏好排序的：第一个认得出的语言说了算。
    const QStringList tags = QLocale::system().uiLanguages();
    for (const QString &tag : tags) {
        const QLocale locale(tag);
        if (locale.language() == QLocale::Chinese) {
            if (locale.script() == QLocale::TraditionalChineseScript)
                return Language::TraditionalChinese;
            if (locale.script() == QLocale::SimplifiedChineseScript)
                return Language::SimplifiedChinese;
            switch (locale.territory()) {
            case QLocale::Taiwan:
            case QLocale::HongKong:
            case QLocale::Macao:
                return Language::TraditionalChinese;
            default:
                return Language::SimplifiedChinese;
            }
        }
        if (locale.language() == QLocale::English)
            return Language::English;
    }
    // 既不是中文也不是英文：给英文。宁可英文，也不要把简体中文塞给看不懂的人。
    return Language::English;
}

// ---------------------------------------------------------------- 译文查找

QString text(const char *key, const char *sourceUtf8)
{
    const Table *table = currentTable();
    if (table && key) {
        const auto it = table->constFind(QByteArray::fromRawData(key, int(qstrlen(key))));
        if (it != table->constEnd())
            return it.value();
    }
    return QString::fromUtf8(sourceUtf8);
}

QString savedChoice()
{
    // 正常情况读 exe 旁边那份；绿色版被解压到写不进去的目录（Program Files、
    // /usr/bin）时，写的是用户目录那份 —— 所以两处都要看，exe 旁边优先。
    const QStringList candidates = {exeSideSettingsPath(), userSettingsPath()};
    for (const QString &path : candidates) {
        QSettings settings(path, QSettings::IniFormat);
        const QString value = settings.value(QLatin1String(kSettingsKey)).toString();
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

void saveChoice(Language language)
{
    const QString value = tag(language);

    // 1) 先写 exe 旁边。安装包装出来的版本能走这条：installer/disktest.iss 给 {app}
    //    加了 users-modify（同一处权限也是「保存测试数据」把 CSV 存到程序目录的前提）；
    //    绿色版放在用户自己的目录里同样可写。
    QSettings exeSide(exeSideSettingsPath(), QSettings::IniFormat);
    exeSide.setValue(QLatin1String(kSettingsKey), value);
    exeSide.sync();
    if (exeSide.status() == QSettings::NoError)
        return;

    // 2) 写不进去：绿色版被手工解压到 Program Files（那里的默认 ACL 只给 Users 读+执行）、
    //    Linux 装到 /usr/bin 之类。这时退回用户目录，否则"选完语言重启又变回去"，
    //    用户根本看不出是权限问题。
    QSettings userSide(QSettings::IniFormat, QSettings::UserScope,
                       settingsOrgName(), settingsAppName());
    userSide.setValue(QLatin1String(kSettingsKey), value);
    userSide.sync();
}

QString settingsDescription()
{
    QString out = exeSideSettingsPath();
    out += exeDirWritable() ? QStringLiteral("  [writable]")
                            : QStringLiteral("  [read-only -> fallback in use]");
    out += QStringLiteral("\n  fallback: ") + userSettingsPath();
    return out;
}

// ---------------------------------------------------------------- 自检

const LoadReport &loadReport()
{
    tables();   // 保证表已经读过
    return reportStorage();
}

bool check(QString *report)
{
    tables();

    QString buffer;
    QTextStream out(&buffer);

    const QVector<ui::TextEntry> &catalog = ui::catalog();
    out << "i18n catalog entries: " << catalog.size() << "\n";

    bool ok = true;
    for (const Language language : {Language::English, Language::TraditionalChinese}) {
        const Table *table = nullptr;
        switch (language) {
        case Language::English:            table = &tables().english; break;
        case Language::TraditionalChinese: table = &tables().traditionalChinese; break;
        case Language::SimplifiedChinese:  continue;
        }
        out << "\n[" << tag(language) << "]\n";

        QSet<QByteArray> known;
        int translated = 0;
        QStringList missing;
        QStringList badPlaceholders;
        for (const ui::TextEntry &entry : catalog) {
            known.insert(QByteArray(entry.key));
            const auto it = table->constFind(QByteArray(entry.key));
            if (it == table->constEnd()) {
                missing.append(QString::fromLatin1(entry.key));
                continue;
            }
            ++translated;
            // 空译文是**故意的**（例如英文把「圈」的量词后缀去掉，
            // 只留 "Rounds: 1"），所以只把"整条没有"算漏翻。
            const QSet<int> sourcePh = placeholders(QString::fromUtf8(entry.source));
            const QSet<int> targetPh = placeholders(it.value());
            if (sourcePh != targetPh) {
                const QString sourceList = joinNumbers(sourcePh);
                const QString targetList = joinNumbers(targetPh);
                badPlaceholders.append(QStringLiteral("%1  (source: %2 | %3: %4)")
                                           .arg(QString::fromLatin1(entry.key),
                                                sourceList.isEmpty() ? QStringLiteral("-") : sourceList,
                                                tag(language),
                                                targetList.isEmpty() ? QStringLiteral("-") : targetList));
            }
        }

        QStringList extra;
        for (auto it = table->constBegin(); it != table->constEnd(); ++it) {
            if (!known.contains(it.key()))
                extra.append(QString::fromLatin1(it.key()));
        }
        extra.sort();
        missing.sort();

        out << "  translated: " << translated << " / " << catalog.size() << "\n";
        out << "  missing   : " << missing.size() << "\n";
        for (const QString &key : missing)
            out << "      " << key << "\n";
        out << "  unknown   : " << extra.size() << "\n";
        for (const QString &key : extra)
            out << "      " << key << "\n";
        out << "  bad %n    : " << badPlaceholders.size() << "\n";
        for (const QString &line : badPlaceholders)
            out << "      " << line << "\n";

        // 「unknown」不算失败：同一个键在两个平台上各有一份文案
        // （kIoModeTip / kModeElevatedTip …），译文表里两行都写着，
        // 而一次构建只看得到其中一个平台的那一行。
        // 真正要拦的是"漏翻"和"占位符对不上"。
        if (!missing.isEmpty() || !badPlaceholders.isEmpty())
            ok = false;
    }

    const LoadReport &load = loadReport();
    out << "\nloaded files:\n";
    for (const QString &file : load.loadedFiles)
        out << "  " << file << "\n";
    if (!load.warnings.isEmpty()) {
        ok = false;
        out << "warnings:\n";
        for (const QString &warning : load.warnings)
            out << "  " << warning << "\n";
    }
    out << "\nsettings file (where the language choice is stored):\n  "
        << settingsDescription() << "\n";
    out << "verdict: " << (ok ? "PASS" : "FAIL") << "\n";
    out.flush();
    if (report)
        *report = buffer;
    return ok;
}

} // namespace i18n
