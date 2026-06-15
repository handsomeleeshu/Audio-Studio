message(STATUS "Audio Studio toolchain: macOS osxcross Clang")

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER o64-clang CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER o64-clang++ CACHE FILEPATH "C++ compiler")
