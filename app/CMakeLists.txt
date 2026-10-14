option(FOH_ENABLE_ASIO "Enable Windows ASIO (requires a local Steinberg SDK)" OFF)
set(FOH_ASIO_SDK_DIR "" CACHE PATH "Local ASIO SDK root containing common/asio.h")
add_library(foh_audio_backends INTERFACE)
if(FOH_ENABLE_ASIO)
    if(NOT WIN32)
        message(FATAL_ERROR "FOH_ENABLE_ASIO is only supported on Windows")
    endif()
    if(NOT EXISTS "${FOH_ASIO_SDK_DIR}/common/asio.h" OR
       NOT EXISTS "${FOH_ASIO_SDK_DIR}/common/iasiodrv.h")
        message(FATAL_ERROR "FOH_ENABLE_ASIO requires FOH_ASIO_SDK_DIR pointing to a local ASIO SDK")
    endif()
    target_include_directories(foh_audio_backends INTERFACE "${FOH_ASIO_SDK_DIR}/common")
endif()
if(WIN32)
    target_compile_definitions(foh_audio_backends INTERFACE
        JUCE_WASAPI=1 JUCE_ASIO=$<BOOL:${FOH_ENABLE_ASIO}>)
endif()

# Keep this identity stable across builds so macOS can retain microphone consent.
juce_add_gui_app(FOHGauge
    VERSION "${FOH_APP_VERSION}"
    BUILD_VERSION "${FOH_BUILD_NUMBER}"  # JUCE keyword for CFBundleVersion
    PRODUCT_NAME "FOH Gauge"
    COMPANY_NAME "Will Adams"
    BUNDLE_ID "com.willadams.fohgauge"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "FOH Gauge needs audio input to measure your sound system."
    PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>26.0</string><key>CFBundleIconFile</key><string>AppIcon</string></dict></plist>")
target_sources(FOHGauge PRIVATE Source/console/ConsoleWindow.cpp Source/Main.cpp Source/MainComponent.cpp Source/SetupPage.cpp Source/EasySetup.cpp Source/RoutingWizard.cpp Source/SessionFiles.cpp Source/AudioEngine.cpp Source/AnalysisEngine.cpp Source/AppModelMeasurement.cpp Source/Widgets.cpp Source/AppModel.cpp Source/LayoutModel.cpp Source/Panels.cpp Source/GuidePanel.cpp Source/WorkspaceView.cpp Source/WorkspaceManager.cpp Source/FloatingPanels.cpp Source/Diagrams.cpp)
# Console network control (X32 writes) is experimental: off by default, so release builds do not expose it.
option(FOH_ENABLE_CONSOLE_CONTROL "Expose File > Console Control (experimental)" OFF)
target_compile_definitions(FOHGauge PRIVATE FOH_CONSOLE_CONTROL=$<BOOL:${FOH_ENABLE_CONSOLE_CONTROL}>)
if(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(Source/Main.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(Source/FloatingPanels.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(Source/console/ConsoleWindow.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(../tests/test_console_window.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(Source/Widgets.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(../tests/test_app_lifetime.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(../tests/test_stored_traces.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(../tests/test_show_monitoring.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(FOHGauge PRIVATE "-framework AVFoundation")
endif()
juce_add_binary_data(FOHGaugeData SOURCES Resources/Fonts/Inter-Regular.ttf Resources/Fonts/Inter-SemiBold.ttf Resources/Fonts/Inter-Bold.ttf
    ../resources/targets/target-plus6.txt ../resources/targets/target-plus9.txt ../resources/targets/target-plus12.txt
    ../THIRD_PARTY_NOTICES.md Resources/Fonts/Inter-OFL-LICENSE.txt
    Resources/Brand/splash.png Resources/Brand/about-logo.png
    Resources/Brand/toolbar-lockup-dark.png Resources/Brand/toolbar-lockup-light.png)
# App icon: build AppIcon.icns from the iconset with Apple's iconutil (not a checked-in .icns).
if(APPLE)
    find_program(FOH_ICONUTIL iconutil REQUIRED)
    set(foh_icns "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns")
    execute_process(COMMAND "${FOH_ICONUTIL}" -c icns "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Brand/AppIcon.iconset" -o "${foh_icns}"
                    RESULT_VARIABLE foh_iconutil_result ERROR_VARIABLE foh_iconutil_error)
    if(NOT foh_iconutil_result EQUAL 0)
        message(FATAL_ERROR "iconutil failed: ${foh_iconutil_error}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Brand/AppIcon.iconset/icon_512x512@2x.png")
    target_sources(FOHGauge PRIVATE "${foh_icns}")
    set_source_files_properties("${foh_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
# Also ship human-readable files in Contents/Resources, outside the executable.
set(foh_notices "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/Inter-OFL-LICENSE.txt")
target_sources(FOHGauge PRIVATE ${foh_notices})
set_source_files_properties(${foh_notices} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_target_properties(FOHGaugeData PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(FOHGauge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FOHGauge,JUCE_PRODUCT_NAME>" JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FOHGauge,JUCE_VERSION>")
target_link_libraries(FOHGauge PRIVATE fohcore fohconsole foh_sanitizers foh_audio_backends FOHGaugeData juce::juce_audio_utils juce::juce_audio_devices juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(FOH_TSAN)
    target_link_libraries(FOHGauge PRIVATE foh_sanitizers)
endif()

if(FOH_BUILD_TESTS)
    add_executable(fohapp_tests ../tests/test_acknowledgements.cpp ../tests/test_console_window.cpp ../tests/test_audio_bridge.cpp ../tests/test_workspace_presets.cpp ../tests/test_workspace_restore.cpp ../tests/test_guide_panel.cpp ../tests/test_routing_wizard.cpp ../tests/test_app_lifetime.cpp ../tests/test_easy_setup.cpp ../tests/test_stored_traces.cpp ../tests/test_show_monitoring.cpp ../tests/test_signal_views.cpp ../tests/test_long_session.cpp ../tests/test_target_overlays.cpp
        Source/console/ConsoleWindow.cpp Source/AudioEngine.cpp Source/AnalysisEngine.cpp Source/AppModelMeasurement.cpp Source/LayoutModel.cpp Source/WorkspaceManager.cpp Source/WorkspaceView.cpp Source/FloatingPanels.cpp Source/Widgets.cpp Source/GuidePanel.cpp Source/RoutingWizard.cpp Source/EasySetup.cpp Source/SetupPage.cpp Source/Panels.cpp Source/MainComponent.cpp Source/SessionFiles.cpp Source/Diagrams.cpp Source/SessionFiles.cpp)
    target_include_directories(fohapp_tests PRIVATE Source)
    target_compile_definitions(fohapp_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(fohapp_tests PRIVATE fohcore fohconsole foh_sanitizers foh_audio_backends FOHGaugeData Catch2::Catch2WithMain juce::juce_audio_utils juce::juce_gui_extra
        juce::juce_recommended_config_flags)
    if(FOH_TSAN)
        target_link_libraries(fohapp_tests PRIVATE foh_sanitizers)
    endif()
    include(Catch)
    catch_discover_tests(fohapp_tests)
endif()
