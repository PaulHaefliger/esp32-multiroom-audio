cmake_minimum_required(VERSION 3.16)

# Pull in ONLY the shared components, not the whole central components/ dir
# (that would drag bt_sink into a build with Bluetooth disabled).
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../multiroom-audio/components/ring_buffer
    ${CMAKE_CURRENT_LIST_DIR}/../multiroom-audio/components/i2s_output
    ${CMAKE_CURRENT_LIST_DIR}/../multiroom-audio/components/audio_proto
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(satellite-audio)
