cmake_minimum_required(VERSION 3.21)
project(nebula VERSION 0.1.0 LANGUAGES CXX)

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
