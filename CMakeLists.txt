cmake_minimum_required(VERSION 3.16)

cmake_policy(SET CMP0104 NEW)
cmake_policy(SET CMP0146 OLD)
cmake_policy(SET CMP0167 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(my_mysql LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Toolchain / compiler selection
# Add an option to force clang; allow turning it off for e.g. GCC coverage builds
# -----------------------------------------------------------------------------
option(FORCE_CLANG "Force use of clang/clang++ compilers (was previously default)" ON)
if(FORCE_CLANG AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "FORCE_CLANG=ON: switching compiler to clang/clang++")
    set(CMAKE_C_COMPILER "clang")
    set(CMAKE_CXX_COMPILER "clang++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
else()
    if(NOT FORCE_CLANG)
        message(STATUS "FORCE_CLANG=OFF: respecting pre-configured compiler (${CMAKE_CXX_COMPILER_ID})")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(THREADS_PTHREADS_WEAK_SEARCH FALSE)
    set(CMAKE_THREAD_LIBS_INIT "-pthread")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-pthread>")
    add_link_options("-pthread")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftime-trace")
    add_compile_options(-ftime-trace)
    message(STATUS "********************Clang time tracing enabled******************************")
endif()

find_package(Threads REQUIRED)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(OpenMP_CXX_FLAGS "-fopenmp")
    set(OpenMP_CXX_LIB_NAMES "omp")
    set(OpenMP_omp_LIBRARY omp)
endif()

find_package(OpenMP REQUIRED)


if(MSVC)
    # Use dynamic runtime libraries
    # set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
    # Uncomment the following lines to use static runtime libraries instead
    # set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif()

include(cmake/CPM.cmake)

set(CMAKE_PCH_ENABLE OFF)

find_package(Git REQUIRED)

execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

message(STATUS "*******GIT_DESCRIBE: ${GIT_DESCRIBE}")
message(STATUS "*******CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")


if(ENABLE_ASAN)
    if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU"))
        message(FATAL_ERROR "ENABLE_ASAN requires Clang or GCC toolchains")
    endif()

    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>"
        "$<$<COMPILE_LANGUAGE:C>:-fsanitize=address>"
        "$<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer>"
    )

    add_link_options(-fsanitize=address -fno-omit-frame-pointer)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(
            "$<$<COMPILE_LANGUAGE:CXX>:-fdiagnostics-color=always>"
            "$<$<COMPILE_LANGUAGE:C>:-fdiagnostics-color=always>"
        )
    endif()

    message(STATUS "AddressSanitizer enabled (ENABLE_ASAN=ON)")
else()
    message(STATUS "AddressSanitizer disabled (ENABLE_ASAN=OFF)")
endif()

# CMAKE_BUILD_TYPE STREQUAL "Debug" and no 'NO_ASAN' environment variable
if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND ENABLE_ASAN)
# Add AddressSanitizer flags
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -v")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fdiagnostics-color=always -v")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fdiagnostics-color=always")
    endif()
else()
    message(STATUS "******AddressSanitizer is disabled******")
endif()


if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG_BUILD)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_CXX_FLAGS_DEBUG "-O1 -gline-tables-only")
        set(CMAKE_C_FLAGS_DEBUG "-O1 -gline-tables-only")
    else()
        # GCC or other: use standard debug info suitable for coverage
        set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
        set(CMAKE_C_FLAGS_DEBUG "-O0 -g")
    endif()
    option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
    
    # Additional debug flags for Clang
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fno-omit-frame-pointer -fno-optimize-sibling-calls)
    endif()
    
elseif(CMAKE_BUILD_TYPE STREQUAL "Test")
    add_definitions(-DTEST_ENV)
    set(CMAKE_CXX_FLAGS_TEST "-O2 -g")
    set(CMAKE_C_FLAGS_TEST "-O2 -g")
    
else()
    add_definitions(-DRELEASE_BUILD)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto=thin -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -flto=thin -DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-fuse-ld=lld")
endif()

# -----------------------------------------------------------------------------
# Coverage instrumentation (GCC / gcov). Enable with -DENABLE_COVERAGE=ON
# When using coverage, recommended to disable FORCE_CLANG so GCC is used.
# -----------------------------------------------------------------------------
option(ENABLE_COVERAGE "Enable code coverage instrumentation (GCC)" OFF)
if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        message(WARNING "ENABLE_COVERAGE is ON but compiler is ${CMAKE_CXX_COMPILER_ID}. For gcov style coverage, configure with -DFORCE_CLANG=OFF so GCC is used.")
    endif()
    message(STATUS "Coverage instrumentation enabled (--coverage)")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

message(STATUS "Current debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "-------------------------env CORES value is: $ENV{CORES}-------------------------")
message(STATUS "-------------------------CMAKE_BUILD_PARALLEL_LEVEL: $ENV{CMAKE_BUILD_PARALLEL_LEVEL}-------------------------")


if(FALSE)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Find the IWYU tool
    find_program(IWYU_PATH NAMES include-what-you-use iwyu)

    if(IWYU_PATH)
        message(STATUS "Found Include What You Use (IWYU): ${IWYU_PATH}")
        set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE 
        "${CMAKE_SOURCE_DIR}/iwyu-wrapper.sh;-Xiwyu;--mapping_file=${CMAKE_SOURCE_DIR}/iwyu_mappings.imp;")
        # --mapping_file=${CMAKE_SOURCE_DIR}/iwyu/boost-all.imp
    else()
        message(WARNING "Include What You Use (IWYU) not found. Skipping IWYU checks.")
    endif()
endif()
endif()


enable_testing()
add_definitions(-DEIGEN_NO_DEBUG)
# add_definitions(-DBOOST_ASIO_ENABLE_HANDLER_TRACKING)

# Custom function to insert JS file content into a C++ file
function(insert_js_into_cpp js_file cpp_file placeholder)
  # Read the JS file content
  file(READ ${js_file} JS_CONTENT)

  # Read the C++ source file
  file(READ ${cpp_file} CPP_CONTENT)

    # Delete the content between //js-content-start and //js-content-end
  string(REGEX REPLACE "//js-content-start.*//js-content-end" "${placeholder}" CPP_CONTENT "${CPP_CONTENT}")


  # Replace the placeholder with the JS content
  string(REPLACE "${placeholder}" "${JS_CONTENT}" CPP_CONTENT "${CPP_CONTENT}")

  # Write the updated content back to the C++ source file
  file(WRITE ${cpp_file} "${CPP_CONTENT}")
endfunction()

# insert_js.cmake
set(JS_FILE "${CMAKE_SOURCE_DIR}/js/login-page.js")  # Path to the JS file
set(CPP_FILE "${CMAKE_SOURCE_DIR}/bbserver/include/handler_login_page.hpp")   # Path to the C++ file
set(PLACEHOLDER "js_goes_here")         # The placeholder in the C++ file

add_definitions(-DBOOST_PROCESS_VERSION=2)

file(GLOB LIB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_compile_definitions(BUFFERSIZE=2048)
add_compile_definitions(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS=1)

find_package(OpenSSL REQUIRED)
find_package(ryml CONFIG REQUIRED)
add_subdirectory(tests)
add_subdirectory(bm)

# -----------------------------------------------------------------------------
# Coverage reporting target (requires gcovr if ENABLE_COVERAGE was used)
# Usage:
#   cmake -S . -B build-coverage -DENABLE_COVERAGE=ON -DFORCE_CLANG=OFF ...
#   cmake --build build-coverage -j
#   cmake --build build-coverage --target coverage
# Outputs HTML + XML into build-coverage/coverage/
# -----------------------------------------------------------------------------
find_program(GCOVR_EXECUTABLE gcovr)
if(GCOVR_EXECUTABLE)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
        COMMAND ${GCOVR_EXECUTABLE}
            -r ${CMAKE_SOURCE_DIR}
            --exclude '.*vcpkg_installed/.*'
            --exclude '.*build-.*'
            --exclude '.*/tests/.*'
            --filter '${CMAKE_SOURCE_DIR}/include'
            --html --html-details -o ${CMAKE_BINARY_DIR}/coverage/index.html
            --xml -o ${CMAKE_BINARY_DIR}/coverage/coverage.xml
            --txt -o ${CMAKE_BINARY_DIR}/coverage/summary.txt
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating gcovr coverage report (HTML/XML/TXT)"
        VERBATIM)

    # -----------------------------------------------------------------
    # coverage_full: invokes scripts/run_coverage.sh for end-to-end
    # configuration (if needed), build, test, and report generation.
    # This provides a single entry point for CI or local use:
    #   cmake --build build-coverage --target coverage_full -- -j
    # Note: Script itself reconfigures only if missing or --reconfigure passed.
    # -----------------------------------------------------------------
    add_custom_target(coverage_full
        COMMAND ${CMAKE_COMMAND} -E echo "[coverage_full] Running scripts/run_coverage.sh"
        COMMAND ${CMAKE_COMMAND} -E env PROJECT_ROOT=${CMAKE_SOURCE_DIR} bash ${CMAKE_SOURCE_DIR}/scripts/run_coverage.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Run full coverage pipeline (configure/build/test/report)"
        VERBATIM)
else()
    message(STATUS "gcovr not found: 'coverage' target will be unavailable")
endif()
