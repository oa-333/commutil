target_sources(
    commutil
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            comm_util_def.h
            comm_util_err.h
            comm_util_log.h
            comm_util.h)

add_subdirectory(io)
add_subdirectory(transport)
add_subdirectory(msg)