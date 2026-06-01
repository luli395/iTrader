cmake_minimum_required(VERSION 3.20)
project(iTrader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_VS_USE_POWERSHELL_CUSTOM_COMMANDS OFF)
set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Disable vcpkg app-local dependency copying." FORCE)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    list(APPEND CMAKE_VS_GLOBALS "VcpkgApplocalDeps=false")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "iTrader currently targets Windows because it loads strategy DLLs and links the Windows CTP SDK.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_options(/W4 /EHsc /permissive- /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(ITRADER_ENABLE_CTP "Build the live CTP trader adapter" OFF)
option(ITRADER_ENABLE_CTP_MD "Build the live CTP market-data adapter and recorder" OFF)

set(CTP_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk/traderapi" CACHE PATH "Path to the 64-bit CTP trader SDK")
set(CTP_LIB "${CTP_SDK_DIR}/thosttraderapi_se.lib")
set(CTP_DLL "${CTP_SDK_DIR}/thosttraderapi_se.dll")

set(CTP_MD_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk/mduserapi" CACHE PATH "Path to the 64-bit CTP market-data SDK")
set(CTP_MD_LIB "${CTP_MD_SDK_DIR}/thostmduserapi_se.lib")
set(CTP_MD_DLL "${CTP_MD_SDK_DIR}/thostmduserapi_se.dll")

if(ITRADER_ENABLE_CTP AND (NOT EXISTS "${CTP_LIB}" OR NOT EXISTS "${CTP_DLL}"))
    message(WARNING "CTP trader SDK not found in ${CTP_SDK_DIR}; live trader support will be disabled.")
    set(ITRADER_ENABLE_CTP OFF CACHE BOOL "" FORCE)
endif()

if(ITRADER_ENABLE_CTP_MD AND (NOT EXISTS "${CTP_MD_LIB}" OR NOT EXISTS "${CTP_MD_DLL}"))
    message(WARNING "CTP market-data SDK not found in ${CTP_MD_SDK_DIR}; MD support and recorder will be disabled.")
    set(ITRADER_ENABLE_CTP_MD OFF CACHE BOOL "" FORCE)
endif()

if(ITRADER_ENABLE_CTP AND EXISTS "${CTP_DLL}")
    foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/${config}")
        file(COPY "${CTP_DLL}" DESTINATION "${CMAKE_BINARY_DIR}/${config}")
    endforeach()
endif()

if(ITRADER_ENABLE_CTP_MD AND EXISTS "${CTP_MD_DLL}")
    foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/${config}")
        file(COPY "${CTP_MD_DLL}" DESTINATION "${CMAKE_BINARY_DIR}/${config}")
    endforeach()
endif()

add_library(itrader_core STATIC
    src/platform.cpp
    include/itrader/domain.hpp
    include/itrader/ini.hpp
    include/itrader/platform.hpp
    include/itrader/runtime_paths.hpp
    include/itrader/runtime_snapshot.hpp
    include/itrader/order_slot_state.hpp
    include/itrader/strategy_api.hpp
)

target_include_directories(itrader_core PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(itrader_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

if(ITRADER_ENABLE_CTP)
    target_sources(itrader_core PRIVATE
        src/ctp_adapter.cpp
        include/itrader/ctp_adapter.hpp
    )
    target_include_directories(itrader_core PRIVATE "${CTP_SDK_DIR}")
    target_link_libraries(itrader_core PRIVATE "${CTP_LIB}")
    target_compile_definitions(itrader_core PUBLIC ITRADER_ENABLE_CTP=1)

    if(ITRADER_ENABLE_CTP_MD)
        target_include_directories(itrader_core PRIVATE "${CTP_MD_SDK_DIR}")
        target_link_libraries(itrader_core PRIVATE "${CTP_MD_LIB}")
        target_compile_definitions(itrader_core PUBLIC ITRADER_ENABLE_CTP_MD=1)
    endif()
endif()

add_executable(itrader src/main.cpp)
target_link_libraries(itrader PRIVATE itrader_core)

add_executable(itrader_ui_api src/ui_api_server.cpp)
target_include_directories(itrader_ui_api PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(itrader_ui_api PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(itrader_ui_api PRIVATE itrader_core ws2_32 Comdlg32 Ole32 Shell32)

if(ITRADER_ENABLE_CTP_MD)
    add_executable(itrader_ctp_md_recorder src/ctp_md_recorder.cpp)
    target_include_directories(itrader_ctp_md_recorder PRIVATE "${CMAKE_SOURCE_DIR}/include" "${CTP_MD_SDK_DIR}")
    target_compile_definitions(itrader_ctp_md_recorder PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(itrader_ctp_md_recorder PRIVATE "${CTP_MD_LIB}")
endif()

function(add_itrader_strategy target_name source_file)
    add_library(${target_name} SHARED "${source_file}")
    target_include_directories(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(${target_name} PRIVATE ITRADER_BUILD_STRATEGY NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "${target_name}")

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel"
    )

    add_custom_command(
        TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/strategies/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${target_name}>"
            "${CMAKE_SOURCE_DIR}/strategies/bin/$<TARGET_FILE_NAME:${target_name}>"
        COMMENT "Copying ${target_name} to strategies/bin catalog"
    )
endfunction()

add_itrader_strategy(sample_strategy strategies/sample_strategy.cpp)
add_itrader_strategy(timed_roundtrip_strategy strategies/timed_roundtrip_strategy.cpp)
add_itrader_strategy(noop_strategy strategies/noop_strategy.cpp)
