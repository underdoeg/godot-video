# Using the same minimum as the godot-cpp project
cmake_minimum_required(VERSION 3.17)

include(FetchContent)
include(ExternalProject)

# Silence unused variable warning when specified from toolchain
if (CMAKE_C_COMPILER)
endif ()

set(LIBNAME "godot-video" CACHE STRING "The name of the library")
set(GODOT_PROJECT_DIR "demo" CACHE STRING "The directory of a Godot project folder")

# Make sure all the dependencies are satisfied
find_package(Python3 3.4 REQUIRED)
find_program(GIT git REQUIRED)


# FFMPEG
#set(FFMPEG_LIBRARY ${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}ffmpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
set(FFMPEG_BUILD ${CMAKE_BINARY_DIR}/ffmpeg/build)
# --enable-shared for so libs (if set, --enable-pic is not necessary)
set(FFMPEG_CONFIG --enable-shared --enable-gpl --enable-gnutls --enable-libaom --enable-libass --enable-libfdk-aac --enable-libfreetype --enable-libmp3lame --enable-libopus --enable-libsvtav1 --enable-libdav1d --enable-libvorbis --enable-libvpx --enable-libx264 --enable-libx265 --enable-nonfree)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("Enable ffmpeg debug build")
    set(FFMPEG_CONFIG ${FFMPEG_CONFIG} --enable-debug=3 --disable-optimizations --disable-stripping)
    message(${FFMPEG_CONFIG})
endif ()

ExternalProject_Add(
        ffmpeg
        GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
        GIT_TAG release/7.1
        GIT_SHALLOW true
        PREFIX ${CMAKE_BINARY_DIR}/ffmpeg
        INSTALL_DIR ${FFMPEG_BUILD}
        #        CONFIGURE_COMMAND echo <INSTALL_DIR>
        CONFIGURE_COMMAND cd <SOURCE_DIR> && ./configure --prefix=<INSTALL_DIR> ${FFMPEG_CONFIG}
        BUILD_COMMAND make -j -C <SOURCE_DIR>
        INSTALL_COMMAND make -C <SOURCE_DIR> install
)

message("FFMPEG will be installed into ${FFMPEG_BUILD}")

#add_library(ForexConnect SHARED IMPORTED)
#set_target_properties(ForexConnect PROPERTIES IMPORTED_LOCATION ${FXCM_LIB_DIR}/libForexConnect.so)

## godot-cpp
FetchContent_Declare(
        godot-cpp
        GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
        GIT_TAG 8bb931c6a9ec6d725b80ca9ba3f947fe25e84307
)
FetchContent_MakeAvailable(godot-cpp)

#add_subdirectory(${godot-cpp_SOURCE_DIR})

# Add godot-cpp's module path and include the exported functions.
# This is made available for documentation generation
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${godot-cpp_SOURCE_DIR}/cmake")
include(GodotCPPModule)

# The godot-cpp target has some of useful properties attached that can be retrieved like so.
get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)

# Now we can specify our own project which will inherit any global cmake properties or variables that have been defined.
project(godot-video
        VERSION 1.0
        DESCRIPTION "This repository serves as a quickstart template for GDExtension development with Godot 4.0+."
        HOMEPAGE_URL "https://github.com/enetheru/godot-cpp-template/tree/main"
        LANGUAGES CXX
)

add_library(${LIBNAME} SHARED)

target_sources(${LIBNAME}
        PRIVATE
        src/av_video.cpp
        src/av_video.h
        src/register_types.cpp
        src/register_types.h
)

# Fetch a list of the xml files to use for documentation and add to our target
file(GLOB_RECURSE DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/doc_classes/*.xml")

# conditionally add doc data to compile output
if (DOC_XML)
    if (GODOTCPP_TARGET MATCHES "editor|template_debug")
        target_doc_sources(${LIBNAME} ${DOC_XML})
    endif ()
endif ()

target_include_directories(${LIBNAME} PRIVATE ${FFMPEG_BUILD}/include) #${CMAKE_BINARY_DIR}/ffmpeg/src/ffmpeg
target_link_directories(${LIBNAME} PRIVATE ${FFMPEG_BUILD}/lib)
target_link_libraries(${LIBNAME} PRIVATE godot-cpp avcodec avdevice avutil)

set_target_properties(${LIBNAME}
        PROPERTIES
        # The generator expression here prevents msvc from adding a Debug or Release subdir.
        RUNTIME_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"

        PREFIX ""
        OUTPUT_NAME "${LIBNAME}${GODOTCPP_SUFFIX}"
)

set(GODOT_PROJECT_BINARY_DIR "${PROJECT_SOURCE_DIR}/${GODOT_PROJECT_DIR}/bin/${GODOTCPP_PLATFORM}")

add_custom_command(TARGET ${LIBNAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${LIBNAME}>" "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>"
)
