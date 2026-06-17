message(STATUS "Audio Studio toolchain: macOS native Clang")

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_VERSION 1)

# Use native macOS compilers
set(CMAKE_C_COMPILER clang CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER clang++ CACHE FILEPATH "C++ compiler")

# macOS-specific flags
set(CMAKE_MACOSX_RPATH ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,@loader_path")
