cmake_minimum_required(VERSION 3.23)
include(FetchContent)

#############################################################
# project definition
#############################################################
project(commutil VERSION 1.0 DESCRIPTION "Communication Utilities Library" LANGUAGES CXX)

# build shared library
add_library(commutil SHARED)
set_target_properties(commutil PROPERTIES POSITION_INDEPENDENT_CODE ON)

#############################################################
# C++ version, should be inherited from outer project
# Otherwise defaults to C++ 11
#############################################################

if (DEFINED CMAKE_CXX_STANDARD)
    message(STATUS "commutil Using externally defined CXX standard: ${CMAKE_CXX_STANDARD}")
else()
    message(STATUS "CXX standard not defined, commutil defaulting to cxx_std_11")
    target_compile_features(commutil PUBLIC cxx_std_11)
endif()

#############################################################
# clang flags
#############################################################

set(commutil_CLANG OFF)
if ("$ENV{CXX}" MATCHES ".*clang.*")
    set(commutil_CLANG ON)
endif()

if (commutil_CLANG)
    target_compile_options(commutil PRIVATE -Wno-c++98-compat)
    target_compile_options(commutil PRIVATE -Wno-c++98-compat-pedantic)
    target_compile_options(commutil PRIVATE -Wno-old-style-cast)
    target_compile_options(commutil PRIVATE -Wno-final-dtor-non-final-class)
    target_compile_options(commutil PRIVATE -Wno-newline-eof)
    target_compile_options(commutil PRIVATE -Wno-gnu-zero-variadic-macro-arguments)
    target_compile_options(commutil PRIVATE -Wno-unsafe-buffer-usage)
    target_compile_options(commutil PRIVATE -Wno-reserved-macro-identifier)
    target_compile_options(commutil PRIVATE -Wno-covered-switch-default)
    target_compile_options(commutil PRIVATE -Wno-format-nonliteral)
    target_compile_options(commutil PRIVATE -Wno-unused-command-line-argument)
    target_compile_options(commutil PRIVATE -Wno-extra-semi-stmt)
    target_compile_options(commutil PRIVATE -Wno-cast-align)
    target_compile_options(commutil PRIVATE -Wno-exit-time-destructors)
    target_compile_options(commutil PRIVATE -Wno-global-constructors)
endif()


#############################################################
# Compile flags
#############################################################
if (MSVC)
    # /Zi - generate full debug information
    # /EHsc - catch(..) catches only C++ exceptions, extern "C" functions never throw C++ exception
    target_compile_definitions(commutil PRIVATE COMMUTIL_DLL)
    target_compile_options(commutil PRIVATE /Zi /EHsc)
    target_compile_options(commutil PRIVATE /Wall)
    target_compile_options(commutil PRIVATE /wd4251)
    target_compile_options(commutil PRIVATE /wd4820)
    target_compile_options(commutil PRIVATE /wd5045)
    target_compile_options(commutil PRIVATE /wd5039)
    target_link_libraries(commutil Ws2_32 dbghelp psapi)
elseif(MINGW)
    target_link_libraries(commutil ws2_32 dbghelp psapi unwind)
    target_compile_options(commutil PRIVATE -Wall)
else()
    target_link_libraries(commutil unwind)
    target_compile_options(commutil PRIVATE -Wall)
    add_link_options(-rdynamic)
endif()

#############################################################
# libuv
#############################################################
if (MSVC)
    find_package(libuv CONFIG REQUIRED)
    target_link_libraries(commutil $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
else()
    message(STATUS "Linking with libuv on UNIX system")
    target_link_libraries(commutil uv)
endif()

#############################################################
# gzip
#############################################################
# get gzip headers path
# NOTE: on Linux/MingW we need to get gzip-hpp explicitly (not using a package manager yet)
if (MSVC)
    set(GZIP_HPP_INCLUDE_DIRS "$ENV{VCPKG_ROOT}/packages/gzip-hpp_x64-windows/include")
else()
    message(STATUS "Getting gzip-hpp")
    FetchContent_Declare(gzip-hpp
        SYSTEM 
        EXCLUDE_FROM_ALL # NOTE: without this, gzip-hpp benchmark gets compiled into the project
        GIT_REPOSITORY https://github.com/mapbox/gzip-hpp
        GIT_TAG v0.1.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(gzip-hpp)
endif()

# now we can get gzip headers path
find_path(GZIP_HPP_INCLUDE_DIRS gzip/compress.hpp)
if (NOT GZIP_HPP_INCLUDE_DIRS)
    # This is a temporary workaround, not sure why find_path() fails on Linux/MinGW
    message(STATUS "Setting gzip include path manually")
    if (EXISTS "${gzp-hpp_SOURCE_DIR}/include")
        message(STATUS "Setting gzip include path to: ${gzp-hpp_SOURCE_DIR}/include")
        set(GZIP_HPP_INCLUDE_DIRS ${gzp-hpp_SOURCE_DIR}/include)
    else()
        message(STATUS "Setting gzip include path to: ${CMAKE_CURRENT_BINARY_DIR}/_deps/gzip-hpp-src/include")
        set(GZIP_HPP_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/_deps/gzip-hpp-src/include)
    endif()
endif()
message(STATUS "Adding gzip include path: ${GZIP_HPP_INCLUDE_DIRS}")
target_include_directories(commutil PRIVATE ${GZIP_HPP_INCLUDE_DIRS})

# gzip requires zlib
message(STATUS "Searching for required package ZLIB")
find_package(ZLIB REQUIRED)
target_link_libraries(commutil ZLIB::ZLIB)
target_compile_definitions(commutil PRIVATE ZLIB_CONST)

#############################################################
# include path
#############################################################
target_include_directories(commutil PRIVATE inc)
target_include_directories(commutil PRIVATE src)

#############################################################
# source folders
#############################################################
add_subdirectory(src)

#############################################################
# local install for tests
#############################################################
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "Installing at ${CMAKE_INSTALL_PREFIX}")

    # public headers
    add_subdirectory(inc)

    # install headers in install dir (irrelevant to fetch content)
    install(TARGETS commutil FILE_SET publicheaders)
    
    # unique system name
    if (MINGW)
        set(SYSTEM_NAME "${CMAKE_SYSTEM_NAME}_mingw-${CMAKE_BUILD_TYPE}")
    else()
        set(SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}")
    endif()

    # install in common bin dir for other projects (irrelevant to fetch content)
    set(commutil_INSTALL_REL_PATH ${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME})
    cmake_path(NORMAL_PATH commutil_INSTALL_REL_PATH OUTPUT_VARIABLE commutil_INSTALL_PATH)
    make_directory(${commutil_INSTALL_PATH})
    install(TARGETS commutil LIBRARY DESTINATION ${commutil_INSTALL_PATH})
    install(TARGETS commutil RUNTIME DESTINATION ${commutil_INSTALL_PATH})
endif()