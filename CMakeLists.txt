# CMakeList.txt : CMake project for SDHDServer, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.10)

project ("SDHRServer")

set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -D_FILE_OFFSET_BITS=64")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)

# Add source to this project's executable.
add_executable (SDHRServer "SDHRServer.cpp" "DrawVBlank_implem.h" "SDHRManager.cpp")
target_link_libraries(SDHRServer PkgConfig::LIBDRM PkgConfig::ZLIB)
target_include_directories(SDHRServer PUBLIC "/usr/include/libdrm;/usr/include")

#if (CMAKE_VERSION VERSION_GREATER 3.12)
#  set_property(TARGET SDHRServer PROPERTY CXX_STANDARD 20)
#endif()