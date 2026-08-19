juce_add_plugin(TroakarSpectral
    COMPANY_NAME "troakar.labs"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "TRKR"
    PLUGIN_CODE "TrkS"
    FORMATS VST3 Standalone
    PRODUCT_NAME "TROAKAR SPECTRAL"
    NEEDS_WEBVIEW2 TRUE
)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

juce_generate_juce_header(TroakarSpectral)

# Web UI assets (HTML, CSS, JS, fonts) — collected before binary data
file(GLOB_RECURSE WEB_ASSETS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Web/index.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Web/css/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Web/css/components/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Web/js/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Web/js/components/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Web/js/models/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Web/js/config/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Web/fonts/*"
)

juce_add_binary_data(TroakarSpectralBinaryData
    NAMESPACE BinaryData
    SOURCES
        ${CMAKE_SOURCE_DIR}/Shared/UI_Assets/GOSTtypeB.ttf
        ${CMAKE_SOURCE_DIR}/Shared/UI_Assets/RussoOne-Regular.ttf
        ${WEB_ASSETS}
)

target_include_directories(TroakarSpectral PRIVATE
    ${CMAKE_SOURCE_DIR}/Shared
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_sources(TroakarSpectral PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/SpectralEngine.h
    ${CMAKE_SOURCE_DIR}/Shared/UI/LabeledKnob.h
    ${CMAKE_SOURCE_DIR}/Shared/UI/DomRadioLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/Shared/UI/DomRadioLookAndFeel.h
    ${CMAKE_SOURCE_DIR}/Shared/UI/RetroFX.h
)

if(WIN32)
    target_compile_definitions(TroakarSpectral PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    )
else()
    target_compile_definitions(TroakarSpectral PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=1
    )
endif()

target_link_libraries(TroakarSpectral PRIVATE
    juce::juce_audio_plugin_client
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    TroakarSpectralBinaryData
)

if(WIN32 AND TARGET TroakarSpectral_VST3)
    set_target_properties(TroakarSpectral_VST3 PROPERTIES
        VS_DEBUGGER_COMMAND "C:/Program Files/REAPER (x64)/reaper.exe"
    )
endif()

target_compile_features(TroakarSpectral PUBLIC cxx_std_17)
