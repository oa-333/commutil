#ifndef __DATA_LISTENER_H__
#define __DATA_LISTENER_H__

#include <cstdint>

#include "comm_util_def.h"
#include "transport/connection_details.h"

namespace commutil {

// forward declaration
class COMMUTIL_API DataClient;

class COMMUTIL_API DataListener {
public:
    virtual ~DataListener() {}

    /**
     * @brief Notifies that a data connection is ready for I/O. In the case of datagram server,
     * this notification arrives on the first time a message is received from a new client address.
     * In such a case no disconnect notification will be delivered to the listener for that datagram
     * client.
     * @param connectionDetails The connection details.
     * @param status The operation status. Zero means success. Any other value denotes an error.
     * @return True if the connection is to be accepted.
     * @return False if the connection is to be rejected. Returning false will cause the connection
     * to be closed.
     */
    virtual bool onConnect(const ConnectionDetails& connectionDetails, int status) = 0;

    /**
     * @brief Notifies that a data client has disconnected and cannot be used for I/O anymore
     * (stream channel only).
     * @param client The disconnecting client.
     */
    virtual void onDisconnect(const ConnectionDetails& connectionDetails) = 0;

    /**
     * @brief Notify about client read error.
     * @param connectionDetails The connection details.
     */
    virtual void onReadError(const ConnectionDetails& connectionDetails, int status) = 0;

    /**
     * @brief Notify about client write error.
     * @param connectionDetails The connection details.
     */
    virtual void onWriteError(const ConnectionDetails& connectionDetails, int status) = 0;

    /**
     * @brief Handle incoming data from a connected client.
     * @param connectionDetails The connection details.
     * @param buffer The buffer pointer.
     * @param length The buffer length.
     * @param isDatagram Specifies whether this is a full datagram.
     */
    virtual void onBytesReceived(const ConnectionDetails& connectionDetails, char* buffer,
                                 uint32_t length, bool isDatagram) = 0;

    /**
     * @brief Notify data from connected client was sent to the client.
     * @param connectionDetails The connection details.
     * @param length The number of bytes sent.
     * @param status The operation result status.
     */
    virtual void onBytesSent(const ConnectionDetails& connectionDetails, uint32_t length,
                             int status) = 0;

protected:
    DataListener() {}
    DataListener(const DataListener&) = delete;
    DataListener(DataListener&&) = delete;
    DataListener& operator=(const DataListener&) = delete;
};

}  // namespace commutil

#endif  // __DATA_LISTENER_H__