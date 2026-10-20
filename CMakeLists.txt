cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(RawroticPredictiveBuffer VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

include($ENV{GEODE_SDK}/cmake/CPM.cmake)
CPMAddPackage("gh:matcool/gd-imgui-cocos#b93f08ccef778a53ebba09b20c347f6a63980119")
target_link_libraries(${PROJECT_NAME} imgui-cocos)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /O2 /Ob3 /Gw /Gy /GS-)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    target_link_options(${PROJECT_NAME} PRIVATE /OPT:REF /OPT:ICF)
endif()

setup_geode_mod(${PROJECT_NAME})
