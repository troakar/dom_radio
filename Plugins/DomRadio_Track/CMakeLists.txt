juce_add_plugin(DomRadioTrack
    COMPANY_NAME "troakar.labs"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "TRKR"
    PLUGIN_CODE "DomT"
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "DOM RADIO TRACK"
)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

juce_generate_juce_header(DomRadioTrack)

juce_add_binary_data(DomRadioTrackBinaryData
    NAMESPACE BinaryData
    SOURCES
    ${CMAKE_SOURCE_DIR}/Shared/noises/noise_mid_velvet_and_soft.mp3
    ${CMAKE_SOURCE_DIR}/Shared/noises/noise_high_vinyl_like.mp3
    ${CMAKE_SOURCE_DIR}/Shared/UI_Assets/GOSTtypeB.ttf
    ${CMAKE_SOURCE_DIR}/Shared/UI_Assets/RussoOne-Regular.ttf
)

target_include_directories(DomRadioTrack PRIVATE
    ${CMAKE_SOURCE_DIR}/Shared
    ${CMAKE_SOURCE_DIR}/Shared/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_sources(DomRadioTrack PRIVATE
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

target_compile_definitions(DomRadioTrack PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DomRadioTrack PRIVATE
    juce::juce_audio_plugin_client
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_basics
    DomRadioTrackBinaryData
)

if(WIN32 AND TARGET DomRadioTrack_VST3)
    set_target_properties(DomRadioTrack_VST3 PROPERTIES
        VS_DEBUGGER_COMMAND "C:/Program Files/REAPER (x64)/reaper.exe"
    )
endif()

target_compile_features(DomRadioTrack PUBLIC cxx_std_17)
