# Using the same minimum as the godot-cpp project
cmake_minimum_required(VERSION 3.5)

include(FetchContent)
include(ExternalProject)

# Silence unused variable warning when specified from toolchain
if (CMAKE_C_COMPILER)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(LIBNAME "godot-video" CACHE STRING "The name of the library")
set(GODOT_PROJECT_DIR "demo" CACHE STRING "The directory of a Godot project folder")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

message(C COMPILER SET TO ${CMAKE_C_COMPILER})
message(CXX COMPILER SET TO ${CMAKE_CXX_COMPILER})

# Make sure all the dependencies are satisfied
find_package(Python3 3.4 REQUIRED)
find_program(GIT git REQUIRED)

set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-Bsymbolic")

set(LIBS_INSTALL ${CMAKE_BINARY_DIR}/libs)
set(LIBS_INCLUDE ${LIBS_INSTALL}/include)
set(LIBS_LINK ${LIBS_INSTALL}/lib)
set(LIBS_PKG ${LIBS_LINK}/pkgconfig)
message("libraries will be installed into ${LIBS_INSTALL}")

set(MAKE_FLAGS "CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER}")

# X264
set(X264_DIR ${CMAKE_BINARY_DIR}/x264)
ExternalProject_Add(
        libx264
        GIT_REPOSITORY https://code.videolan.org/videolan/x264.git
        GIT_TAG master
        GIT_SHALLOW true
        PREFIX ${X264_DIR}
        INSTALL_DIR ${LIBS_INSTALL}
        #        CONFIGURE_COMMAND echo <INSTALL_DIR>
        CONFIGURE_COMMAND cd <SOURCE_DIR> && ./configure ${MAKE_FLAGS} --prefix=<INSTALL_DIR> --enable-static --disable-cli --enable-pic
        BUILD_COMMAND make ${MAKE_FLAGS} -j -C <SOURCE_DIR>
        INSTALL_COMMAND make ${MAKE_FLAGS} -C <SOURCE_DIR> install
)

set(X265_DIR ${CMAKE_BINARY_DIR}/x265)
ExternalProject_Add(
        libx265
        GIT_REPOSITORY https://phwhitfield@bitbucket.org/multicoreware/x265_git.git
        GIT_TAG master
        #        GIT_SHALLOW true
        PREFIX ${X265_DIR}
        INSTALL_DIR ${LIBS_INSTALL}
        #        CONFIGURE_COMMAND cd <SOURCE_DIR>/build/linux && ./make-Makefiles.bash && ./configure --prefix=<INSTALL_DIR> --enable-static --disable-cli --enable-pic
        #        BUILD_COMMAND make -j -C <SOURCE_DIR>
        #        INSTALL_COMMAND make -C <SOURCE_DIR> install
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBS_INSTALL}
        -DCMAKE_PREFIX_PATH=${XZ_DIR}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
        <SOURCE_DIR>/source # Tell CMake to use subdirectory as source.
)

set(ZLIB_DIR ${CMAKE_BINARY_DIR}/zlib)
ExternalProject_Add(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW true
        PREFIX ${ZLIB_DIR}
        INSTALL_DIR ${LIBS_INSTALL}
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBS_INSTALL}
        -DCMAKE_PREFIX_PATH=${ZLIB_DIR}
        -DBUILD_SHARED_LIBS=OFF
)

# XZ
set(XZ_DIR ${CMAKE_BINARY_DIR}/xz)
ExternalProject_Add(
        xz
        GIT_REPOSITORY https://github.com/tukaani-project/xz
        GIT_TAG v5.8
        GIT_SHALLOW true
        PREFIX ${XZ_DIR}
        INSTALL_DIR ${LIBS_INSTALL}
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBS_INSTALL}
        -DCMAKE_PREFIX_PATH=${XZ_DIR}
)

# bzip2
set(BZIP2_DIR ${CMAKE_BINARY_DIR}/bzip2)
ExternalProject_Add(
        bzip2
        GIT_REPOSITORY https://gitlab.com/bzip2/bzip2.git
        GIT_TAG master
        GIT_SHALLOW true
        PREFIX ${BZIP2_DIR}
        INSTALL_DIR ${LIBS_INSTALL}
        CMAKE_ARGS
        -DENABLE_SHARED_LIB=OFF
        -DENABLE_STATIC_LIB=ON
        -DENABLE_STATIC_LIB_IS_PIC=ON
        -DCMAKE_INSTALL_PREFIX=${LIBS_INSTALL}
        -DCMAKE_PREFIX_PATH=${BZIP2_DIR}
)

## WE need shared and static libs, static for final link, shared to have ffmpeg autodetect in configure work

# LIBVA
#set(LIBVA_DIR ${CMAKE_BINARY_DIR}/libva)
#ExternalProject_Add(
#        libva
#        GIT_REPOSITORY https://github.com/intel/libva.git
#        GIT_TAG 2.22.0
#        GIT_SHALLOW true
#        PREFIX ${LIBVA_DIR}
#        INSTALL_DIR ${LIBS_INSTALL}
#        #        CONFIGURE_COMMAND echo <INSTALL_DIR>
#        CONFIGURE_COMMAND cd <SOURCE_DIR> && ./autogen.sh --prefix=<INSTALL_DIR> --libdir=<INSTALL_DIR>/lib --enable-static --enable-pic
#        BUILD_COMMAND make -j -C <SOURCE_DIR>
#        INSTALL_COMMAND make -C <SOURCE_DIR> install
#)

# LIBXCB
#set(LIBXCB_DIR_STATIC ${CMAKE_BINARY_DIR}/libxcb-static)
#ExternalProject_Add(
#        libxcb-static
#        GIT_REPOSITORY https://gitlab.freedesktop.org/xorg/lib/libxcb.git
#        GIT_TAG libxcb-1.17.0
#        GIT_SHALLOW true
#        PREFIX ${LIBXCB_DIR}
#        INSTALL_DIR ${LIBS_INSTALL}
#        #        CONFIGURE_COMMAND echo <INSTALL_DIR>
#        CONFIGURE_COMMAND cd <SOURCE_DIR> && ./autogen.sh --prefix=<INSTALL_DIR> --libdir=<INSTALL_DIR>/lib --enable-static --enable-pic
#        BUILD_COMMAND make -j -C <SOURCE_DIR>
#        INSTALL_COMMAND make -C <SOURCE_DIR> install
#)
#
#set(LIBXCB_DIR_SHARED ${CMAKE_BINARY_DIR}/libxcb-shared)
#ExternalProject_Add(
#        libxcb-shared
#        GIT_REPOSITORY https://gitlab.freedesktop.org/xorg/lib/libxcb.git
#        GIT_TAG libxcb-1.17.0
#        GIT_SHALLOW true
#        PREFIX ${LIBXCB_DIR}
#        INSTALL_DIR ${LIBS_INSTALL}
#        #        CONFIGURE_COMMAND echo <INSTALL_DIR>
#        CONFIGURE_COMMAND cd <SOURCE_DIR> && ./autogen.sh --prefix=<INSTALL_DIR> --libdir=<INSTALL_DIR>/lib
#        BUILD_COMMAND make -j -C <SOURCE_DIR>
#        INSTALL_COMMAND make -C <SOURCE_DIR> install
#)

# LIBVDPAU
#set(LIBVDPAU_DIR ${CMAKE_BINARY_DIR}/libvdpau)
#ExternalProject_Add(
#        libvdpau
#        GIT_REPOSITORY https://gitlab.freedesktop.org/vdpau/libvdpau.git
#        GIT_TAG 1.5
#        GIT_SHALLOW true
#        PREFIX ${LIBVDPAU_DIR}
#        INSTALL_DIR ${LIBS_INSTALL}
#        #        CONFIGURE_COMMAND echo <INSTALL_DIR>
#        CONFIGURE_COMMAND cd <SOURCE_DIR> && meson --prefix=<INSTALL_DIR> --libdir=<INSTALL_DIR>/lib <INSTALL_DIR>/build
#        BUILD_COMMAND ninja -C <INSTALL_DIR>/build install
#        INSTALL_COMMAND "" #make -C <SOURCE_DIR> install
#)

## LIBDRM
set(LIBDRM_DIR ${CMAKE_BINARY_DIR}/libdrm)
ExternalProject_Add(
        libdrm
        GIT_REPOSITORY https://gitlab.freedesktop.org/mesa/libdrm.git
        GIT_TAG libdrm-2.4.125
        GIT_SHALLOW true
        PREFIX ${LIBDRM_DIR}
        INSTALL_DIR ${LIBS_INSTALL}
        #        CONFIGURE_COMMAND echo <INSTALL_DIR>
        CONFIGURE_COMMAND cd <SOURCE_DIR> && meson --prefix=<INSTALL_DIR> --libdir=<INSTALL_DIR>/lib <INSTALL_DIR>/build
        BUILD_COMMAND ninja -C <INSTALL_DIR>/build install
        INSTALL_COMMAND "" #make -C <SOURCE_DIR> install
)
set(FFNVENC_DIR ${CMAKE_BINARY_DIR}/ffnvenc)
ExternalProject_Add(
        ffnvenc
        GIT_REPOSITORY https://git.videolan.org/git/ffmpeg/nv-codec-headers.git
        GIT_TAG n13.0.19.0
        GIT_SHALLOW true
        PREFIX ${FFNVENC_DIR}
        INSTALL_DIR ${LIBS_INSTALL}
        CONFIGURE_COMMAND ""
        #        CONFIGURE_COMMAND cd <SOURCE_DIR>
        BUILD_COMMAND ""
        #BUILD_COMMAND cd <SOURCE_DIR> && mkdir -p <INSTALL_DIR>/include && cp -r <SOURCE_DIR>/include/ffnvcodec <INSTALL_DIR>/include
        #${CMAKE_COMMAND} -E env PREFIX=${LIBS_INSTALL} make -C <INSTALL_DIR>/build install
        INSTALL_COMMAND make -C <SOURCE_DIR> PREFIX=<INSTALL_DIR> install
)
# FFMPEG
#set(FFMPEG_LIBRARY ${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}ffmpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
set(FFMPEG_DIR ${CMAKE_BINARY_DIR}/ffmpeg)
# --enable-shared for so libs (if set, --enable-pic is not necessary) #--enable-gnutls

# see https://gist.github.com/omegdadi/6904512c0a948225c81114b1c5acb875
# config with everything
# set(FFMPEG_CONFIG --enable-shared --enable-libaom --enable-gpl --enable-gnutls --enable-libass --enable-libfdk-aac --enable-libfreetype --enable-libmp3lame --enable-libopus --enable-libsvtav1 --enable-libdav1d --enable-libvorbis --enable-libvpx --enable-libx264 --enable-libx265 --enable-nonfree)
# potentially enable  --enable-libass  --enable-libfreetype => subtitles |  --enable-libdav1d av1 decode | --enable-libopus opus audio | --enable-libvpx VP8/VP9 |  --enable-libfdk-aac AAC Audio |  --enable-libvorbis  vorbis audio |  --enable-libmp3lame mp3 audio
# --enable-cuda-sdk  --enable-nvenc --enable-ffnvcodec
set(FFMPEG_CONFIG --enable-gpl --enable-libx264 --enable-libx265 --enable-nonfree --enable-vaapi
        --enable-rpath --enable-ffplay --disable-cuda-sdk --disable-cuda-nvcc)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("Enable ffmpeg debug build")
    set(FFMPEG_CONFIG ${FFMPEG_CONFIG} --enable-debug=3 --disable-optimizations --disable-stripping)
    message(${FFMPEG_CONFIG})
endif ()

ExternalProject_Add(
        ffmpeg
        GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
        GIT_TAG release/8.0
        GIT_SHALLOW true
        PREFIX ${FFMPEG_DIR}
        INSTALL_DIR ${LIBS_INSTALL}
        #        CONFIGURE_COMMAND echo <INSTALL_DIR>
        CONFIGURE_COMMAND
        cd <SOURCE_DIR> &&
        #${CMAKE_COMMAND} -E env PATH=/opt/cuda/bin:/usr/bin
        ${CMAKE_COMMAND} -E env PKG_CONFIG_PATH=${LIBS_PKG}
        ${CMAKE_COMMAND} -E env CFLAGS=-I${LIBS_INCLUDE}
        ${CMAKE_COMMAND} -E env LDFLAGS=-L${LIBS_LINK}
        ./configure --prefix=<INSTALL_DIR> ${FFMPEG_CONFIG}
        BUILD_COMMAND make -j -C <SOURCE_DIR>
        INSTALL_COMMAND make -C <SOURCE_DIR> install
)


message("FFMPEG will be installed into ${FFMPEG_INSTALL}")


#add_library(ForexConnect SHARED IMPORTED)
#set_target_properties(ForexConnect PROPERTIES IMPORTED_LOCATION ${FXCM_LIB_DIR}/libForexConnect.so)

## godot-cpp
FetchContent_Declare(
        godot-cpp
        GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
        # GIT_TAG 1421ba26ad045fb23b35ad7360cf5b2f1de55738
        GIT_TAG 4.4
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
        DESCRIPTION "godot video ffmpeg"
        HOMEPAGE_URL "https://tweaklab.org"
        LANGUAGES CXX
)


add_library(${LIBNAME} SHARED
        src/godot/gav_loader.cpp
        src/godot/gav_playback.cpp
        src/godot/gav_settings.cpp
        src/godot/gav_stream.cpp
        src/godot/register_types.cpp
        src/godot/shaders.cpp
        src/godot/vk_ctx.cpp
        src/av_wrapper/av_player.cpp
        src/av_wrapper/av_player.h
        src/av_wrapper/av_helpers.cpp
        src/av_wrapper/av_helpers.h
        src/godot/gav_texture.cpp
        src/godot/gav_texture.h
        src/av_wrapper/av_codecs.cpp
        src/av_wrapper/av_codecs.h
        src/godot/gav_singleton.cpp
        src/godot/gav_singleton.h
)

#target_sources(${LIBNAME}
#        PRIVATE
#        src/av_video.cpp
#        src/av_video.h
#        src/register_types.cpp
#        src/register_types.h
#)

# Fetch a list of the xml files to use for documentation and add to our target
file(GLOB_RECURSE DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/doc_classes/*.xml")

# conditionally add doc data to compile output
if (DOC_XML)
    if (GODOTCPP_TARGET MATCHES "editor|template_debug")
        target_doc_sources(${LIBNAME} ${DOC_XML})
    endif ()
endif ()
set(GODOT_PROJECT_BINARY_DIR "${PROJECT_SOURCE_DIR}/${GODOT_PROJECT_DIR}/bin/${GODOTCPP_PLATFORM}")

set(USE_SYSTEM_FFMPEG OFF CACHE BOOL "Use system installed ffmpeg instead of building from source")
set(USE_GODOT_PATCHED OFF CACHE BOOL "Build for patched godot with vulkan video modules")
set(BUILD_ANDROID OFF CACHE BOOL "Build for Android")

target_include_directories(${LIBNAME} PUBLIC src src/godot)

if (USE_GODOT_PATCHED)
    target_compile_definitions(${LIBNAME} PUBLIC -DGODOT_VULKAN_PATCHED=1)
endif ()


if (BUILD_ANDROID)
    message("building for android")
    target_include_directories(${LIBNAME} PRIVATE "${PROJECT_SOURCE_DIR}/android/ffmpeg/include/arm64-v8a")
    target_link_directories(${LIBNAME} PRIVATE "${PROJECT_SOURCE_DIR}/android/ffmpeg/lib/arm64-v8a")
    target_link_libraries(${LIBNAME} PRIVATE godot-cpp
            avformat avcodec avdevice avutil avfilter swscale swresample
    )
elseif (USE_SYSTEM_FFMPEG)
    message("-- using system ffmpeg")
    #    target_include_directories(${LIBNAME} PRIVATE ${LIBS_INCLUDE}) #${CMAKE_BINARY_DIR}/ffmpeg/src/ffmpeg
    #    target_link_directories(${LIBNAME} PRIVATE ${LIBS_LINK})
    target_link_libraries(${LIBNAME} PRIVATE godot-cpp
            va va-drm va-x11 va-wayland xcb-dri3
            drm Xext vdpau vulkan Xfixes X11-xcb
            avformat avcodec avdevice avutil avfilter swscale swresample
            vulkan aom va fdk-aac mp3lame dav1d opus va-drm va-x11 vdpau vorbis vorbisenc numa
            x264 x265 lzma bz2
    )
else ()
    message("-- using bundled ffmpeg")
    target_include_directories(${LIBNAME} PRIVATE ${LIBS_INCLUDE}) #${CMAKE_BINARY_DIR}/ffmpeg/src/ffmpeg
    target_link_directories(${LIBNAME} PRIVATE ${LIBS_LINK})
    target_link_libraries(${LIBNAME} PRIVATE godot-cpp
            libavformat.a libavcodec.a libavdevice.a libavutil.a libavfilter.a libswscale.a swresample.a
            x264.a x265.a lzma.a bz2_static.a z.a
            #libva.a libva-drm.a libva-glx.a libva-wayland.a libva-x11.a
            #libxcb.a libxcb-dri3.a
            # xcb xcb-dri3
            va va-drm va-x11 va-wayland # OpenCL
            # link system libva, no idea how to use the static libs platform independent
            X11 Xfixes
            vulkan aom fdk-aac mp3lame dav1d opus vdpau vorbis vorbisenc numa
    )
endif ()

#target_link_options(${LIBNAME} PUBLIC -Wl,-rpath ./bin/linux)

set_target_properties(${LIBNAME}
        PROPERTIES
        # The generator expression here prevents msvc from adding a Debug or Release subdir.
        RUNTIME_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"

        PREFIX ""
        OUTPUT_NAME "${LIBNAME}${GODOTCPP_SUFFIX}"
)

#add_custom_command(TARGET ${LIBNAME} PRE_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_INSTALL}/lib/*.so*" "${GODOT_PROJECT_BINARY_DIR}/"
#)
add_custom_command(TARGET ${LIBNAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${LIBNAME}>" "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>"
)


#function(copylibs)
#    file(GLOB FFMPEG_LIBS
#            "${FFMPEG_INSTALL}/lib/*.so*"
#    )
#    file(COPY "$<TARGET_FILE:${LIBNAME}>" DESTINATION "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>" )
#    message(${FFMPEG_LIBS})
#endfunction()
#
#add_custom_command(TARGET ${LIBNAME} POST_BUILD COMMAND  ${CMAKE_COMMAND} )
