cmake_minimum_required(VERSION 3.16)
project(PlantBrainGrid VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_PYTHON_BINDINGS "Build Python bindings" ON)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Threading
find_package(Threads REQUIRED)

# Core library
file(GLOB_RECURSE CORE_SOURCES "src/core/*.cpp")
add_library(plantbraingrid_core STATIC ${CORE_SOURCES})
set_target_properties(plantbraingrid_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(plantbraingrid_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(plantbraingrid_core PUBLIC Threads::Threads)

# Python bindings
if(BUILD_PYTHON_BINDINGS)
    find_package(Python3 COMPONENTS Interpreter Development.Module)

    if(Python3_Development.Module_FOUND)
        # Try to find pybind11
        find_package(pybind11 QUIET)
        if(NOT pybind11_FOUND)
            include(FetchContent)
            FetchContent_Declare(
                pybind11
                GIT_REPOSITORY https://github.com/pybind/pybind11.git
                GIT_TAG v2.11.1
            )
            FetchContent_MakeAvailable(pybind11)
        endif()

        pybind11_add_module(_plantbraingrid src/bindings/module.cpp)
        target_link_libraries(_plantbraingrid PRIVATE plantbraingrid_core)
        # Add parent of Python include dir for multi-arch pyconfig.h support
        if(Python3_INCLUDE_DIRS)
            foreach(_inc_dir ${Python3_INCLUDE_DIRS})
                get_filename_component(_inc_parent "${_inc_dir}" DIRECTORY)
                target_include_directories(_plantbraingrid PRIVATE "${_inc_parent}")
            endforeach()
        endif()
        message(STATUS "Python bindings: ENABLED")
    else()
        message(STATUS "Python bindings: DISABLED (Python development files not found)")
    endif()
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()

    # Fetch Catch2
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)

    # Test executable
    file(GLOB_RECURSE TEST_SOURCES "tests/cpp/*.cpp")
    add_executable(plantbraingrid_tests ${TEST_SOURCES})
    target_link_libraries(plantbraingrid_tests PRIVATE
        plantbraingrid_core
        Catch2::Catch2WithMain
    )

    target_compile_definitions(plantbraingrid_tests PRIVATE
        EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(plantbraingrid_tests)
endif()
