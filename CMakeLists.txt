cmake_minimum_required(VERSION 3.21)
project(nebula VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Fast dev loop: the Debug build skips QML AOT compilation (qmlcachegen) and uses a cheap debug level.
# Use ccache and mold when present. UI-only changes need no rebuild: run with NEBULA_QML_DIR=qml (see `just dev`).
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()
find_program(MOLD_PROGRAM mold)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(NEBULA_QML_FAST NO_CACHEGEN)
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g1")
endif()

qt_add_executable(nebula src/main.cpp)

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
    src/hostproto.h src/host.h src/host.cpp src/hostclient.h src/hostclient.cpp
    src/settings.h src/settings.cpp
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
    qml/OnboardingWizard.qml
)

target_link_libraries(nebula PRIVATE Qt6::Quick Qt6::Network PkgConfig::VTERM util)
target_include_directories(nebula PRIVATE src)
target_compile_definitions(nebula PRIVATE NEBULA_VERSION="${PROJECT_VERSION}")
target_compile_options(nebula PRIVATE -Wall -Wextra)
if(MOLD_PROGRAM)
  target_link_options(nebula PRIVATE -fuse-ld=mold)
endif()

enable_testing()
add_executable(test_agentdetect tests/test_agentdetect.cpp src/agentdetect.cpp)
target_include_directories(test_agentdetect PRIVATE src)
target_link_libraries(test_agentdetect PRIVATE Qt6::Core)
add_test(NAME agentdetect COMMAND test_agentdetect)

install(TARGETS nebula RUNTIME DESTINATION bin)
install(FILES data/nebula.desktop DESTINATION share/applications)
install(FILES data/nebula.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION share/licenses/nebula)

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
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, qt6-wayland")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libsecret-tools")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtwayland")
set(CPACK_RPM_PACKAGE_SUGGESTS "libsecret")
# do not claim ownership of standard system directories
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
    /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps /usr/share/licenses)
include(CPack)
