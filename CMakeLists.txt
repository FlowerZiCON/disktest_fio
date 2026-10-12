cmake_minimum_required(VERSION 3.21)

project(qtfiogui
    VERSION 2.2.8
    DESCRIPTION "Qt + fio disk reliability tester GUI (file mode only)"
    LANGUAGES CXX)

# 官方 fio 放在 third_party 下。打包时只带上运行需要的文件，
# examples/ 是给人看的样例、不随包发布。
set(FIO_DIR "${CMAKE_SOURCE_DIR}/third_party/fio"
    CACHE PATH "官方 fio 所在目录（内含 fio.exe）")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Charts)

add_executable(qtfiogui WIN32
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
    # exe 文件图标（资源管理器 / 任务栏 / Alt+Tab）
    resources/app.rc
    # 窗口图标用的 PNG，编进 qrc 随程序走
    resources/resources.qrc
)

target_link_libraries(qtfiogui PRIVATE Qt6::Widgets Qt6::Charts)

# 版本号只有 project(... VERSION x.y.z) 这一个来源，编译期传给代码，
# 标题栏和「关于」里显示的都取自它。
target_compile_definitions(qtfiogui PRIVATE QTFIOGUI_VERSION="${PROJECT_VERSION}")

if(MSVC)
    # /utf-8 必须加：源码里有中文字面量，否则 MSVC 按 GBK 解释会变乱码
    target_compile_options(qtfiogui PRIVATE /utf-8 /W4)
else()
    target_compile_options(qtfiogui PRIVATE -Wall -Wextra)
endif()

# 图标可以随时重新生成（需要 Pillow）。resources/logo-source.ico 是母版，
# resources/logo.ico 是给 Windows 用的多尺寸版本（母版只有 128 一帧，缩到 16px 会糊）。
#   python tools/make_icon.py resources/logo.ico resources/logo-window.png \
#          --from resources/logo-source.ico
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(icon
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/make_icon.py"
                "${CMAKE_SOURCE_DIR}/resources/logo.ico"
                "${CMAKE_SOURCE_DIR}/resources/logo-window.png"
                --from "${CMAKE_SOURCE_DIR}/resources/logo-source.ico"
        COMMENT "重新生成 resources/logo.ico 与 logo-window.png")
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

if(TARGET Qt6::windeployqt)
    add_custom_target(package
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:qtfiogui>" "${DIST_DIR}/"
        COMMAND Qt6::windeployqt --release --no-translations --no-opengl-sw
                --no-system-d3d-compiler "${DIST_DIR}/qtfiogui.exe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${MSVC_CRT_DLLS} "${DIST_DIR}/"
    # 只带 fio.exe + 许可证/说明；上游的 examples/ 不随包发布
    # （55 个样例 .fio 文件，程序一个都不读）
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_DIR}/fio"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${FIO_DIR}/fio.exe" "${DIST_DIR}/fio/fio.exe"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${FIO_DIR}/COPYING.txt" "${FIO_DIR}/MORAL-LICENSE.txt"
            "${FIO_DIR}/README.txt" "${FIO_DIR}/HOWTO.txt"
            "${FIO_DIR}/REPORTING-BUGS.txt" "${DIST_DIR}/fio/"
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
        DEPENDS qtfiogui
        COMMENT "打包到 ${DIST_DIR}（exe + Qt 运行库 + MSVC 运行库 + 平台插件 + fio）"
        VERBATIM)

    add_custom_target(deploy
        COMMAND Qt6::windeployqt --release --no-translations --no-opengl-sw
                --no-system-d3d-compiler "$<TARGET_FILE:qtfiogui>"
        DEPENDS qtfiogui
        COMMENT "windeployqt: 把 Qt 运行时拷到 exe 旁边")
endif()

# 用 cmake --install 分发时许可证也必须跟着走：GPL 第 4/6 条要求随二进制
# 一并提供许可证文本，LGPL 同样要求附上声明。缺了这一条，装出来的目录
# 就是"没有许可证的二进制"，比不发布还糟。
install(TARGETS qtfiogui RUNTIME DESTINATION .)
install(FILES
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
        DESTINATION licenses)
install(FILES
        "${CMAKE_SOURCE_DIR}/licenses/Qt-LGPLv3.txt"
        "${CMAKE_SOURCE_DIR}/licenses/fio-GPLv2.txt"
        DESTINATION licenses)
