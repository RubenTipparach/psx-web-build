cmake_minimum_required(VERSION 3.25)

# Reference the ps1-bare-metal SDK (local clone in sdk/ folder)
set(SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/sdk")
set(CMAKE_TOOLCHAIN_FILE "${SDK_PATH}/cmake/toolchain.cmake")

project(
	lander-baremetal
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "PSX Lander Model Viewer - Bare Metal"
)

# Set up compiler flags and tools
include(${SDK_PATH}/cmake/setup.cmake)
include(${SDK_PATH}/cmake/tools.cmake)

# Build common library from SDK
add_library(
	common OBJECT
	${SDK_PATH}/src/libc/clz.s
	${SDK_PATH}/src/libc/crt0.c
	${SDK_PATH}/src/libc/cxxsupport.cpp
	${SDK_PATH}/src/libc/malloc.c
	${SDK_PATH}/src/libc/misc.c
	${SDK_PATH}/src/libc/setjmp.s
	${SDK_PATH}/src/libc/string.c
	${SDK_PATH}/src/libc/string.s
	${SDK_PATH}/src/ps1/cache.s
	${SDK_PATH}/src/vendor/printf.c
)
target_include_directories(
	common PUBLIC
	${SDK_PATH}/src
	${SDK_PATH}/src/libc
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/src
)
link_libraries(common)

# Convert texture from PNG to raw 16bpp data
convertImage(
	assets/lander_ship.png 16
	lander/textureData.dat
)

# Build the lander executable
addPS1Executable(
	lander
	src/gpu.c
	src/main.c
	src/trig.c
)

# Embed texture data into executable
addBinaryFile(lander textureData "${PROJECT_BINARY_DIR}/lander/textureData.dat")
