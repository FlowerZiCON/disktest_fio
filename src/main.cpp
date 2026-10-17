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

#include "mainwindow.h"

#include "i18n.h"
#include "rundata.h"
#include "uitext.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("disktest"));

    // 版本号只有 CMakeLists.txt 的 project(... VERSION) 一个来源，
    // 别在这里写死——写死就会出现"标题栏 2.2.7、--version 输出 2.0.0"这种不一致。
    app.setApplicationVersion(kAppVersion());
    app.setOrganizationName(QStringLiteral("disktest"));
    app.setWindowIcon(QIcon(QStringLiteral(":/logo-window.png")));

    // 界面语言是**唯一**会存到磁盘上的偏好（见 MainWindow::resetToDefaultSettings：
    // 测试参数每次启动都回默认值，那条规则不动）。存在哪、写不进去怎么办，
    // 由 i18n::saveChoice() 决定：优先 exe 旁边的 disktest.ini，写不进去才退回
    // 用户目录（%APPDATA%\disktest\ 或 ~/.config/...）。这里不再设 QSettings 全局格式，
    // 免得某个未来用到默认 QSettings 的代码悄悄跟着变位置。

    // 结果 CSV 解析器的纯逻辑自检：不开窗口、不碰磁盘、不跑 fio。
    // 写代码时用 `disktest.exe --selftest-parse` 就能验规则，不用整盘测试。
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--selftest-parse")) {
        QStringList lines;
        const bool ok = RunData::runParserSelfTest(&lines);
        QTextStream out(stdout);
        out << QStringLiteral("=== CSV 解析器自检 ===\n");
        for (const QString &line : lines)
            out << line << QLatin1Char('\n');
        out << QStringLiteral("解析器自检结论: ") << (ok ? QStringLiteral("PASS")
                                                        : QStringLiteral("FAIL")) << QLatin1Char('\n');
        out.flush();
        return ok ? 0 : 1;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        kAppDescription());
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption selfTestOption(
        QStringLiteral("selftest"),
        kClSelfTest());
    QCommandLineOption outDirOption(
        QStringList{QStringLiteral("o"), QStringLiteral("outdir")},
        kClOutDir(),
        QStringLiteral("dir"));
    QCommandLineOption fioOption(
        QStringLiteral("fio"),
        kClFioPath(),
        QStringLiteral("path"));
    QCommandLineOption targetOption(
        QStringLiteral("selftest-target"),
        kClTarget(),
        QStringLiteral("root"));
    QCommandLineOption fillLimitOption(
        QStringLiteral("selftest-fill-limit"),
        kClFillLimit(),
        QStringLiteral("mib"));
    QCommandLineOption modeOption(
        QStringLiteral("selftest-mode"),
        kClMode(),
        QStringLiteral("mode"));
    QCommandLineOption selfTestStopOption(
        QStringLiteral("selftest-stop-after"),
        kClStopAfter(),
        QStringLiteral("sec"));
    QCommandLineOption selfTestScatterOption(
        QStringLiteral("selftest-scatter"),
        kClScatter());
    QCommandLineOption selfTestReadOnlyOption(
        QStringLiteral("selftest-read-only"),
        kClReadOnly());
    QCommandLineOption selfTestLowDensityOption(
        QStringLiteral("selftest-low-density"),
        kClLowDensity());
    QCommandLineOption selfTestRoundsOption(
        QStringLiteral("selftest-rounds"),
        kClSelftestRounds(),
        QStringLiteral("n"));
    QCommandLineOption selfTestPerfOption(
        QStringLiteral("selftest-perf"),
        kClSelftestPerf());
    QCommandLineOption selfTestPerfSizeOption(
        QStringLiteral("selftest-perf-size"),
        kClPerfSizeArg(),
        QStringLiteral("mib"));
    QCommandLineOption selfTestSaveDataOption(
        QStringLiteral("selftest-save-data"),
        kClSaveData(),
        QStringLiteral("path"));
    QCommandLineOption selfTestLoadDataOption(
        QStringLiteral("selftest-load-data"),
        kClLoadData(),
        QStringLiteral("path"));
    // 开发排查用：强制把"校验失败"的大字告警条挂出来看样式。
    // 故意不写进 README 的自检参数表：它是排查手段，不是给用户的功能。
    QCommandLineOption debugBannerOption(
        QStringLiteral("debug-force-verify-banner"),
        kClDebugBanner());
    // 界面语言。--lang 优先于保存的设置，保存的设置优先于系统语言。
    QCommandLineOption langOption(
        QStringLiteral("lang"),
        kClLang(),
        QStringLiteral("tag"));
    // 译文自检：不算界面的一部分，翻译/发版前跑一次。
    QCommandLineOption i18nCheckOption(
        QStringLiteral("i18n-check"),
        kClI18nCheck());

    parser.addOption(selfTestOption);
    parser.addOption(outDirOption);
    parser.addOption(fioOption);
    parser.addOption(targetOption);
    parser.addOption(fillLimitOption);
    parser.addOption(modeOption);
    parser.addOption(selfTestStopOption);
    parser.addOption(selfTestScatterOption);
    parser.addOption(selfTestReadOnlyOption);
    parser.addOption(selfTestLowDensityOption);
    parser.addOption(selfTestRoundsOption);
    parser.addOption(selfTestPerfOption);
    parser.addOption(selfTestPerfSizeOption);
    parser.addOption(selfTestSaveDataOption);
    parser.addOption(selfTestLoadDataOption);
    parser.addOption(debugBannerOption);
    parser.addOption(langOption);
    parser.addOption(i18nCheckOption);
    parser.process(app);

    // ---------------------------------------------------------------- 界面语言
    //
    // 顺序：系统语言 < 上次在「帮助 → 语言」里选的那个 < 命令行 --lang。
    // **必须在建 MainWindow 之前定下来**：界面文案是搭窗口时取的，
    // 换了语言只能重新启动（见 MainWindow::switchLanguage）。
    i18n::Language language = i18n::systemLanguage();
    i18n::Language parsed;
    if (i18n::languageFromTag(i18n::savedChoice(), &parsed))
        language = parsed;
    if (parser.isSet(langOption)) {
        const QString value = parser.value(langOption);
        if (value.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0) {
            language = i18n::systemLanguage();
        } else if (i18n::languageFromTag(value, &parsed)) {
            language = parsed;
        } else {
            QTextStream err(stderr);
            err << "unknown --lang: " << value
                << "  (expected zh_CN / zh_TW / en / auto)\n";
            err.flush();
            return 2;
        }
    }
    i18n::setCurrent(language);

    // 译文自检：不开窗口。翻译完/发版前跑一次，覆盖率与占位符一起看。
    if (parser.isSet(i18nCheckOption)) {
        QString report;
        const bool ok = i18n::check(&report);
        QTextStream out(stdout);
        out << report;
        out.flush();
        return ok ? 0 : 1;
    }

    MainWindow window;
    if (parser.isSet(fioOption))
        window.setFioPath(parser.value(fioOption));
    if (parser.isSet(outDirOption))
        window.setSelfTestOutputDir(QDir::cleanPath(parser.value(outDirOption)));
    window.setSelfTestTarget(parser.value(targetOption));
    window.setSelfTestFillLimit(parser.value(fillLimitOption).toInt());
    window.setSelfTestMode(parser.value(modeOption));
    window.setSelfTestStopAfter(parser.value(selfTestStopOption).toInt());
    window.setSelfTestScatter(parser.isSet(selfTestScatterOption));
    window.setSelfTestReadOnly(parser.isSet(selfTestReadOnlyOption));
    window.setSelfTestLowDensity(parser.isSet(selfTestLowDensityOption));
    window.setSelfTestRounds(parser.value(selfTestRoundsOption).toInt());
    window.setSelfTestPerf(parser.isSet(selfTestPerfOption),
                           parser.value(selfTestPerfSizeOption).toInt());
    window.setSelfTestSaveData(parser.value(selfTestSaveDataOption));
    window.setSelfTestLoadData(parser.value(selfTestLoadDataOption));
    window.setDebugForceVerifyBanner(parser.isSet(debugBannerOption));

    window.show();

    if (parser.isSet(selfTestOption)) {
        // 等窗口真正显示、布局完成后再跑，这样导出的图表截图是完整的
        QTimer::singleShot(400, &window, &MainWindow::runSelfTest);
    }

    return app.exec();
}
