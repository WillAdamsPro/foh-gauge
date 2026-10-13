juce_add_gui_app(FOHGauge
    PRODUCT_NAME "FOH Gauge"
    COMPANY_NAME "Will Adams"
    BUNDLE_ID "com.willadams.fohgauge"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "FOH Gauge needs audio input to measure your sound system.")
target_sources(FOHGauge PRIVATE Source/Main.cpp Source/MainComponent.cpp Source/AudioEngine.cpp Source/AnalysisEngine.cpp)
juce_add_binary_data(FOHGaugeData SOURCES Resources/Fonts/Inter-Regular.ttf Resources/Fonts/Inter-SemiBold.ttf Resources/Fonts/Inter-Bold.ttf)
set_target_properties(FOHGaugeData PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(FOHGauge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FOHGauge,JUCE_PRODUCT_NAME>" JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FOHGauge,JUCE_VERSION>")
target_link_libraries(FOHGauge PRIVATE fohcore FOHGaugeData juce::juce_audio_utils juce::juce_audio_devices juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
