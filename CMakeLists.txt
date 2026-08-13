cmake_minimum_required(VERSION 3.22)
project(MahachkalaEcosystem VERSION 1.0.0)

add_subdirectory(modules/JUCE)

include_directories(Shared)

add_subdirectory(Plugins/DomRadio_Master)
add_subdirectory(Plugins/DomRadio_Drive)
add_subdirectory(Plugins/DomRadio_Track)
add_subdirectory(Plugins/Troakar_Spectral)
