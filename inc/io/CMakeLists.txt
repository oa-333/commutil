
target_sources(
    commutil
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            fixed_input_stream.h
            fixed_output_stream.h
            input_stream.h
            io_def.h
            output_stream.h
            segmented_input_stream.h
            serializable.h)