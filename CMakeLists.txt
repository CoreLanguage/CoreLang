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

# Link LLVM statically into the compiler by default. The resulting binary
# only needs base-system libraries at runtime (zlib/zstd/ncurses/m), so it
# installs and runs on any Linux without LLVM installed. Build with
# -DCORE_LLVM_STATIC=OFF to link the shared libLLVM instead (smaller binary,
# but requires llvm-18 runtime libraries on every machine that runs core).
option(CORE_LLVM_STATIC "Link LLVM statically (portable binaries)" ON)

if(NOT CORE_LLVM_STATIC)
  find_library(CORE_LLVM_DYLIB NAMES LLVM-18 LLVM-18.1 LLVM HINTS ${LLVM_LIBRARY_DIRS})
  if(NOT CORE_LLVM_DYLIB)
    message(FATAL_ERROR "shared LLVM requested (-DCORE_LLVM_STATIC=OFF) but libLLVM not found")
  endif()
  message(STATUS "Linking against shared LLVM: ${CORE_LLVM_DYLIB}")
  set(CORE_LLVM_LINK_LIB ${CORE_LLVM_DYLIB})
else()
  message(STATUS "Linking against static LLVM components")
  execute_process(COMMAND llvm-config-18 --link-static --libfiles
                  OUTPUT_VARIABLE LLVM_STATIC_FILES OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  separate_arguments(LLVM_STATIC_FILES UNIX_COMMAND "${LLVM_STATIC_FILES}")
  # Polly is optional and unused by Core; skip it when not installed
  list(FILTER LLVM_STATIC_FILES EXCLUDE REGEX "Polly")
  set(CORE_LLVM_LINK_LIB ${LLVM_STATIC_FILES})
endif()

# ---------------------------------------------------------------------------
# Core compiler sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CORE_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(core ${CORE_SRCS})

target_include_directories(core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})
target_compile_definitions(core PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(core PRIVATE -fno-rtti -Wall -Wextra -g)
if(CORE_LLVM_STATIC)
  # the static archives reference this symbol through a weak check in
  # abi-breaking.h; defining it keeps the ABI verify pointer consistent
  target_compile_definitions(core PRIVATE LLVM_DISABLE_ABI_BREAKING_CHECKS)
  target_link_options(core PRIVATE -static-libstdc++ -static-libgcc)
  execute_process(COMMAND llvm-config-18 --link-static --system-libs
                  OUTPUT_VARIABLE LLVM_SYSLIBS OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  separate_arguments(LLVM_SYSLIBS UNIX_COMMAND "${LLVM_SYSLIBS}")
  target_link_libraries(core PRIVATE
    -Wl,--start-group ${CORE_LLVM_LINK_LIB} -Wl,--end-group pthread ${LLVM_SYSLIBS})
else()
  target_link_libraries(core PRIVATE ${CORE_LLVM_LINK_LIB} pthread)
endif()

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

foreach(STD_SRC ${CORE_STD_SRCS})
  get_filename_component(STD_NAME ${STD_SRC} NAME)
  add_custom_command(TARGET core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STD_SRC} $<TARGET_FILE_DIR:core>/std/${STD_NAME}
    COMMENT "Installing std/${STD_NAME}")
endforeach()

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
