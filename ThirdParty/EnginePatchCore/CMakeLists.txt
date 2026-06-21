cmake_minimum_required(VERSION 3.16)
project(EnginePatchCore)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(EnginePatchCore STATIC
    src/EnginePatchManager.cpp
    src/EnginePatchFileLoader.cpp
)

target_include_directories(EnginePatchCore PUBLIC include)

if(WIN32)
    target_link_libraries(EnginePatchCore PRIVATE)
endif()
