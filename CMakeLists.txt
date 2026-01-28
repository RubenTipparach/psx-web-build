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

# Set path to local psxavenc
set(PSXAVENC_PATH "${CMAKE_CURRENT_LIST_DIR}/tools/psxavenc/psxavenc.exe" CACHE FILEPATH "Path to psxavenc" FORCE)

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

# Convert font from PNG to raw 4bpp indexed data (texture + palette)
convertImage(
	assets/font.png 4
	lander/fontTexture.dat
	lander/fontPalette.dat
)

# Convert audio from WAV to SPU-ADPCM format (10-second guitar clip)
convertAudioSample(
	assets/guitar_10s.wav
	22050
	"${PROJECT_BINARY_DIR}/lander/audioData.spu"
)

# Convert 3D model from OBJ to binary format
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/modelData.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/ship_low_poly.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertModel.py"
		"${PROJECT_SOURCE_DIR}/assets/ship_low_poly.obj"
		"${PROJECT_BINARY_DIR}/lander/modelData.bin"
	VERBATIM
)

# Build the lander executable
addPS1Executable(
	lander
	src/gpu.c
	src/spu.c
	src/model.c
	src/font.c
	src/main.c
	src/trig.c
)

# Embed texture data into executable
addBinaryFile(lander textureData "${PROJECT_BINARY_DIR}/lander/textureData.dat")

# Embed model data into executable
addBinaryFileWithSize(lander modelData modelData_size "${PROJECT_BINARY_DIR}/lander/modelData.bin")

# Embed font data into executable
addBinaryFile(lander fontTexture "${PROJECT_BINARY_DIR}/lander/fontTexture.dat")
addBinaryFile(lander fontPalette "${PROJECT_BINARY_DIR}/lander/fontPalette.dat")

# Embed audio data into executable
addBinaryFileWithSize(lander audioData audioData_size "${PROJECT_BINARY_DIR}/lander/audioData.spu")
