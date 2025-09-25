#include <sstream>

#include "commutil_log_imp.h"
#include "transport/connection_details.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(ConnectionDetails)

void ConnectionDetails::setHostDetails(uv_tcp_t* socket) {
    sockaddr_in peerAddr = {};
    int len = sizeof(sockaddr_in);
    int res = uv_tcp_getpeername(socket, (struct sockaddr*)&peerAddr, &len);
    if (res < 0) {
        LOG_UV_ERROR(uv_tcp_getpeername, res, "Failed to get TCP connection peer name");
    } else {
        char address[INET_ADDRSTRLEN];
        const char* hostName = inet_ntop(AF_INET, &peerAddr.sin_addr, address, sizeof(address));
        setNetAddress(hostName, ntohs(peerAddr.sin_port));
    }
}

void ConnectionDetails::setHostDetails(const sockaddr_in* addr) {
    char address[INET_ADDRSTRLEN];
    const char* hostName = inet_ntop(AF_INET, &addr->sin_addr, address, sizeof(address));
    setNetAddress(hostName, ntohs(addr->sin_port));
}

std::string ConnectionDetails::makeStrRep() const {
    std::stringstream s;
    s << "{";
    if (m_transportType == TransportType::TT_NET) {
        s << "[NET] " << getHostName() << ":" << m_port;
    } else {
        s << "[PIPE] " << getPipeName();
    }
    s << " <" << getConnectionIndex() << "/" << getConnectionId() << ">}";
    return s.str();
}

}  // namespace commutil
