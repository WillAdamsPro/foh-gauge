# Keep this identity stable across builds so macOS can retain microphone consent.
juce_add_gui_app(FOHGauge
    PRODUCT_NAME "FOH Gauge"
    COMPANY_NAME "Will Adams"
    BUNDLE_ID "com.willadams.fohgauge"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "FOH Gauge needs audio input to measure your sound system."
    PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>26.0</string></dict></plist>")
target_sources(FOHGauge PRIVATE Source/Main.cpp Source/MainComponent.cpp Source/SetupPage.cpp Source/SessionFiles.cpp Source/AudioEngine.cpp Source/AnalysisEngine.cpp Source/Widgets.cpp Source/AppModel.cpp Source/LayoutModel.cpp Source/Panels.cpp Source/GuidePanel.cpp Source/WorkspaceView.cpp Source/WorkspaceManager.cpp Source/FloatingPanels.cpp)
if(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(Source/Main.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(FOHGauge PRIVATE "-framework AVFoundation")
endif()
juce_add_binary_data(FOHGaugeData SOURCES Resources/Fonts/Inter-Regular.ttf Resources/Fonts/Inter-SemiBold.ttf Resources/Fonts/Inter-Bold.ttf
    ../resources/targets/target-plus6.txt ../resources/targets/target-plus9.txt ../resources/targets/target-plus12.txt)
set_target_properties(FOHGaugeData PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(FOHGauge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FOHGauge,JUCE_PRODUCT_NAME>" JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FOHGauge,JUCE_VERSION>")
target_link_libraries(FOHGauge PRIVATE fohcore FOHGaugeData juce::juce_audio_utils juce::juce_audio_devices juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(FOH_BUILD_TESTS)
    add_executable(fohapp_tests ../tests/test_audio_bridge.cpp ../tests/test_workspace_presets.cpp ../tests/test_workspace_restore.cpp
        Source/AudioEngine.cpp Source/AnalysisEngine.cpp Source/LayoutModel.cpp Source/WorkspaceManager.cpp)
    target_include_directories(fohapp_tests PRIVATE Source)
    target_compile_definitions(fohapp_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(fohapp_tests PRIVATE fohcore Catch2::Catch2WithMain juce::juce_audio_utils juce::juce_gui_extra
        juce::juce_recommended_config_flags)
    include(Catch)
    catch_discover_tests(fohapp_tests)
endif()
