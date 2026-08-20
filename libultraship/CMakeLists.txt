cmake_minimum_required(VERSION 3.24.0)

option(NON_PORTABLE "Build a non-portable version" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    option(SIGN_LIBRARY "Enable xcode signing" OFF)
    option(BUNDLE_ID "Bundle ID for xcode signing" "com.example.libultraship")
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
endif()

project(libultraship LANGUAGES C CXX)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(cmake/Utils.cmake)
set(ADDITIONAL_LIB_INCLUDES "")

# ========= Configuration Options =========
option(INCLUDE_MPQ_SUPPORT "Enable StormLib and MPQ archive support" OFF)
option(GBI_UCODE "Specify the GBI ucode version" F3DEX_GBI_2)

# =========== Dependencies =============
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/dependencies/windows-vcpkg.cmake)
endif()

include(cmake/dependencies/common.cmake)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/dependencies/windows.cmake)
endif()

# =========== Sources =============
add_subdirectory("src")

# =========== Tests =============
option(LUS_BUILD_TESTS "Build unit tests" OFF)
if(LUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory("tests")
endif()
