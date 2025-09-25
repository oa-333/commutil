#ifndef __COMM_UTIL_DEF_H__
#define __COMM_UTIL_DEF_H__

// Windows/MSVC
#ifdef _MSC_VER
// include windows header unless instructed not to do so
#ifndef COMMUTIL_NO_WINDOWS_HEADER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#define COMMUTIL_WINDOWS
#define COMMUTIL_MSVC

#define DLL_EXPORT __declspec(dllexport)
#define DLL_IMPORT __declspec(dllimport)
#ifdef COMMUTIL_DLL
#define COMMUTIL_API DLL_EXPORT
#else
#define COMMUTIL_API DLL_IMPORT
#endif

// Windows/MinGW stuff
#elif defined(__MINGW32__) || defined(__MINGW64__)
#define COMMUTIL_WINDOWS
#define COMMUTIL_MINGW
#define COMMUTIL_GCC
#define COMMUTIL_API

// Linux stuff
#elif defined(__linux__)
#define COMMUTIL_LINUX
#define COMMUTIL_GCC
#define COMMUTIL_API

// unsupported platform
#else
#error "Unsupported platform"
#endif

// define incorrect __cplusplus on MSVC
#if defined(COMMUTIL_WINDOWS) && defined(_MSVC_LANG)
#define COMMUTIL_CPP_VER _MSVC_LANG
#else
#define COMMUTIL_CPP_VER __cplusplus
#endif

// define fallthrough attribute
#if (COMMUTIL_CPP_VER >= 201703L)
#define COMMUTIL_FALLTHROUGH [[fallthrough]]
#else
#define COMMUTIL_FALLTHROUGH
#endif

// define strcasecmp for MSVC
#ifdef COMMUTIL_MSVC
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

#endif  // __COMM_UTIL_DEF_H__