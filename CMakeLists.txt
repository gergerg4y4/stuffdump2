cmake_minimum_required(VERSION 3.22)
project(stuffdump)

add_library(stuffdump SHARED stuffdump.cpp)

target_link_libraries(stuffdump
    android
    log
    dl
)

set_target_properties(stuffdump PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
