
target_sources(
    commutil
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            concurrent_hash_table.h
            connection_details.h
            data_allocator.h
            data_client.h
            data_listener.h
            data_loop_listener.h
            data_server.h
            data_stream_client.h
            data_stream_server.h
            pipe_client.h
            pipe_server.h
            tcp_client.h
            tcp_server.h
            udp_client.h
            udp_server.h)