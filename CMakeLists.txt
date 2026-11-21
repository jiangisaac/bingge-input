cmake_minimum_required(VERSION 3.20)
project(TV1TSF VERSION 14.2.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "TV1TSF only supports Windows")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(src/SettingsDialog.rc.in generated/TV1TSFSettings.rc @ONLY)
set(TV1_DICTIONARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/dictionaries")
set(TV1_PINYIN_BIN "${TV1_DICTIONARY_DIR}/pinyin.bin")
set(TV1_WUBI86_BIN "${TV1_DICTIONARY_DIR}/wubi86.bin")
set(TV1_ENGLISH_BIN "${TV1_DICTIONARY_DIR}/english.bin")
add_custom_command(
    OUTPUT "${TV1_PINYIN_BIN}" "${TV1_WUBI86_BIN}" "${TV1_ENGLISH_BIN}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_dictionaries.py"
        --emit-bin
        --input-dir "${CMAKE_CURRENT_SOURCE_DIR}/data"
        --output-dir "${TV1_DICTIONARY_DIR}"
    DEPENDS
        data/pinyin.tsv
        data/wubi86.tsv
        data/english.tsv
        tools/prepare_dictionaries.py
    COMMENT "Precompiling pinyin, Wubi and English dictionaries"
    VERBATIM
)
add_custom_target(GenerateDictionaries
    DEPENDS "${TV1_PINYIN_BIN}" "${TV1_WUBI86_BIN}" "${TV1_ENGLISH_BIN}")

add_library(TV1TSF SHARED
    src/CandidateListUIElement.cpp
    src/CandidateListUIElement.h
    src/CandidateWindow.cpp
    src/Com.cpp
    src/Dictionary.cpp
    src/EditSession.cpp
    src/SettingsDialog.cpp
    src/TextService.cpp
    src/TV1TSF.h
    src/TV1TSF.def
    src/resource.h
    src/resources.rc
    "${CMAKE_CURRENT_BINARY_DIR}/generated/TV1TSFSettings.rc"
)

add_dependencies(TV1TSF GenerateDictionaries)
# 词典 .bin 以 RCDATA 嵌入 resources.rc；显式声明依赖，词库更新后强制重编 RC，避免 DLL 内嵌旧词典。
set_source_files_properties(src/resources.rc PROPERTIES
    OBJECT_DEPENDS "${TV1_PINYIN_BIN};${TV1_WUBI86_BIN};${TV1_ENGLISH_BIN}")
target_include_directories(TV1TSF PRIVATE src "${TV1_DICTIONARY_DIR}")
target_compile_features(TV1TSF PRIVATE cxx_std_17)

option(TV1TSF_SEARCH_DIAGNOSTICS "Enable SearchHost/host key and candidate diagnostics" OFF)
if(TV1TSF_SEARCH_DIAGNOSTICS)
    target_compile_definitions(TV1TSF PRIVATE TV1TSF_SEARCH_DIAGNOSTICS=1)
endif()

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
    comdlg32
    gdi32
    ole32
    oleaut32
    user32
    uuid
    version
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
add_dependencies(DictionaryTests GenerateDictionaries)
target_include_directories(DictionaryTests PRIVATE src "${TV1_DICTIONARY_DIR}")
target_compile_features(DictionaryTests PRIVATE cxx_std_17)
target_compile_definitions(DictionaryTests PRIVATE TV1TSF_DICTIONARY_TESTING)
target_compile_definitions(DictionaryTests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)
target_compile_options(DictionaryTests PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_link_libraries(DictionaryTests PRIVATE psapi)
set_target_properties(DictionaryTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

add_executable(CandidateListUIElementTests
    src/CandidateListUIElement.cpp
    src/CandidateListUIElement.h
    tests/CandidateListUIElementTests.cpp
)
target_include_directories(CandidateListUIElementTests PRIVATE src)
target_compile_features(CandidateListUIElementTests PRIVATE cxx_std_17)
target_compile_definitions(CandidateListUIElementTests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)
target_compile_options(CandidateListUIElementTests PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_link_libraries(CandidateListUIElementTests PRIVATE oleaut32 uuid)
set_target_properties(CandidateListUIElementTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

enable_testing()
add_test(NAME DictionaryTests COMMAND DictionaryTests)
add_test(NAME CandidateListUIElementTests COMMAND CandidateListUIElementTests)
