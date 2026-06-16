message(STATUS "Audio Studio toolchain: Windows MinGW")

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++ CACHE FILEPATH "C++ compiler")
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres CACHE FILEPATH "Windows resource compiler")
