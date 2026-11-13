# Keep the QML target beside its QML files: Qt's import scanner recursively
# scans SOURCE_DIR. The project root also contains the local Qt SDK and artifacts.
# Shared controller interfaces live in src/platform; each OS provides the
# implementations in its own folder.
if(WIN32)
    set(island_platform_dir "${PROJECT_SOURCE_DIR}/src/platform/windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(island_platform_dir "${PROJECT_SOURCE_DIR}/src/platform/linux")
    find_package(LayerShellQt 6.6 REQUIRED)
    find_package(KF6GlobalAccel REQUIRED)
    find_package(LibTaskManager REQUIRED)
    find_package(KF6Service REQUIRED)
    find_package(KF6IconThemes REQUIRED)
    find_package(KF6Kirigami REQUIRED)
    find_package(KF6KIO REQUIRED)
else()
    message(FATAL_ERROR "No platform backend for ${CMAKE_SYSTEM_NAME} yet: add src/platform/linux.")
endif()
qt_add_executable(IslandDesktop WIN32
    ../src/main.cpp ../src/platform/shellgesture.cpp
    ../src/platform/desktopcontroller.h "${island_platform_dir}/desktopcontroller.cpp"
    ../src/platform/taskbarcontroller.h "${island_platform_dir}/taskbarcontroller.cpp"
    ../src/platform/mediacontroller.h ../src/platform/shellcontroller.h
    ../src/platform/volumecontroller.h ../src/platform/audiocontroller.h
    ../src/platform/startupcontroller.h ../src/platform/hotkeycontroller.h
    ../src/platform/powercontroller.h ../src/platform/bluetoothcontroller.h
    "${island_platform_dir}/mediacontroller.cpp" "${island_platform_dir}/shellcontroller.cpp"
    "${island_platform_dir}/volumecontroller.cpp" "${island_platform_dir}/audiocontroller.cpp"
    "${island_platform_dir}/startupcontroller.cpp" "${island_platform_dir}/hotkeycontroller.cpp"
    "${island_platform_dir}/powercontroller.cpp" "${island_platform_dir}/bluetoothcontroller.cpp"
    ../src/core/monitorcontroller.cpp ../src/core/monitorcontroller.h ../src/core/monitorselection.h)
target_include_directories(IslandDesktop PRIVATE "${PROJECT_SOURCE_DIR}/src/platform")
set_target_properties(IslandDesktop PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_libraries(IslandDesktop PRIVATE island_core Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Network)
if(WIN32)
    # Home panel: task list, Start-menu applications and the notification-area host.
    target_sources(IslandDesktop PRIVATE ../src/core/tasklist.h
        ../src/platform/windows/shellapps.cpp ../src/platform/windows/shellapps.h
        ../src/platform/windows/taskwatcher.cpp ../src/platform/windows/taskwatcher.h
        ../src/platform/windows/tasksmodel.h
        ../src/platform/windows/trayhost.cpp ../src/platform/windows/trayhost.h)
    target_sources(IslandDesktop PRIVATE ../src/platform/windows/taskbarguardian.cpp ../src/platform/windows/taskbarguardian.h)
    target_sources(IslandDesktop PRIVATE ../src/platform/windows/taskbarworkarea.cpp ../src/platform/windows/taskbarworkarea.h)
    target_include_directories(IslandDesktop PRIVATE "${PROJECT_SOURCE_DIR}/src/platform/windows")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(IslandDesktop PRIVATE ../src/platform/linux/pulseworker.cpp
        ../src/platform/linux/kwinpointer.cpp ../src/platform/linux/kwinpointer.h)
    set_source_files_properties(../src/platform/linux/kwin-pointer.js PROPERTIES QT_RESOURCE_ALIAS kwin-pointer.js)
    qt_add_resources(IslandDesktop linux_pointer PREFIX "/linux" FILES ../src/platform/linux/kwin-pointer.js)
    target_link_libraries(IslandDesktop PRIVATE Qt6::DBus PkgConfig::PULSE PkgConfig::XCBSHAPE
        LayerShellQt::Interface KF6::GlobalAccel KF6::WindowSystem PW::LibTaskManager KF6::Service KF6::KIOGui KF6::IconThemes)
endif()
set(island_qml_files)
foreach(name Main SettingsWindow HomePage DesktopPanel DesktopButton DesktopIcon IslandEyes IslandColors LineIcon ActionButton PlaybackIndicator TimePage ClockPage TimerPage MusicPage SettingsPage SoundSlider AudioMeter HotkeyField DevicesPage)
    set(qml_file "${PROJECT_SOURCE_DIR}/qml/${name}.qml")
    # DesktopIcon is the one platform-specific QML type: Kirigami on Linux,
    # the island's own line glyphs and shell icons on Windows.
    if(WIN32 AND name STREQUAL "DesktopIcon")
        set(qml_file "${PROJECT_SOURCE_DIR}/qml/windows/DesktopIcon.qml")
    endif()
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${name}.qml")
    list(APPEND island_qml_files "${qml_file}")
endforeach()
qt_add_qml_module(IslandDesktop
    URI Island VERSION 1.0
    OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/Island"
    SOURCES ../src/core/islandsurface.cpp ../src/core/islandsurface.h
    QML_FILES ${island_qml_files})
if(MSVC)
    target_compile_definitions(IslandDesktop PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(IslandDesktop PRIVATE /W4 /utf-8)
    target_link_libraries(IslandDesktop PRIVATE windowsapp ole32 uuid user32 gdi32 setupapi cfgmgr32 bthprops
        shell32 dwmapi version wtsapi32)
else()
    target_compile_options(IslandDesktop PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(TARGETS IslandDesktop RUNTIME DESTINATION bin)
    install(FILES "${PROJECT_SOURCE_DIR}/share/applications/io.github.iwtkmsss.island-desktop.desktop" DESTINATION share/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION share/doc/IslandDesktop)
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/dynamic-island/LICENSE" DESTINATION share/licenses/IslandDesktop)
else()
    install(TARGETS IslandDesktop RUNTIME DESTINATION . BUNDLE DESTINATION .)
    qt_generate_deploy_qml_app_script(TARGET IslandDesktop OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
    install(FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION .)
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/dynamic-island/LICENSE" DESTINATION licenses/dynamic-island)

endif()
