# Keep the QML target beside its QML files: Qt's import scanner recursively
# scans SOURCE_DIR. The project root also contains the local Qt SDK and artifacts.
qt_add_executable(IslandDesktop WIN32
    ../src/main.cpp ../src/mediacontroller.cpp ../src/mediacontroller.h
    ../src/shellcontroller.cpp ../src/shellcontroller.h
    ../src/volumecontroller.cpp ../src/volumecontroller.h
    ../src/audiocontroller.cpp ../src/audiocontroller.h
    ../src/monitorcontroller.cpp ../src/monitorcontroller.h ../src/monitorselection.h)
set_target_properties(IslandDesktop PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_libraries(IslandDesktop PRIVATE island_core Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Network)
set(island_qml_files)
foreach(name Main IslandColors LineIcon ActionButton PlaybackIndicator ClockPage TimerPage MusicPage SettingsPage VolumePage SoundSlider AudioMeter)
    set(qml_file "${PROJECT_SOURCE_DIR}/qml/${name}.qml")
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${name}.qml")
    list(APPEND island_qml_files "${qml_file}")
endforeach()
qt_add_qml_module(IslandDesktop
    URI Island VERSION 1.0
    OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/Island"
    SOURCES ../src/islandsurface.cpp ../src/islandsurface.h
    QML_FILES ${island_qml_files})
if(MSVC)
    target_compile_definitions(IslandDesktop PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(IslandDesktop PRIVATE /W4 /utf-8)
    target_link_libraries(IslandDesktop PRIVATE windowsapp ole32 uuid user32 gdi32)
else()
    target_compile_options(IslandDesktop PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS IslandDesktop RUNTIME DESTINATION . BUNDLE DESTINATION .)
qt_generate_deploy_qml_app_script(TARGET IslandDesktop OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
install(FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/dynamic-island/LICENSE" DESTINATION licenses/dynamic-island)
