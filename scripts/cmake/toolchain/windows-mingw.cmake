message(STATUS "Audio Studio toolchain: Windows MinGW")

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix CACHE FILEPATH "C++ compiler")
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres CACHE FILEPATH "Windows resource compiler")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
