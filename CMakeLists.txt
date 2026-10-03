cmake_minimum_required(VERSION 3.20)
project(Core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# LLVM dependency (tested with LLVM 18.x; requires llvm-dev + matching libLLVM)
# ---------------------------------------------------------------------------
find_package(LLVM 18.1 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_CMAKE_DIR}")

find_library(CORE_LLVM_DYLIB NAMES LLVM-18 LLVM-18.1 LLVM HINTS ${LLVM_LIBRARY_DIRS})
if(CORE_LLVM_DYLIB)
  message(STATUS "Linking against shared LLVM: ${CORE_LLVM_DYLIB}")
  set(CORE_LLVM_LINK_LIB ${CORE_LLVM_DYLIB})
else()
  message(STATUS "Linking against static LLVM components")
  llvm_map_components_to_libnames(CORE_LLVM_LINK_LIB
    core support analysis codegen target mc irprinter bitwriter targetparser passes)
endif()

# ---------------------------------------------------------------------------
# Core compiler sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CORE_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(core ${CORE_SRCS})

target_include_directories(core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})
target_compile_definitions(core PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(core PRIVATE -fno-rtti -Wall -Wextra)
target_link_libraries(core PRIVATE ${CORE_LLVM_LINK_LIB} pthread)

# ---------------------------------------------------------------------------
# Runtime support library (compiled C runtime: printing, memory, threads...)
# and the Core standard library sources. Both are installed next to the
# `core` binary; the driver locates them relative to argv[0].
# ---------------------------------------------------------------------------
set(CORE_RUNTIME_OBJ ${CMAKE_CURRENT_BINARY_DIR}/corert.o)
add_custom_command(OUTPUT ${CORE_RUNTIME_OBJ}
  COMMAND ${CMAKE_C_COMPILER} -O2 -fPIC -c ${CMAKE_CURRENT_SOURCE_DIR}/runtime/corert.c -o ${CORE_RUNTIME_OBJ}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/runtime/corert.c
  COMMENT "Building Core runtime (corert.o)")
add_custom_target(core_runtime ALL DEPENDS ${CORE_RUNTIME_OBJ})
add_dependencies(core core_runtime)

file(GLOB CORE_STD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/std/*.cr)

add_custom_command(TARGET core POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_RUNTIME_OBJ} $<TARGET_FILE_DIR:core>/corert.o
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:core>/std
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_STD_SRCS} $<TARGET_FILE_DIR:core>/std/
  COMMENT "Installing Core runtime + stdlib next to compiler")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME core_e2e
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py --core $<TARGET_FILE:core>)
endif()

install(TARGETS core DESTINATION bin)
install(FILES ${CORE_RUNTIME_OBJ} DESTINATION lib/core)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/std/ DESTINATION lib/core/std)
