# Keep the QML target beside its QML files: Qt's import scanner recursively
# scans SOURCE_DIR. The project root also contains the local Qt SDK and artifacts.
# Shared controller interfaces live in src/platform; each OS provides the
# implementations in its own folder.
if(WIN32)
    set(island_platform_dir "${PROJECT_SOURCE_DIR}/src/platform/windows")
else()
    message(FATAL_ERROR "No platform backend for ${CMAKE_SYSTEM_NAME} yet: add src/platform/linux.")
endif()
qt_add_executable(IslandDesktop WIN32
    ../src/main.cpp
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
set(island_qml_files)
foreach(name Main HomePage IslandEyes IslandColors LineIcon ActionButton PlaybackIndicator TimePage ClockPage TimerPage MusicPage SettingsPage SoundSlider AudioMeter HotkeyField DevicesPage)
    set(qml_file "${PROJECT_SOURCE_DIR}/qml/${name}.qml")
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
    target_link_libraries(IslandDesktop PRIVATE windowsapp ole32 uuid user32 gdi32 setupapi cfgmgr32 bthprops)
else()
    target_compile_options(IslandDesktop PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS IslandDesktop RUNTIME DESTINATION . BUNDLE DESTINATION .)
qt_generate_deploy_qml_app_script(TARGET IslandDesktop OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
install(FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/dynamic-island/LICENSE" DESTINATION licenses/dynamic-island)
