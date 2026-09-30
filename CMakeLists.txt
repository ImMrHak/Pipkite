# SignalRelay native targets (CMake). The Visual Studio solution (SignalRelay.sln) is the primary build and
# also contains the C++/CLI Windows Forms UI, which CMake does not build. Core, storage and protocol build
# and test without .NET or a MetaTrader terminal installed.
#
#   cmake -S . -B out/cmake -G "Visual Studio 18 2026" -A x64
#   cmake --build out/cmake --config Release
#   ctest --test-dir out/cmake -C Release
cmake_minimum_required(VERSION 3.25)
project(SignalRelay VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "SignalRelay targets Windows x64 only")
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

file(GLOB CORE_SRC SignalRelay.Core/*.cpp)
add_library(SignalRelay.Core STATIC ${CORE_SRC})
sr_target(SignalRelay.Core)

file(GLOB STORAGE_SRC SignalRelay.Storage/*.cpp)
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_compile_definitions(sqlite3 PRIVATE SQLITE_DQS=0 SQLITE_THREADSAFE=1 SQLITE_DEFAULT_WAL_SYNCHRONOUS=2 SQLITE_ENABLE_FTS5 SQLITE_OMIT_DEPRECATED SQLITE_DEFAULT_FOREIGN_KEYS=1)
target_compile_options(sqlite3 PRIVATE /w)
add_library(SignalRelay.Storage STATIC ${STORAGE_SRC})
sr_target(SignalRelay.Storage)
target_link_libraries(SignalRelay.Storage PUBLIC sqlite3 SignalRelay.Core)

file(GLOB PROTOCOL_SRC SignalRelay.Protocol/*.cpp)
add_library(SignalRelay.Protocol STATIC ${PROTOCOL_SRC})
sr_target(SignalRelay.Protocol)
target_link_libraries(SignalRelay.Protocol PUBLIC ${SR_SYSLIBS})
target_link_libraries(SignalRelay.Storage PUBLIC SignalRelay.Protocol)

file(GLOB ENGINE_SRC SignalRelay.Engine/*.cpp)
add_executable(SignalRelay.Engine ${ENGINE_SRC})
sr_target(SignalRelay.Engine)
target_link_libraries(SignalRelay.Engine PRIVATE SignalRelay.Storage SignalRelay.Protocol SignalRelay.Core)

add_library(MT5BridgeHost SHARED SignalRelay.MT5BridgeHost/BridgeHost.cpp SignalRelay.MT5BridgeHost/MT5BridgeHost.def)
sr_target(MT5BridgeHost)
target_link_libraries(MT5BridgeHost PRIVATE SignalRelay.Protocol)

add_library(bridge_impl SHARED SignalRelay.MT5BridgeImpl/BridgeImpl.cpp)
sr_target(bridge_impl)
set_target_properties(bridge_impl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/bridge/impl/1.0.0)
add_custom_command(TARGET bridge_impl POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/SignalRelay.MT5BridgeImpl/manifest.json $<TARGET_FILE_DIR:bridge_impl>/manifest.json)

add_library(gold_parser_native SHARED SignalRelay.Plugin.GoldParser/Plugin.cpp)
sr_target(gold_parser_native)
target_link_libraries(gold_parser_native PRIVATE SignalRelay.Core)
set_target_properties(gold_parser_native PROPERTIES OUTPUT_NAME module RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/plugins/gold_parser_native/1.0.0)
add_custom_command(TARGET gold_parser_native POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/SignalRelay.Plugin.GoldParser/manifest.json $<TARGET_FILE_DIR:gold_parser_native>/manifest.json)

add_executable(SignalRelay.Launcher WIN32 SignalRelay.Launcher/Launcher.cpp)
sr_target(SignalRelay.Launcher)
target_link_libraries(SignalRelay.Launcher PRIVATE SignalRelay.Protocol)

add_executable(SignalRelay.SignTool SignalRelay.SignTool/SignTool.cpp)
sr_target(SignalRelay.SignTool)
target_link_libraries(SignalRelay.SignTool PRIVATE SignalRelay.Protocol)

add_executable(SignalRelay.FakeTerminal SignalRelay.FakeTerminal/FakeTerminal.cpp)
sr_target(SignalRelay.FakeTerminal)

file(GLOB TEST_SRC SignalRelay.Tests/*.cpp SignalRelay.Engine/TerminalLauncher.cpp)
add_executable(SignalRelay.Tests ${TEST_SRC})
sr_target(SignalRelay.Tests)
target_link_libraries(SignalRelay.Tests PRIVATE SignalRelay.Storage SignalRelay.Protocol SignalRelay.Core)

add_executable(SignalRelay.Benchmarks SignalRelay.Benchmarks/Bench.cpp)
sr_target(SignalRelay.Benchmarks)
target_link_libraries(SignalRelay.Benchmarks PRIVATE SignalRelay.Storage SignalRelay.Protocol SignalRelay.Core)

enable_testing()
add_test(NAME unit COMMAND SignalRelay.Tests)
