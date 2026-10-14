add_library(tfv_core STATIC
    domain/BookmarkModel.cpp domain/BookmarkModel.h
    domain/ProjectDocument.cpp domain/ProjectDocument.h
    media/FrameCache.h media/Decoder.cpp media/Decoder.h
    media/VideoFrame.cpp media/VideoFrame.h
    media/MediaWorker.cpp media/MediaWorker.h
    media/AudioEngine.cpp media/AudioEngine.h
    application/Preferences.cpp application/Preferences.h
    application/PlayerController.cpp application/PlayerController.h
    platform/WindowsIntegration.cpp platform/WindowsIntegration.h
    platform/ResourceMonitor.cpp platform/ResourceMonitor.h
)
target_include_directories(tfv_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tfv_core PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia FFmpeg::FFmpeg)
set_source_files_properties(../config/defaults.json PROPERTIES QT_RESOURCE_ALIAS "config/defaults.json")
qt_add_resources(tfv_core tfv_defaults PREFIX "/tfv" FILES ../config/defaults.json)
set_source_files_properties(../config/windows-integration.json PROPERTIES QT_RESOURCE_ALIAS "config/windows-integration.json")
qt_add_resources(tfv_core tfv_windows_integration PREFIX "/tfv" FILES ../config/windows-integration.json)
if(WIN32)
    target_link_libraries(tfv_core PUBLIC shell32 psapi pdh)
endif()
target_compile_definitions(tfv_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(tfv_core PRIVATE /W4 /permissive- /utf-8)
endif()
qt_add_executable(TrieFrameViewer WIN32 app/main.cpp
    presentation/VideoItem.cpp presentation/VideoItem.h
    presentation/YuvNode.cpp presentation/YuvNode.h
    presentation/WindowChrome.cpp presentation/WindowChrome.h)
target_sources(TrieFrameViewer PRIVATE presentation/WindowDiagnostics.cpp presentation/WindowDiagnostics.h)

target_compile_definitions(TrieFrameViewer PRIVATE TFV_APP_VERSION="${PROJECT_VERSION}")

configure_file(app/windows.rc.in "${CMAKE_CURRENT_BINARY_DIR}/windows.rc" @ONLY)
target_sources(TrieFrameViewer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
qt_add_resources(TrieFrameViewer tfv_branding
    PREFIX "/tfv" BASE "../assets"
    FILES ../assets/branding/fv-icon.svg)
target_link_libraries(TrieFrameViewer PRIVATE tfv_core Qt6::Quick Qt6::QuickControls2)
target_link_libraries(TrieFrameViewer PRIVATE Qt6::GuiPrivate)
qt_add_shaders(TrieFrameViewer tfv_video_shaders
    BATCHABLE
    PREFIX "/tfv" BASE "presentation"
    FILES presentation/shaders/yuv.vert presentation/shaders/yuv.frag)
if(WIN32)
    target_link_libraries(TrieFrameViewer PRIVATE dwmapi d3d11 dxgi)
endif()
if(BUILD_TESTING)
    target_link_libraries(TrieFrameViewer PRIVATE Qt6::Test)
    target_sources(TrieFrameViewer PRIVATE ../tests/AppSmoke.cpp ../tests/PerformanceBench.cpp)
    target_link_libraries(TrieFrameViewer PRIVATE psapi d3d11 dxgi)
    target_compile_definitions(TrieFrameViewer PRIVATE TFV_DEVELOPMENT_TESTS)
endif()
set_source_files_properties(ui/Theme.qml ui/IconPaths.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(TrieFrameViewer
    URI Trie.FrameViewer VERSION 1.0
    QML_FILES
        ui/Main.qml ui/Theme.qml ui/IconPaths.qml
        ui/components/Icon.qml ui/components/ToolButton.qml ui/components/AppMenuBar.qml
        ui/components/Chip.qml ui/components/SectionLabel.qml
        ui/components/DebugOverlay.qml
        ui/components/SelectControl.qml ui/components/ValueSlider.qml ui/components/FrameInput.qml
        ui/components/Timeline.qml ui/components/BookmarkEditor.qml
        ui/views/Viewer.qml ui/views/Transport.qml ui/views/ReviewStage.qml
        ui/views/SettingsDialog.qml
)
set_target_properties(TrieFrameViewer PROPERTIES OUTPUT_NAME "TrieFrameViewer")
install(TARGETS TrieFrameViewer RUNTIME DESTINATION .)
file(GLOB ffmpeg_dlls "${FFMPEG_ROOT}/bin/*.dll")
foreach(dll IN LISTS ffmpeg_dlls)
    add_custom_command(TARGET TrieFrameViewer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:TrieFrameViewer>")
    install(FILES "${dll}" DESTINATION .)
endforeach()
qt_generate_deploy_qml_app_script(TARGET TrieFrameViewer OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT "${deploy_script}")
