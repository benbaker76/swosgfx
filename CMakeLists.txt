# swosgfx -- Sensible World of Soccer graphics: a C99 library (lib/) and the
# swosgfx command-line tool (cli/, C++17).
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# -DSWOSGFX_TESTS=ON also builds the tests in tests/ (they compare with the
# reference outputs tests/reference.sh makes; see tests/README.md).
cmake_minimum_required(VERSION 3.16)
project(swosgfx VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# One self-contained .exe on Windows: the C runtime linked in.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SWOSGFX_TESTS "Build the tests" OFF)

file(GLOB SWOSGFX_LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c)
add_library(swosgfx_lib STATIC ${SWOSGFX_LIB_SOURCES})
target_include_directories(swosgfx_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
if(MSVC)
  target_compile_definitions(swosgfx_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(swosgfx_lib PRIVATE /W3)
else()
  target_compile_options(swosgfx_lib PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(swosgfx_lib PUBLIC m)
endif()

file(GLOB SWOSGFX_CLI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp)
add_executable(swosgfx ${SWOSGFX_CLI_SOURCES})
target_link_libraries(swosgfx PRIVATE swosgfx_lib)
target_compile_definitions(swosgfx PRIVATE SWOSGFX_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_definitions(swosgfx PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(NOT APPLE)
  # A Linux binary that runs on any distribution of the same libc age.
  target_link_options(swosgfx PRIVATE -static-libstdc++ -static-libgcc)
endif()
install(TARGETS swosgfx RUNTIME DESTINATION bin)

# Each test runs without arguments on its own data and skips what needs the
# game's files; given a folder it also checks against those (tests/README.md).
if(SWOSGFX_TESTS)
  enable_testing()
  file(GLOB SWOSGFX_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.c)
  foreach(src ${SWOSGFX_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE swosgfx_lib)
    target_compile_definitions(${name} PRIVATE SWOSGFX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
      target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME ${name} COMMAND ${name})
  endforeach()
endif()
