cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/version.cmake)

# Draftling does not yet support ESP-IDF 6.0 because the m5stack/M5GFX
# managed component (used by the M5Stack PaperS3 display backend) is not
# compatible with ESP-IDF 6.0. Refuse to configure under 6.x so users get
# a clear message instead of a confusing downstream build error.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    message(FATAL_ERROR
        "Draftling does not yet support ESP-IDF 6.0 or later "
        "(detected ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}). "
        "The m5stack/M5GFX dependency is not compatible with ESP-IDF 6.x. "
        "Please use ESP-IDF v5.3 - v5.x.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(draftling)
