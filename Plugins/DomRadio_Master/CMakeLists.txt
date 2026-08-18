juce_add_plugin(DomRadioMaster
    COMPANY_NAME "troakar.labs"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "TRKR"
    PLUGIN_CODE "DomM"
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "DOM RADIO MECHLABOR 610"
    NEEDS_WEBVIEW2 TRUE
)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

juce_generate_juce_header(DomRadioMaster)

# Web UI assets (HTML, CSS, JS, fonts) — collected before binary data
file(GLOB_RECURSE WEB_ASSETS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Web/index.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/Web/css/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/Web/css/components/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/Web/js/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/Web/js/components/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/Web/js/models/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/Web/js/config/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/Web/fonts/*"
)

juce_add_binary_data(DomRadioMasterBinaryData
    NAMESPACE BinaryData
    SOURCES
        ${CMAKE_SOURCE_DIR}/Shared/noises/noise_mid_velvet_and_soft.mp3
        ${CMAKE_SOURCE_DIR}/Shared/noises/noise_high_vinyl_like.mp3
        ${CMAKE_SOURCE_DIR}/Shared/UI_Assets/GOSTtypeB.ttf
        ${CMAKE_SOURCE_DIR}/Shared/UI_Assets/RussoOne-Regular.ttf
        ${WEB_ASSETS}
)

target_include_directories(DomRadioMaster PRIVATE
    ${CMAKE_SOURCE_DIR}/Shared
    ${CMAKE_SOURCE_DIR}/Shared/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_sources(DomRadioMaster PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/UI/VUMeterPro.cpp
    Source/UI/VUMeterPro.h
    Source/UI/EQGraphLED.cpp
    Source/UI/EQGraphLED.h
    Source/UI/SaturationIndicator.cpp
    Source/UI/SaturationIndicator.h
    ${CMAKE_SOURCE_DIR}/Shared/UI/DomRadioLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/Shared/UI/DomRadioLookAndFeel.h
    ${CMAKE_SOURCE_DIR}/Shared/UI/LabeledKnob.h
    ${CMAKE_SOURCE_DIR}/Shared/UI/RetroFX.h
)

target_compile_definitions(DomRadioMaster PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
)

target_link_libraries(DomRadioMaster PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    DomRadioMasterBinaryData
)

if(WIN32 AND TARGET DomRadioMaster_VST3)
    set_target_properties(DomRadioMaster_VST3 PROPERTIES
        VS_DEBUGGER_COMMAND "C:/Program Files/REAPER (x64)/reaper.exe"
    )
endif()

enable_testing()
add_test(NAME pluginval
    COMMAND pluginval --strictness-level 10 --validate
        "$<TARGET_FILE:DomRadioMaster_VST3>"
)

target_compile_features(DomRadioMaster PUBLIC cxx_std_17)
