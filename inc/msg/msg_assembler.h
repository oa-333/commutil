#ifndef __MSG_ASSEMBLER_H__
#define __MSG_ASSEMBLER_H__

#include "comm_util_log.h"
#include "io/segmented_input_stream.h"
#include "msg/msg_listener.h"
#include "transport/data_allocator.h"
#include "transport/data_listener.h"

namespace commutil {

/** @brief A message assembler utility class for assembling messages from incoming connections. */
class COMMUTIL_API MsgAssembler : public DataListener {
public:
    MsgAssembler() : m_listener(nullptr), m_msgStreams(nullptr), m_streamCount(0) {}
    MsgAssembler(const MsgAssembler&) = delete;
    MsgAssembler(MsgAssembler&&) = delete;
    MsgAssembler& operator=(const MsgAssembler&) = delete;
    ~MsgAssembler() final {}

    /**
     * @brief Initializes the message assembler.
     * @param maxConnections The maximum number of active connections.
     * @param byteOrder Specifies whether the input stream is using big endian byte order.
     * @param dataAllocator The transport layer's data allocator (required for deallocating incoming
     * data buffers).
     * @param listener The message listener.
     */
    ErrorCode initialize(uint32_t maxConnections, ByteOrder byteOrder, DataAllocator* dataAllocator,
                         MsgListener* listener);

    /** @brief Terminates the message assembler. */
    ErrorCode terminate();

    /**
     * @brief Notify incoming connection.
     * @param connectionDetails The connection details.
     * @param status The operation status. Zero means success. Any other value denotes an error.
     * @return True if the connection is to be accepted.
     * @return False if the connection is to be rejected. Returning false will cause the connection
     * to be closed.
     */
    bool onConnect(const ConnectionDetails& connectionDetails, int status) final;

    /**
     * @brief Notify connection closed.
     * @param connectionDetails The connection details.
     */
    void onDisconnect(const ConnectionDetails& connectionDetails) final;

    /**
     * @brief Notify client about read error.
     * @param connectionDetails The connection details.
     */
    void onReadError(const ConnectionDetails& connectionDetails, int status) final;

    /**
     * @brief Notify client about write error.
     * @param connectionDetails The connection details.
     */
    void onWriteError(const ConnectionDetails& connectionDetails, int status) final;

    /**
     * @brief Handle incoming data from a connected client.
     * @param connectionDetails The connection details.
     * @param buffer The buffer pointer.
     * @param length The buffer length.
     * @param isDatagram Specifies whether this is a full datagram.
     */
    DataAction onBytesReceived(const ConnectionDetails& connectionDetails, char* buffer,
                               uint32_t length, bool isDatagram) final;

    /**
     * @brief Notify data from connected client was sent to the client.
     * @param connectionDetails The connection details.
     * @param length The number of bytes sent.
     * @param status The operation result status.
     */
    void onBytesSent(const ConnectionDetails& connectionDetails, uint32_t length, int status) final;

private:
    struct MsgStream {
        SegmentedInputStream m_is;
        MsgHeader m_msgHeader;
        enum MsgState { MS_WAITING_HEADER, MS_WAITING_BODY, MS_RESYNC } m_state;

        MsgStream(ByteOrder byteOrder, SegmentedInputStream::BufferDeallocator* bufferDeallocator)
            : m_is(byteOrder, bufferDeallocator), m_state(MS_WAITING_HEADER) {}
        MsgStream(const MsgStream&) = delete;
        MsgStream(MsgStream&&) = delete;
        MsgStream& operator=(const MsgStream&) = delete;
        ~MsgStream() {}

        void reset() {
            m_is.reset();
            m_state = MS_RESYNC;
        }
    };

    class BufferDeallocator : public SegmentedInputStream::BufferDeallocator {
    public:
        BufferDeallocator() : m_dataAllocator(nullptr) {}
        BufferDeallocator(const BufferDeallocator&) = delete;
        BufferDeallocator(BufferDeallocator&&) = delete;
        BufferDeallocator& operator=(const BufferDeallocator&) = delete;
        ~BufferDeallocator() override {}

        inline void setDataAllocator(DataAllocator* dataAllocator) {
            m_dataAllocator = dataAllocator;
        }

        void deallocateBuffer(char* buffer) override { m_dataAllocator->freeRequestBuffer(buffer); }

    private:
        DataAllocator* m_dataAllocator;
    };

    MsgListener* m_listener;
    MsgStream** m_msgStreams;
    uint32_t m_streamCount;
    BufferDeallocator m_bufferDeallocator;

    void onWaitHeader(MsgStream* msgStream);
    bool onWaitBody(MsgStream* msgStream, const ConnectionDetails& connectionDetails);
    void onResync(MsgStream* msgStream);

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_ASSEMBLER_H__