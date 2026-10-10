# Pipkite native targets (CMake). The Visual Studio solution (Pipkite.sln) is the primary build and
# also contains the C++/CLI Windows Forms UI, which CMake does not build. Core, storage and protocol build
# and test without .NET or a MetaTrader terminal installed.
#
#   cmake -S . -B out/cmake -G "Visual Studio 18 2026" -A x64
#   cmake --build out/cmake --config Release
#   ctest --test-dir out/cmake -C Release
cmake_minimum_required(VERSION 3.25)
project(Pipkite VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Pipkite targets Windows x64 only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 UNICODE _UNICODE)
set(SR_WARNINGS /W4 /permissive- /utf-8 /Zc:__cplusplus /wd4996)
set(SR_INCLUDES ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)
set(SR_SYSLIBS ws2_32 winhttp bcrypt crypt32 advapi32 shell32 ole32 user32)

function(sr_target name)
  target_include_directories(${name} PRIVATE ${SR_INCLUDES})
  target_compile_options(${name} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SR_WARNINGS}>)
endfunction()

file(GLOB CORE_SRC Pipkite.Core/*.cpp)
add_library(Pipkite.Core STATIC ${CORE_SRC})
sr_target(Pipkite.Core)

file(GLOB STORAGE_SRC Pipkite.Storage/*.cpp)
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_compile_definitions(sqlite3 PRIVATE SQLITE_DQS=0 SQLITE_THREADSAFE=1 SQLITE_DEFAULT_WAL_SYNCHRONOUS=2 SQLITE_ENABLE_FTS5 SQLITE_OMIT_DEPRECATED SQLITE_DEFAULT_FOREIGN_KEYS=1)
target_compile_options(sqlite3 PRIVATE /w)
add_library(Pipkite.Storage STATIC ${STORAGE_SRC})
sr_target(Pipkite.Storage)
target_link_libraries(Pipkite.Storage PUBLIC sqlite3 Pipkite.Core)

file(GLOB PROTOCOL_SRC Pipkite.Protocol/*.cpp)
add_library(Pipkite.Protocol STATIC ${PROTOCOL_SRC})
sr_target(Pipkite.Protocol)
target_link_libraries(Pipkite.Protocol PUBLIC ${SR_SYSLIBS})
target_link_libraries(Pipkite.Storage PUBLIC Pipkite.Protocol)

file(GLOB ENGINE_SRC Pipkite.Engine/*.cpp)
add_executable(Pipkite.Engine ${ENGINE_SRC})
sr_target(Pipkite.Engine)
target_link_libraries(Pipkite.Engine PRIVATE Pipkite.Storage Pipkite.Protocol Pipkite.Core)

add_library(MT5BridgeHost SHARED Pipkite.MT5BridgeHost/BridgeHost.cpp Pipkite.MT5BridgeHost/MT5BridgeHost.def)
sr_target(MT5BridgeHost)
target_link_libraries(MT5BridgeHost PRIVATE Pipkite.Protocol)

add_library(bridge_impl SHARED Pipkite.MT5BridgeImpl/BridgeImpl.cpp)
sr_target(bridge_impl)
set_target_properties(bridge_impl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/bridge/impl/1.0.0)
add_custom_command(TARGET bridge_impl POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/Pipkite.MT5BridgeImpl/manifest.json $<TARGET_FILE_DIR:bridge_impl>/manifest.json)

add_library(gold_parser_native SHARED Pipkite.Plugin.GoldParser/Plugin.cpp)
sr_target(gold_parser_native)
target_link_libraries(gold_parser_native PRIVATE Pipkite.Core)
set_target_properties(gold_parser_native PROPERTIES OUTPUT_NAME module RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/plugins/gold_parser_native/1.0.0)
add_custom_command(TARGET gold_parser_native POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/Pipkite.Plugin.GoldParser/manifest.json $<TARGET_FILE_DIR:gold_parser_native>/manifest.json)

add_executable(Pipkite.Launcher WIN32 Pipkite.Launcher/Launcher.cpp)
sr_target(Pipkite.Launcher)
target_link_libraries(Pipkite.Launcher PRIVATE Pipkite.Protocol)

add_executable(Pipkite.SignTool Pipkite.SignTool/SignTool.cpp)
sr_target(Pipkite.SignTool)
target_link_libraries(Pipkite.SignTool PRIVATE Pipkite.Protocol)

add_executable(Pipkite.FakeTerminal Pipkite.FakeTerminal/FakeTerminal.cpp)
sr_target(Pipkite.FakeTerminal)

file(GLOB TEST_SRC Pipkite.Tests/*.cpp Pipkite.Engine/TerminalLauncher.cpp)
add_executable(Pipkite.Tests ${TEST_SRC})
sr_target(Pipkite.Tests)
target_link_libraries(Pipkite.Tests PRIVATE Pipkite.Storage Pipkite.Protocol Pipkite.Core)

add_executable(Pipkite.Benchmarks Pipkite.Benchmarks/Bench.cpp)
sr_target(Pipkite.Benchmarks)
target_link_libraries(Pipkite.Benchmarks PRIVATE Pipkite.Storage Pipkite.Protocol Pipkite.Core)

enable_testing()
add_test(NAME unit COMMAND Pipkite.Tests)
