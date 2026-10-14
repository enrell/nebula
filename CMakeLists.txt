cmake_minimum_required(VERSION 3.21)
project(nebula VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Network Qml WebEngineQuick WebChannel)

# libvterm: the system one where there is one (Linux), the copy in third_party/ otherwise (Windows) or on request
option(NEBULA_BUNDLED_VTERM "Build the bundled libvterm instead of using the system one" ${WIN32})
if(NEBULA_BUNDLED_VTERM)
  enable_language(C)
  file(GLOB NEBULA_VTERM_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/third_party/libvterm/src/*.c)
  add_library(vterm STATIC ${NEBULA_VTERM_SOURCES})
  target_include_directories(vterm PUBLIC third_party/libvterm/include PRIVATE third_party/libvterm/src)
  set_target_properties(vterm PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(vterm PRIVATE /W0)
    target_compile_definitions(vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(vterm PRIVATE -w)
  endif()
  set(NEBULA_VTERM_LIB vterm)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
  set(NEBULA_VTERM_LIB PkgConfig::VTERM)
endif()

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Fast dev loop: the Debug build skips QML AOT compilation (qmlcachegen) and uses a cheap debug level.
# Use ccache and mold when present. UI-only changes need no rebuild: run with NEBULA_QML_DIR=qml (see `just dev`).
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()
if(NOT MSVC)
  find_program(MOLD_PROGRAM mold)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
  set(NEBULA_QML_FAST NO_CACHEGEN)
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g1")
endif()

qt_add_executable(nebula src/main.cpp)

# what differs between the systems: the pseudo terminal, and on Windows the process, console and credential helpers
if(WIN32)
  set(NEBULA_PLATFORM_SOURCES src/ptyprocess_win.cpp src/win/conpty.h src/win/conpty.cpp src/win/winproc.h src/win/winproc.cpp
                              src/win/winsecret.h src/win/winsecret.cpp)
  set_target_properties(nebula PROPERTIES WIN32_EXECUTABLE ON)   # a GUI program: no console window (see main.cpp for ctl/mcp)
else()
  set(NEBULA_PLATFORM_SOURCES src/ptyprocess_posix.cpp)
endif()

qt_add_qml_module(nebula
  URI Nebula
  VERSION 1.0
  ${NEBULA_QML_FAST}
  SOURCES
    src/theme.h src/theme.cpp
    src/terminalsession.h src/terminalsession.cpp
    src/terminalview.h src/terminalview.cpp
    src/workspace.h src/workspace.cpp
    src/agentdetect.h src/agentdetect.cpp
    src/apiserver.h src/apiserver.cpp
    src/cli.h src/cli.cpp
    src/paths.h
    src/secrets.h src/secrets.cpp src/profiles.h src/profiles.cpp src/llm.h src/llm.cpp
    src/integrations.h src/integrations.cpp src/launcher.h src/launcher.cpp src/mcp.h src/mcp.cpp
    src/tools.h src/tools.cpp src/operator.h src/operator.cpp src/acp.h src/acp.cpp src/nativeagents.h src/nativeagents.cpp
    src/ptyprocess.h ${NEBULA_PLATFORM_SOURCES}
    src/platform.h src/platform.cpp src/procinfo.h src/procinfo.cpp
    src/hostproto.h src/host.h src/host.cpp src/hostclient.h src/hostclient.cpp
    src/settings.h src/settings.cpp
    src/paneids.h
    src/views/viewengine.h src/views/viewengine.cpp src/views/viewfiles.h src/views/viewfiles.cpp
    src/views/viewpane.h src/views/viewpane.cpp src/views/viewweb.h src/views/viewweb.cpp
  QML_FILES
    qml/Main.qml
    qml/Icon.qml
    qml/IconButton.qml
    qml/Toggle.qml
    qml/Stepper.qml
    qml/Field.qml
    qml/SettingRow.qml
    qml/SettingsPage.qml
    qml/ContextMenu.qml
    qml/Button.qml
    qml/Choice.qml
    qml/Dialog.qml
    qml/LauncherDialog.qml
    qml/OperatorPanel.qml
    qml/BroadcastDialog.qml
    qml/Sidebar.qml
    qml/TabBar.qml
    qml/PaneTree.qml
    qml/PaneFrame.qml
    qml/Badge.qml
    qml/Dropdown.qml
    qml/Kbd.qml
    qml/Card.qml
    qml/StatusBar.qml
    qml/StateMark.qml
    qml/OnboardingWizard.qml
    qml/ViewContent.qml
    qml/ViewModal.qml
)

# Views: the document checker and the page renderer, built from renderer/ (see renderer/package.json).
# renderer/dist is committed, so building nebula needs no node.
# Chunk and asset names carry content hashes, so the list is globbed (CONFIGURE_DEPENDS re-globs on every build).
file(GLOB_RECURSE NEBULA_RENDERER_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/renderer/dist/*)
list(FILTER NEBULA_RENDERER_FILES EXCLUDE REGEX "THIRD_PARTY_LICENSES\\.txt$")   # installed as a file, not served
qt_add_resources(nebula renderer
  PREFIX /nebula/renderer
  BASE renderer/dist
  FILES ${NEBULA_RENDERER_FILES}
)

target_link_libraries(nebula PRIVATE Qt6::Quick Qt6::Network Qt6::Qml Qt6::WebEngineQuick Qt6::WebChannel ${NEBULA_VTERM_LIB})
if(WIN32)
  target_link_libraries(nebula PRIVATE shell32 advapi32)
else()
  target_link_libraries(nebula PRIVATE util)
endif()
target_include_directories(nebula PRIVATE src)
target_compile_definitions(nebula PRIVATE NEBULA_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(nebula PRIVATE /W3 /utf-8 /permissive-)
  target_compile_definitions(nebula PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(nebula PRIVATE -Wall -Wextra)
endif()
if(MOLD_PROGRAM)
  target_link_options(nebula PRIVATE -fuse-ld=mold)
endif()

enable_testing()
add_executable(test_agentdetect tests/test_agentdetect.cpp src/agentdetect.cpp)
target_include_directories(test_agentdetect PRIVATE src)
target_link_libraries(test_agentdetect PRIVATE Qt6::Core)
add_test(NAME agentdetect COMMAND test_agentdetect)

# the view checker inside V4 with the real file sandbox, over the same cases as `npm test` in renderer/
add_executable(test_views tests/test_views.cpp src/views/viewengine.cpp src/views/viewfiles.cpp)
qt_add_resources(test_views renderer_core PREFIX /nebula/renderer BASE renderer/dist FILES renderer/dist/core.js)
target_include_directories(test_views PRIVATE src)
target_link_libraries(test_views PRIVATE Qt6::Qml)
add_test(NAME views COMMAND test_views ${CMAKE_SOURCE_DIR}/renderer/test)

if(WIN32)
  # the Win32 layer on its own, without Qt: a program on a pseudo console, process inspection, credentials
  add_executable(win_probe tests/win_probe.cpp src/win/conpty.cpp src/win/winproc.cpp src/win/winsecret.cpp)
  target_include_directories(win_probe PRIVATE src)
  target_link_libraries(win_probe PRIVATE shell32 advapi32)
  if(MSVC)
    target_compile_options(win_probe PRIVATE /W3 /utf-8)
  endif()
  add_test(NAME win_probe COMMAND win_probe)
endif()

if(WIN32)
  # a portable folder: nebula.exe, licenses next to it; the release workflow adds Qt with windeployqt
  install(TARGETS nebula RUNTIME DESTINATION .)
  install(FILES LICENSE renderer/dist/THIRD_PARTY_LICENSES.txt third_party/libvterm/LICENSE DESTINATION licenses)
else()
  install(TARGETS nebula RUNTIME DESTINATION bin)
  install(FILES data/nebula.desktop DESTINATION share/applications)
  install(FILES data/nebula.svg DESTINATION share/icons/hicolor/scalable/apps)
  install(FILES LICENSE renderer/dist/THIRD_PARTY_LICENSES.txt DESTINATION share/licenses/nebula)
endif()

# Native .deb / .rpm packages (`cpack -G DEB` / `cpack -G RPM` in the build dir). Library dependencies are computed
# from the binary (dpkg-shlibdeps / rpm autoreq); QML and Wayland runtime modules are not linked, so list them here.
set(CPACK_PACKAGE_NAME nebula)
set(CPACK_PACKAGE_VENDOR enrell)
set(CPACK_PACKAGE_CONTACT "enrell <enrellsa10@proton.me>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Qt Quick workspace manager for terminal AI agents")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/enrell/nebula")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION devel)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, qml6-module-qtwebengine, qml6-module-qtwebchannel, qt6-wayland")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libsecret-tools")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtwayland, qt6-qtwebengine, qt6-qtwebchannel")
set(CPACK_RPM_PACKAGE_SUGGESTS "libsecret")
# do not claim ownership of standard system directories
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
    /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps /usr/share/licenses)
include(CPack)
