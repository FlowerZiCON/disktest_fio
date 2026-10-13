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
    app.setApplicationVersion(kAppVersion);
    app.setOrganizationName(QStringLiteral("disktest"));
    app.setWindowIcon(QIcon(QStringLiteral(":/logo-window.png")));

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
        kAppDescription);
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption selfTestOption(
        QStringLiteral("selftest"),
        kClSelfTest);
    QCommandLineOption outDirOption(
        QStringList{QStringLiteral("o"), QStringLiteral("outdir")},
        kClOutDir,
        QStringLiteral("dir"));
    QCommandLineOption fioOption(
        QStringLiteral("fio"),
        kClFioPath,
        QStringLiteral("path"));
    QCommandLineOption targetOption(
        QStringLiteral("selftest-target"),
        kClTarget,
        QStringLiteral("root"));
    QCommandLineOption fillLimitOption(
        QStringLiteral("selftest-fill-limit"),
        kClFillLimit,
        QStringLiteral("mib"));
    QCommandLineOption modeOption(
        QStringLiteral("selftest-mode"),
        kClMode,
        QStringLiteral("mode"));
    QCommandLineOption selfTestStopOption(
        QStringLiteral("selftest-stop-after"),
        kClStopAfter,
        QStringLiteral("sec"));
    QCommandLineOption selfTestScatterOption(
        QStringLiteral("selftest-scatter"),
        kClScatter);
    QCommandLineOption selfTestReadOnlyOption(
        QStringLiteral("selftest-read-only"),
        kClReadOnly);
    QCommandLineOption selfTestLowDensityOption(
        QStringLiteral("selftest-low-density"),
        kClLowDensity);
    QCommandLineOption selfTestRoundsOption(
        QStringLiteral("selftest-rounds"),
        QStringLiteral("自检的测试圈数（整轮重复几遍）"),
        QStringLiteral("n"));
    QCommandLineOption selfTestPerfOption(
        QStringLiteral("selftest-perf"),
        QStringLiteral("自检跑性能模式（四个测试项，每项 10 秒）"));
    QCommandLineOption selfTestPerfSizeOption(
        QStringLiteral("selftest-perf-size"),
        QStringLiteral("自检性能模式的数据大小（MiB）"),
        QStringLiteral("mib"));
    QCommandLineOption selfTestSaveDataOption(
        QStringLiteral("selftest-save-data"),
        kClSaveData,
        QStringLiteral("path"));
    QCommandLineOption selfTestLoadDataOption(
        QStringLiteral("selftest-load-data"),
        kClLoadData,
        QStringLiteral("path"));
    // 开发排查用：强制把"校验失败"的大字告警条挂出来看样式。
    // 故意不写进 README 的自检参数表：它是排查手段，不是给用户的功能。
    QCommandLineOption debugBannerOption(
        QStringLiteral("debug-force-verify-banner"),
        QStringLiteral("开发排查：强制显示校验失败告警条（界面会标明是示例）"));

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
    parser.process(app);

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
