cmake_minimum_required(VERSION 3.12)
project(torch)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# Set build options
option(USE_STANDALONE "Build as a standalone executable" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

# SmashBrotatoes is an SSB64-only Windows fork of Torch. Support for the other
# games (SM64/MK64/SF64/PM64/F-Zero X/Mario Artist/NAudio) and the Emscripten/web
# target has been removed; SSB64 support is enabled by the parent build via
# -DBUILD_SSB64=ON.

################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
    message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

    if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64"
        OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"))
        message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
    endif()
endif()

if(USE_STANDALONE)
    # Link libgfxd
    # Because libgfxd is not a CMake project, we have to manually fetch it and add it to the build
    FetchContent_Declare(
        libgfxd
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libgfxd
    )

    FetchContent_GetProperties(libgfxd)

    if(NOT libgfxd_POPULATED)
        FetchContent_MakeAvailable(libgfxd)
        include_directories(${libgfxd_SOURCE_DIR})
        set(LGFXD_SRC gfxd.c uc_f3d.c uc_f3db.c uc_f3dex.c uc_f3dexb.c uc_f3dex2.c)
        foreach (LGFXD_FILE ${LGFXD_SRC})
            list(APPEND LGFXD_FILES "${libgfxd_SOURCE_DIR}/${LGFXD_FILE}")
        endforeach()
    endif()
endif()
# Source files

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE CXX_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/lib/strhash64/*.cpp)
file(GLOB C_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lib/libmio0/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lib/libyay0/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lib/libvpk0/*.c)
set(SRC_DIR ${CXX_FILES} ${C_FILES} ${LGFXD_FILES})

if(BUILD_SSB64)
    add_definitions(-DSSB64_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/ssb64/*")
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

# Build
if (USE_STANDALONE)
    add_definitions(-DSTANDALONE)
    add_executable(${PROJECT_NAME} ${SRC_DIR})
	set(LINK_TYPE "MD")
else()
    add_library(${PROJECT_NAME} STATIC ${SRC_DIR})
	set(LINK_TYPE "MT")
endif()

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    set(CMAKE_CXX_FLAGS "-Wno-narrowing")
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
            /w;
            /Od;
            /ZI;
            /${LINK_TYPE}d
        >
        $<$<CONFIG:Release>:
            /O3;
            /Gy;
            /W3;
            /${LINK_TYPE}
        >
        /permissive-;
        /MP;
        ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
        ${DEFAULT_CXX_EXCEPTION_HANDLING}
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
            /INCREMENTAL
        >
        $<$<CONFIG:Release>:
            /OPT:REF;
            /OPT:ICF;
            /INCREMENTAL:NO;
            /FORCE:MULTIPLE
        >
        /MANIFEST:NO;
        /DEBUG;
        /SUBSYSTEM:CONSOLE
    )
    if (NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
        target_link_options(${PROJECT_NAME} PRIVATE /bigobj)
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()


# Fetch Dependencies

if(NOT USE_STANDALONE AND EXISTS "/mnt/c/WINDOWS/system32/wsl.exe")
    FetchContent_Declare(
        GSL
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/GSL
    )
    FetchContent_MakeAvailable(GSL)
    target_link_libraries(${PROJECT_NAME} PRIVATE GSL)
endif()

# Link BinaryTools

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/binarytools)
add_dependencies(${PROJECT_NAME} BinaryTools)

# Link n64graphics

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/n64graphics)
add_dependencies(${PROJECT_NAME} N64Graphics)

# Link YamlCpp

set(YAML_CPP_BUILD_TOOLS OFF)
set(YAML_CPP_BUILD_TESTS OFF)
set(YAML_CPP_DISABLE_UNINSTALL ON)
FetchContent_Declare(
    yaml-cpp
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp
)
set(YAML_CPP_BUILD_TESTS OFF)
FetchContent_MakeAvailable(yaml-cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE yaml-cpp)
if(NOT USE_STANDALONE)
	target_compile_definitions(yaml-cpp PRIVATE YAML_CPP_STATIC_DEFINE)
endif()

if(USE_STANDALONE)
    if(EMSCRIPTEN)
        set(CMAKE_CXX_FLAGS_DEBUG "-O3")
        set(SPDLOG_USE_STD_FORMAT ON)
    endif()

	FetchContent_Declare(
		spdlog
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog
	)

	FetchContent_MakeAvailable(spdlog)
	target_link_libraries(${PROJECT_NAME} PRIVATE spdlog)
else()
	find_package(spdlog QUIET)
    if(NOT spdlog_FOUND)
        FetchContent_Declare(
            spdlog
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog
        )

        FetchContent_MakeAvailable(spdlog)
    endif()
	target_link_libraries(${PROJECT_NAME} PUBLIC spdlog::spdlog)
endif()

# Link TinyXML2
set(tinyxml2_BUILD_TESTING OFF)
FetchContent_Declare(
    tinyxml2
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyxml2
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(tinyxml2)

target_link_libraries(${PROJECT_NAME} PRIVATE tinyxml2 yaml-cpp N64Graphics BinaryTools)

if(NOT USE_STANDALONE)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_include_directories(${PROJECT_NAME} PUBLIC ${yaml-cpp_SOURCE_DIR}/include)
endif()
