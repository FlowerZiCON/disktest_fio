cmake_minimum_required(VERSION 3.21)

project(disktest
    VERSION 3.0.6
    DESCRIPTION "Qt + fio disk reliability and performance tester GUI"
    LANGUAGES CXX)

# ---------------------------------------------------------------- 程序元信息
#
# 这些字符串编进 exe 的版本资源里（资源管理器 → 右键 → 属性 → 详细信息）。
# 只在这里写一次：resources/app.rc.in 是模板，CMake 把下面的值填进去，
# 所以「版本号 / 产品名 / 版权」不会出现 exe 属性与界面各说一套的情况。
#
# 版本号**不在这里**——它就是上面 project(... VERSION) 里的那个，别的地方没有第二份。
# 产品名要和 src/uitext.h 的 kAppName 一致（那是标题栏和「关于」里显示的名字）。
set(APP_PRODUCT_NAME     "磁盘测试By MOONLIGHT")
set(APP_FILE_DESCRIPTION "基于 fio 的磁盘可靠性和性能测试工具")
set(APP_COMPANY          "MOONLIGHT")
set(APP_COPYRIGHT        "Copyright (C) 2026 MOONLIGHT. GNU GPL v3 或更高版本。")
set(APP_HOMEPAGE         "https://github.com/FlowerZiCON/disktest_fio")

# 官方 fio 放在 third_party 下。打包时只带上运行需要的文件，
# examples/ 是给人看的样例、不随包发布。
set(FIO_DIR "${CMAKE_SOURCE_DIR}/third_party/fio"
    CACHE PATH "官方 fio 所在目录（内含 fio.exe / fio）")

# fio 在 Windows 上叫 fio.exe，在 Linux 上就叫 fio。程序里找"自带的那份"
# 用的是同一个名字，两边必须一致，所以只在这里定义一次。
if(WIN32)
    set(FIO_EXE_NAME "fio.exe")
else()
    set(FIO_EXE_NAME "fio")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 6.2 是下限而不是首选：Ubuntu 22.04 (jammy) 的 universe 里就是 Qt 6.2.4
# （qt6-base-dev + libqt6charts6-dev），把下限写成 6.5 会让 Linux 上直接
# 找不到 Qt。代码没有用到 6.2 之后才有的 API，Windows 上的 6.8 照样满足。
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Charts)

add_executable(disktest WIN32
    src/main.cpp
    src/uitext.h
    src/fioconfig.h
    src/fioconfig.cpp
    src/prociocounter.h
    src/prociocounter.cpp
    src/driveinfo.h
    src/driveinfo.cpp
    src/fiorunner.h
    src/fiorunner.cpp
    src/perfcell.h
    src/perfcell.cpp
    src/speedchart.h
    src/speedchart.cpp
    # 结果 CSV 的解析（读）与结果窗口
    src/rundata.h
    src/rundata.cpp
    src/resultwindow.h
    src/resultwindow.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    # 窗口图标用的 PNG，编进 qrc 随程序走
    resources/resources.qrc
)

# exe 的文件图标 + 版本资源（资源管理器 / 任务栏 / Alt+Tab / 右键属性 → 详细信息）。
# .rc 只有 Windows 工具链认识，CMake 也只在 Windows 上启用 RC 语言；
# 在 Linux 上把它塞进 add_executable 会让 g++ 去编这个文件，直接报错。
#
# 参与编译的不是手写的 .rc，而是从 resources/app.rc.in 生成的：模板里只有
# @...@ 占位符，版本号/产品名/版权/平台标识在 configure 时填进去 —— 这样
# 「改版本号只改 project(... VERSION)」这条规则对 exe 属性同样成立。
# 生成到 build/ 而不是原地改写：仓库里那份 .in 保持干净、不会被 configure 弄脏。
if(WIN32)
    if(PROJECT_VERSION_TWEAK)
        set(_ver_tweak "${PROJECT_VERSION_TWEAK}")
    else()
        set(_ver_tweak "0")
    endif()
    # FILEVERSION 只能是 4 个 16 位整数，所以这里拼成逗号形式；
    # 给人看的那份（"3.0.6"）走 APP_VERSION。
    set(APP_VERSION_COMMA
        "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},${_ver_tweak}")
    set(APP_VERSION "${PROJECT_VERSION}")

    # 这是哪一份包 —— 和 src/uitext.h 的 DISKTEST_ARCH 回答同一个问题，
    # 报问题、对着 release 页挑包时，看一眼属性就知道。
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(APP_ARCH "win64")
    else()
        set(APP_ARCH "win32")
    endif()
    set(APP_COMMENTS "基于 fio 的磁盘可靠性测试 GUI（${APP_ARCH}）；源码 ${APP_HOMEPAGE}")

    # .rc 里的图标路径按生成文件所在目录（build/）解析，所以必须给绝对路径。
    # 用正斜杠而不是 TO_NATIVE_PATH：rc.exe 会解释字符串里的反斜杠转义，
    # "...\resources\logo.ico" 里的 \r 会变成一个回车，报 RC2135 file not found。
    set(APP_ICON_FILE "${CMAKE_SOURCE_DIR}/resources/logo.ico")

    # 生成的 .rc 必须带 UTF-8 BOM：没有 BOM 时 rc.exe 按系统 ANSI 代码页读
    # （中文系统是 936），中文串里的字节会"吞掉"后面的引号，报
    #   error C2001: 字符串字面量中的换行符
    # BOM 在这里补而不是写进模板：编辑器一旦"另存为 UTF-8 无 BOM"就会把模板里的
    # BOM 悄悄抹掉，构建随即失败（实测踩过），不如让生成步骤每次都补。
    file(READ "${CMAKE_SOURCE_DIR}/resources/app.rc.in" _rc_template)
    string(CONFIGURE "${_rc_template}" _rc_text @ONLY)
    string(ASCII 239 187 191 _rc_bom)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/app.rc" "${_rc_bom}${_rc_text}")
    # 模板改了要重新 configure，否则 build/ 里的 app.rc 还是老的。
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_SOURCE_DIR}/resources/app.rc.in")

    target_sources(disktest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

target_link_libraries(disktest PRIVATE Qt6::Widgets Qt6::Charts)

# 版本号只有 project(... VERSION x.y.z) 这一个来源，编译期传给代码，
# 标题栏和「关于」里显示的都取自它。
target_compile_definitions(disktest PRIVATE DISKTEST_VERSION="${PROJECT_VERSION}")

if(MSVC)
    # /utf-8 必须加：源码里有中文字面量，否则 MSVC 按 GBK 解释会变乱码
    target_compile_options(disktest PRIVATE /utf-8 /W4)
else()
    target_compile_options(disktest PRIVATE -Wall -Wextra)
endif()

# 图标可以随时重新生成（需要 Pillow）。resources/logo-source.ico 是母版，
# resources/logo.ico 是给 Windows 用的多尺寸版本（母版只有 128 一帧，缩到 16px 会糊）。
#   python tools/make_icon.py resources/logo.ico resources/logo-window.png \
#          --from resources/logo-source.ico
#
# 只在 Windows 上有意义：logo.ico 的唯一用途是被 app.rc 编进 pe 文件，
# Linux 用的窗口图标是 qrc 里那张 logo-window.png（两边共用，已经生成好了）。
if(WIN32)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_custom_target(icon
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/make_icon.py"
                    "${CMAKE_SOURCE_DIR}/resources/logo.ico"
                    "${CMAKE_SOURCE_DIR}/resources/logo-window.png"
                    --from "${CMAKE_SOURCE_DIR}/resources/logo-source.ico"
            COMMENT "重新生成 resources/logo.ico 与 logo-window.png")
    endif()
endif()

# ---------------------------------------------------------------- 打包
#
# 目标机器上可能既没有 Qt 也没有 VC++ 运行库，所以得把
# Qt DLL + 平台插件 + MSVC 运行库全拷到 exe 旁边。
#   注意：不能加 --no-compiler-runtime —— Qt 是 /MD 构建的，本程序必须一起
#   动态链接 CRT，少了 vcruntime140 / msvcp140 在干净机器上会直接起不来。
set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")

# Qt 是 /MD 构建的，本程序必须跟着动态链接 MSVC 运行库，所以目标机器上没有
# VC++ 运行库时就得把这些 DLL 一起带上。windeployqt 在本机找不到它们
# （拷完之后 dist 里只有 Qt 的 DLL），所以直接从 VC 安装目录的 Redist 里取。
set(MSVC_CRT_DLLS "")
if(MSVC)
    # cl.exe 位于 <VC>/Tools/MSVC/<版本>/bin/Host<x64>/x64/cl.exe
    # 注意 VC 和 MSVC 之间还有一层 Tools，别按目录名猜。
    get_filename_component(_bin_arch "${CMAKE_CXX_COMPILER}" DIRECTORY) # .../bin/Hostx64/x64
    get_filename_component(_bin_host "${_bin_arch}" DIRECTORY)          # .../bin/Hostx64
    get_filename_component(_bin "${_bin_host}" DIRECTORY)               # .../bin
    get_filename_component(_msvc_ver_dir "${_bin}" DIRECTORY)           # .../<版本>
    get_filename_component(_msvc_dir "${_msvc_ver_dir}" DIRECTORY)      # .../MSVC
    get_filename_component(_tools_dir "${_msvc_dir}" DIRECTORY)         # .../Tools
    get_filename_component(_vc_root "${_tools_dir}" DIRECTORY)          # .../VC
    get_filename_component(_msvc_ver "${_msvc_ver_dir}" NAME)           # 例如 14.51.36231

    file(GLOB MSVC_CRT_DLLS
         "${_vc_root}/Redist/MSVC/${_msvc_ver}/x64/Microsoft.VC*.CRT/*.dll")
    if(NOT MSVC_CRT_DLLS)
        file(GLOB MSVC_CRT_DLLS
             "${_vc_root}/Redist/MSVC/*/x64/Microsoft.VC*.CRT/*.dll")
    endif()
    list(LENGTH MSVC_CRT_DLLS _crt_count)
    if(_crt_count GREATER 0)
        message(STATUS "打包时将附上 ${_crt_count} 个 MSVC 运行库 DLL（${_vc_root}/Redist/MSVC/${_msvc_ver}/x64）")
    else()
        message(WARNING "没找到 MSVC 运行库 Redist（查过 ${_vc_root}/Redist/MSVC/${_msvc_ver}/x64）；"
                        "打包产物在没有 VC++ 运行库的机器上可能起不来")
    endif()
endif()

# 随包发布的那份 fio。Windows 上是仓库里带的 fio.exe，Linux 上一般直接用
# 发行版的 fio（本仓库 third_party/fio 里放的是 Windows 构建，Linux 上跑不了），
# 找不到就不附 —— 程序会自己去 PATH 上找。
set(_fio_binary "${FIO_DIR}/${FIO_EXE_NAME}")
set(PACKAGE_FIO_COMMANDS "")
if(EXISTS "${_fio_binary}")
    # 只带 fio 可执行文件 + 许可证/说明；上游的 examples/ 不随包发布
    # （55 个样例 .fio 文件，程序一个都不读）
    set(PACKAGE_FIO_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_DIR}/fio"
        COMMAND "${CMAKE_COMMAND}" -E copy
                "${_fio_binary}" "${DIST_DIR}/fio/${FIO_EXE_NAME}"
        COMMAND "${CMAKE_COMMAND}" -E copy
                "${FIO_DIR}/COPYING.txt" "${FIO_DIR}/MORAL-LICENSE.txt"
                "${FIO_DIR}/README.txt" "${FIO_DIR}/HOWTO.txt"
                "${FIO_DIR}/REPORTING-BUGS.txt" "${DIST_DIR}/fio/")
else()
    message(STATUS "没找到 ${_fio_binary}：打包时不附 fio，程序会去 PATH 上找")
endif()

if(TARGET Qt6::windeployqt)
    add_custom_target(package
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:disktest>" "${DIST_DIR}/"
        COMMAND Qt6::windeployqt --release --no-translations --no-opengl-sw
                --no-system-d3d-compiler "${DIST_DIR}/disktest.exe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${MSVC_CRT_DLLS} "${DIST_DIR}/"
        ${PACKAGE_FIO_COMMANDS}
    # 许可证文本集中放在 dist/licenses/：
    #   LICENSE                 程序自身（GPL-3.0）
    #   THIRD-PARTY-NOTICES.md  第三方组件汇总说明
    #   Qt-LGPLv3.txt           Qt（LGPL-3.0）
    #   fio-GPLv2.txt           fio（GPL-2.0）
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_DIR}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
            "${CMAKE_SOURCE_DIR}/licenses/Qt-LGPLv3.txt"
            "${CMAKE_SOURCE_DIR}/licenses/fio-GPLv2.txt"
            "${DIST_DIR}/licenses/"
    # 随包带一份面向用户的 README。它引用了截图，所以截图目录也要跟上——
    # 少了截图的 README 在发布包里就是一堆断链。
    # 必须用 copy_directory：cmake -E copy 对目录是**静默失败**的
    # （目标目录根本不建，也不报错），写错了只会让包里悄悄少东西。
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_SOURCE_DIR}/README.md" "${DIST_DIR}/README.md"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/docs/images" "${DIST_DIR}/docs/images"
        DEPENDS disktest
        COMMENT "打包到 ${DIST_DIR}（exe + Qt 运行库 + MSVC 运行库 + 平台插件 + fio）"
        VERBATIM)

    add_custom_target(deploy
        COMMAND Qt6::windeployqt --release --no-translations --no-opengl-sw
                --no-system-d3d-compiler "$<TARGET_FILE:disktest>"
        DEPENDS disktest
        COMMENT "windeployqt: 把 Qt 运行时拷到 exe 旁边")
else()
    # Linux（没有 windeployqt）：Qt 运行时是发行版提供的，不入包。
    # 这里只把 exe + 许可证 + README 收进 dist，方便直接 tar 走。
    # 想连 fio 一起发，就把一份 Linux 版 fio 放进 third_party/fio/，
    # 上面那段 PACKAGE_FIO_COMMANDS 会自动把它带上。
    add_custom_target(package
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:disktest>" "${DIST_DIR}/"
        ${PACKAGE_FIO_COMMANDS}
    # 许可证文本集中放在 dist/licenses/：
    #   LICENSE                 程序自身（GPL-3.0）
    #   THIRD-PARTY-NOTICES.md  第三方组件汇总说明
    #   Qt-LGPLv3.txt           Qt（LGPL-3.0）
    #   fio-GPLv2.txt           fio（GPL-2.0）
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_DIR}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
            "${CMAKE_SOURCE_DIR}/licenses/Qt-LGPLv3.txt"
            "${CMAKE_SOURCE_DIR}/licenses/fio-GPLv2.txt"
            "${DIST_DIR}/licenses/"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_SOURCE_DIR}/README.md" "${DIST_DIR}/README.md"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/docs/images" "${DIST_DIR}/docs/images"
        DEPENDS disktest
        COMMENT "打包到 ${DIST_DIR}（exe + 许可证 + README；Qt 运行时由发行版提供）"
        VERBATIM)
endif()

# 用 cmake --install 分发时许可证也必须跟着走：GPL 第 4/6 条要求随二进制
# 一并提供许可证文本，LGPL 同样要求附上声明。缺了这一条，装出来的目录
# 就是"没有许可证的二进制"，比不发布还糟。
install(TARGETS disktest RUNTIME DESTINATION .)
install(FILES
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
        DESTINATION licenses)
install(FILES
        "${CMAKE_SOURCE_DIR}/licenses/Qt-LGPLv3.txt"
        "${CMAKE_SOURCE_DIR}/licenses/fio-GPLv2.txt"
        DESTINATION licenses)
