cmake_minimum_required(VERSION 3.22)
project(MahachkalaEcosystem VERSION 1.0.0)

# Windows WebView2 + resource provider must be set BEFORE JUCE modules are parsed
set(JUCE_USE_WIN_WEBVIEW2 ON CACHE BOOL "Enable WebView2 engine for juce::WebBrowserComponent" FORCE)
set(JUCE_WEB_BROWSER ON CACHE BOOL "Enable juce::WebBrowserComponent" FORCE)
set(JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING ON CACHE BOOL "Statically link WebView2Loader" FORCE)

add_subdirectory(modules/JUCE)

include_directories(Shared)

add_subdirectory(Plugins/DomRadio_Master)
add_subdirectory(Plugins/DomRadio_Drive)
add_subdirectory(Plugins/DomRadio_Track)
add_subdirectory(Plugins/Troakar_Spectral)
