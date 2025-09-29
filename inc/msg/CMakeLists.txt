
target_sources(
    commutil
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            msg_assembler.h
            msg_backlog.h
            msg_batch_reader.h
            msg_batch_writer.h
            msg_buffer_array_writer.h
            msg_client.h
            msg_config.h
            msg_def.h
            msg_frame_listener.h
            msg_frame_reader.h
            msg_frame_writer.h
            msg_header.h
            msg_listener.h
            msg_multiplexer.h
            msg_request_pool.h
            msg_request.h
            msg_sender.h
            msg_server.h
            msg_session.h
            msg_stat_listener.h
            msg_writer.h
            msg.h)