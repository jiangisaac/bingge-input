cmake_minimum_required(VERSION 3.20)
project(TV1TSF LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "TV1TSF only supports Windows")
endif()

add_library(TV1TSF SHARED
    src/CandidateWindow.cpp
    src/Com.cpp
    src/Dictionary.cpp
    src/EditSession.cpp
    src/TextService.cpp
    src/TV1TSF.h
    src/TV1TSF.def
    src/resource.h
    src/resources.rc
)

target_compile_features(TV1TSF PRIVATE cxx_std_17)
target_compile_definitions(TV1TSF PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)

if(MSVC)
    target_compile_options(TV1TSF PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
else()
    message(FATAL_ERROR "TV1TSF requires MSVC and the Windows SDK")
endif()

target_link_libraries(TV1TSF PRIVATE
    advapi32
    gdi32
    ole32
    oleaut32
    user32
    uuid
)

set_target_properties(TV1TSF PROPERTIES
    PREFIX ""
    OUTPUT_NAME "TV1TSF"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

add_executable(DictionaryTests
    src/Dictionary.cpp
    src/TV1TSF.h
    src/resource.h
    src/resources.rc
    tests/DictionaryTests.cpp
)
target_include_directories(DictionaryTests PRIVATE src)
target_compile_features(DictionaryTests PRIVATE cxx_std_17)
target_compile_definitions(DictionaryTests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)
target_compile_options(DictionaryTests PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
set_target_properties(DictionaryTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

enable_testing()
add_test(NAME DictionaryTests COMMAND DictionaryTests)
