#ifndef __CONNECTION_DETAILS_H__
#define __CONNECTION_DETAILS_H__

#include <uv.h>

#include <cassert>
#include <cstdint>
#include <string>

#include "comm_util_def.h"
#include "comm_util_log.h"

// connection details:
// server/client side
// pipe: just name
// tpc/udp:server side address
// client side connection bound address

// it has been decided to sacrifice polymorphism for performance, and avoid cloning and heap
// allocations. as a result this file is not following th open/close principle, and is subject to
// possible rare changes.

namespace commutil {

/** @brief Transport type constants. */
enum class TransportType {
    /** @brief Network transport type (currently means only IP addresses). */
    TT_NET,

    /** @brief IPC transport type. Currently this means only names pipes on Windows, or Unix Domain
     * Sockets on Linux.
     */
    TT_IPC
};

class COMMUTIL_API ConnectionDetails {
public:
    /** Constructor for network address. */
    ConnectionDetails(const char* hostName = "", int port = 0, uint64_t connId = 0,
                      uint64_t connIndex = 0)
        : m_transportType(TransportType::TT_NET),
          m_name(hostName),
          m_port(port),
          m_connId(connId),
          m_connIndex(connIndex) {}

    /** Constructor for IPC address. */
    ConnectionDetails(const char* pipeName, uint64_t connId = 0, uint64_t connIndex = 0)
        : m_transportType(TransportType::TT_NET),
          m_name(pipeName),
          m_port(0),
          m_connId(connId),
          m_connIndex(connIndex) {}

    ConnectionDetails(const ConnectionDetails&) = default;
    ConnectionDetails(ConnectionDetails&&) = default;
    ConnectionDetails& operator=(const ConnectionDetails&) = default;
    ~ConnectionDetails() {}

    /** @brief Retrieves the transport type of the connection details. */
    inline TransportType getTransportType() const { return m_transportType; }

    /** @brief Retrieves the host name of a network address. */
    inline const char* getHostName() const {
        assert(m_transportType == TransportType::TT_NET);
        return m_name.c_str();
    }

    /** @brief Retrieves the port of a network address. */
    inline int getPort() const {
        assert(m_transportType == TransportType::TT_NET);
        return m_port;
    }

    /** @brief Sets the host name of a network address. */
    inline void setHostName(const char* hostName) {
        m_transportType = TransportType::TT_NET;
        m_name = hostName;
    }

    /** @brief Sets the port of a network address. */
    inline void setPort(int port) {
        m_transportType = TransportType::TT_NET;
        m_port = port;
    }

    /** @brief Sets the host/port of a network address. */
    inline void setNetAddress(const char* hostName, int port) {
        m_transportType = TransportType::TT_NET;
        m_name = hostName;
        m_port = port;
    }

    /** @brief Sets the host details from a tcp socket. */
    void setHostDetails(uv_tcp_t* socket);

    /** @brief Sets the host details from a socket address. */
    void setHostDetails(const sockaddr_in* addr);

    /** @brief Retrieves the pipe name of an IPC address. */
    inline const char* getPipeName() const {
        assert(m_transportType == TransportType::TT_IPC);
        return m_name.c_str();
    }

    /** @brief Sets the pipe name of an IPC address. */
    inline void setPipeName(const char* pipeName) {
        m_transportType = TransportType::TT_IPC;
        m_name = pipeName;
    }

    /** @brief Retrieves the unique connection id (running id). */
    inline uint64_t getConnectionId() const { return m_connId; }

    /** @brief Retrieves the reusable connection index. */
    inline uint64_t getConnectionIndex() const { return m_connIndex; }

    /** @brief Sets the unique connection id. */
    inline void setConnectionId(uint64_t connectionId) {
        m_connId = connectionId;
        m_strRepValid = false;
    }

    /** @brief Sets the reusable connection index. */
    inline void setConnectionIndex(uint32_t connectionIndex) {
        m_connIndex = connectionIndex;
        m_strRepValid = false;
    }

    /** @brief Retrieves a string representation on the connection details. */
    inline const char* toString() const {
        if (!m_strRepValid) {
            m_strRep = makeStrRep();
        }
        return m_strRep.c_str();
    }

protected:
    std::string makeStrRep() const;

private:
    /** @brief Transport type. */
    TransportType m_transportType;

    /** @brief Pipe name or host name. */
    std::string m_name;

    /** @brief IP address port. */
    int m_port;

    /** @var The connection's unique id (running number). */
    uint64_t m_connId;

    /** @var The index of the connection in the server's connection array. */
    uint64_t m_connIndex;

    /** @var String representation. */
    mutable std::string m_strRep;

    /** @var Specifies whether string representation is valid. */
    mutable bool m_strRepValid;

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __CONNECTION_DETAILS_H__