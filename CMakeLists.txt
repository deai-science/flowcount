cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "1.0.0-prototype")
set_property(GLOBAL PROPERTY JOB_POOLS pc_compile=2)
set(CMAKE_JOB_POOL_COMPILE pc_compile)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# esp_ipa splits its input list on spaces, even for a quoted single filename.
# Stage the sensor's tuning JSON in a space-free cache; do not patch dependencies.
if(DEFINED ENV{USERPROFILE})
    set(pc_ipa_default_cache "$ENV{USERPROFILE}/.cache/esp32-p4-people-counter")
else()
    set(pc_ipa_default_cache "$ENV{HOME}/.cache/esp32-p4-people-counter")
endif()
set(PC_IPA_CACHE_DIR "${pc_ipa_default_cache}" CACHE PATH "Space-free path for esp_ipa JSON")
if(PC_IPA_CACHE_DIR MATCHES "[ \t]")
    message(FATAL_ERROR "Set PC_IPA_CACHE_DIR to a path without spaces (esp_ipa input parser limitation).")
endif()
file(MAKE_DIRECTORY "${PC_IPA_CACHE_DIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/config/imx219_default.json"
    "${PC_IPA_CACHE_DIR}/imx219_default.json" COPYONLY)
idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH
    "${PC_IPA_CACHE_DIR}/imx219_default.json" APPEND)
# ESP-DL brings large static libraries. Do not retain all linker input tables
# in RAM; this also makes builds usable on memory-constrained Windows hosts.
idf_build_set_property(LINK_OPTIONS "-Wl,--no-keep-memory" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--reduce-memory-overheads" APPEND)
project(people_counter)
