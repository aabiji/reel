cmake_minimum_required(VERSION 3.28)

# Use ninja as the prefered generator
set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "" FORCE)

project(showtime)
set(output_binary showtime)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

# Compile flags and sources
add_compile_options(-Werror -Wall -Wextra -Wno-unused)

add_executable(${output_binary}
	src/player.cc
	src/decode.cc
	src/main.cc
)

# Link ffmpeg and sdl2
find_package(PkgConfig REQUIRED)
pkg_check_modules(Ffmpeg REQUIRED IMPORTED_TARGET
	libavformat
	libavcodec
	libavutil
	libavfilter
	libswscale
	libswresample
	libavdevice
)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

target_link_libraries(${output_binary}
	PkgConfig::Ffmpeg
	PkgConfig::SDL2
)
