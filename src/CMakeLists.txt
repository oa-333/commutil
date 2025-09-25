target_sources(commutil PRIVATE
    commutil_common.cpp
    commutil_err.cpp
    commutil_log_imp.cpp
    commutil_tls.cpp
    commutil_win32_dll_event.cpp
    commutil.cpp
    log_buffer.cpp)

add_subdirectory(io)
add_subdirectory(transport)
add_subdirectory(msg)