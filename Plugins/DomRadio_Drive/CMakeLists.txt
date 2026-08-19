juce_add_plugin(DomRadioDrive
    COMPANY_NAME "troakar.labs"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE "TRKR"
    PLUGIN_CODE "DomD"
    FORMATS VST3 Standalone
    PRODUCT_NAME "Dom Radio Drive"
)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

juce_generate_juce_header(DomRadioDrive)

target_include_directories(DomRadioDrive PRIVATE
    ${CMAKE_SOURCE_DIR}/Shared
)

target_sources(DomRadioDrive PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
)

target_compile_definitions(DomRadioDrive PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DomRadioDrive PRIVATE
    juce::juce_audio_plugin_client
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_basics
)

if(WIN32 AND TARGET DomRadioDrive_VST3)
    set_target_properties(DomRadioDrive_VST3 PROPERTIES
        VS_DEBUGGER_COMMAND "C:/Program Files/REAPER (x64)/reaper.exe"
    )
endif()

target_compile_features(DomRadioDrive PUBLIC cxx_std_17)
