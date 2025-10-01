# commutil Library

Commutil is a lightweight library for I/O and messaging utilities in C++, with special focus on high usability.  
Supported platforms are:

- Windows
- Linux
- MinGW

Compilation with clang is also supported on all platforms.

## Description

The commutil library provides infrastructure for:

- TCP/UDP client/server communication
- Windows names pipe client/server communication
- Unix Domain sockets client/server communication
- Message framing protocol on top of any defined transport layer object

## Getting Started

In order to use the library, first include the main header "comm_util.h", and initialize the library:

    #include "comm_util.h"

    // initialize the library
    commutil::ErrorCode rc = commutil::initCommUtil();
    if (rc != commutil::ErrorCode::E_OK) {
        // handle error
    }

    // do application stuff

    res = commutil::termCommUtil();
    if (rc != commutil::ErrorCode::E_OK) {
        // handle error
    }


### Dependencies & Limitations

The commutil package depends on libuv and gzip.

Feature/pull requests and bug reports are welcome.

### Installing

The library can be built and installed by running:

    build.sh --install-dir <install-path>
    build.bat --install-dir <install-path>

(Checkout the possible options with --help switch).

Add to compiler include path:

    -I<install-path>/include
    
Add to linker flags:

    -L<install-path>/lib -lcommutil

For CMake builds it is possible to use FetchContent as follows:

    FetchContent_Declare(commutil
        GIT_REPOSITORY https://github.com/oa-333/commutil.git
        GIT_TAG v0.1.0
    )
    FetchContent_MakeAvailable(commutil)
    target_include_directories(
        <your project name here>
        PRIVATE
        ${commutil_SOURCE_DIR}/inc
    )
    target_link_libraries(<your project name here> commutil)

commutil supports C++ standard version 11 and above. If your project requires a higher version, make sure to define CMAKE_CXX_STANDARD accordingly before including commutil with FetchContent_Declare().

In the future it may be uploaded to package managers (e.g. vcpkg).

## Help

See [examples](#examples) section below, and documentation in header files for more information.

## Authors

Oren A. (oa.github.333@gmail.com)

## License

This project is licensed under the Apache 2.0 License - see the LICENSE file for details.

# Documentation

## Contents
- [Transport Layer](#transport-layer)
    - [Data Listener](#data-listener)
    - [Data Client](#data-client)
    - [Data Server](#data-server)
    - [TCP Communication](#tcp-communication)
    - [UDP Communication](#udp-communication)
    - [Pipe Communication](#pipe-communication)
- [Messaging Layer](#messaging-layer)
    - [Framing Protocol](#framing-protocol)
    - [Message Listener](#message-listener)
    - [Message Frame Reader and Listener](#message-frame-reader-and-listener)
    - [Message Client](#message-client)
    - [Message Server](#message-server)
    - [Message Sender](#message-sender)
    - [Message Assembly](#message-assembly)
    - [Packing and Sending Frames](#packing-and-sending-frames)

## Transport Layer

### Data Listener

The communication model of commutil is event-driven and follows libuv asynchronous I/O loop model. As such, both client-side and server side communications require a data listener to react to I/O events:

    class MyDataListener : public commutil::DataListener {
    public:
        bool onConnect(const commutil::ConnectionDetails& connectionDetails, int status) override { ... }
        void onDisconnect(const commutil::ConnectionDetails& connectionDetails) override { ... }
        void onReadError(const commutil::ConnectionDetails& connectionDetails, int status) override { ... }
        void onWriteError(const commutil::ConnectionDetails& connectionDetails, int status) override { ... }
        commutil::DataAction onBytesReceived(const commutil::ConnectionDetails& connectionDetails, char* buffer,
                                             uint32_t length, bool isDatagram) override { ... }
        void onBytesSent(const commutil::ConnectionDetails& connectionDetails, uint32_t length,
                         int status) override { ... }
    };

The listener can react to the following major events (both client and server side):

- Connect/disconnect event
- read/write event
- Read/write error event

In the following sections a data listener will be used to 

All transport layer communications 

### Data Client

The data client is the parent class of all client-side communication channels (currently TCP, UDP and pipe/Unix domain sockets). 
The data client should be first initialized with a data listener before starting its communication thread/loop.
Once this is done, messages can be sent through the data client.

In the following example, a TcpClient is used as data client implementation:

    // create TCP client and initialize it
    commutil::TcpClient dataClient("127.0.0.1, 7070, 5000);

    // initialize data client
    MyDataListener listener;
    commutil::ErrorCode rc = dataClient.initialize(&listener);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to initialize TCP client: %s\n", commutil::errorCodeToString(rc));
        return 1;
    }

    // start the asynchronous I/O loop for the client
    rc = dataClient.start();
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to start TCP client running: %s\n", commutil::errorCodeToString(rc));
        return 2;
    }

    // send buffer to server
    char* buffer = ...;
    uint32_t length = ...;
    rc = dataClient.write(buffer, length);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to send message through TCP client: %s\n", commutil::errorCodeToString(rc));
        dataClient.terminate();
        return 3s;
    }

### Data Server

On the other end of the channel, a data server accepts connections and receives incoming messages. 
Just like the data client, the data server needs to be first initialized, then started before being used. 
Unlike the data client, the data server can only reply to incoming messages, which are dispatched to the registered data listener.

So continuing from the previous example, let us add another member to the data listener, so that it can reply to incoming messages:

    class ServerListener : public commutil::DataListener {
    public:
        ServerListener(DataServer* dataServer) : m_dataServer(dataServer) {}

        bool onConnect(const commutil::ConnectionDetails& connectionDetails, int status) override { ... }
        void onDisconnect(const commutil::ConnectionDetails& connectionDetails) override { ... }
        void onReadError(const commutil::ConnectionDetails& connectionDetails, int status) override { ... }
        void onWriteError(const commutil::ConnectionDetails& connectionDetails, int status) override { ... }
        commutil::DataAction onBytesReceived(const commutil::ConnectionDetails& connectionDetails, char* buffer,
                                             uint32_t length, bool isDatagram) override { ... }
        void onBytesSent(const commutil::ConnectionDetails& connectionDetails, uint32_t length,
                         int status) override { ... }

    private:
        DatServer* m_dataServer;
    };

Now, when incoming messages arrive, the data listener can react as follows:

    commutil::DataAction ServerListener::onBytesReceived(const commutil::ConnectionDetails& connectionDetails, char* buffer,
                                                         uint32_t length, bool isDatagram) {
        // process incoming buffer
        ...

        // reply to client
        char* replyBuffer = ...;
        uint32_t replyLength = ...;
        commutil::ErrorCode rc = m_dataServer->replyMsg(connectionDetails, replyBuffer, replyLength);

        // signal transport layer tha the input buffer can be deallocated
        return DataAction::DATA_CAN_DELETE;
    }

In the following example, a TcpServer is used as data server implementation:

    // create TCP server and initialize it
    commutil::TcpServer tcpServer("127.0.0.1", 7070, 20, 8);

    // server parameters: allow for at most 100 concurrent connection, each using 1k preallocated buffer for I/O
    uint32_t maxConnections = 100;
    uint32_t bufferSize = 1024;
    
    // initialize server and start it running
    ServerListener serverListener;
    commutil::ErrorCode rc = tcpServer.initialize(&serverListener, maxConnections, bufferSize);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to initialize TCP server: %s\n", commutil::errorCodeToString(rc));
        return 1;
    }

    // start the server I/O thread/loop running
    rc = tcpServer.start();
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to start TCP server running: %s\n", commutil::errorCodeToString(rc));
        tcpServer.terminate();
        return 2;
    }

From this point onward communication is done in a reactive manner through the registered listener.

### TCP Communication

The transport layer support TCP communication, both on server and client side.  
Client-side communication may be done as follows:

    const char* hostAddress = "127.0.0.1";
    int port = 7070;
    uint64_t reconnectTimeoutMillis = 5000;

    // create TCP client and initialize it
    commutil::TcpClient tcpClient(hostAddress, port, reconnectTimeoutMillis);

    // continue with DataClient API for initializing and starting the data client
    // receive incoming message through data listener

Server-side communication is almost identical:

    const char* hostAddress = "127.0.0.1";
    int port = 7070;

    // allow for at most 20 TCP connections pending accept at any given moment
    int backlog = 20;

    // use 8 I/O threads for processing incoming messages
    uint32_t concurrency = 8;  

    // create TCP server and initialize it
    commutil::TcpServer tcpServer(hostAddress, port, backlog, concurrency);

    // continue with DataServer API for initializing and starting the data server
    // receive incoming message through data listener


### UDP Communication

The transport layer supports UDP communication, both on server and client side.  
Client-side communication may be done as follows:

    const char* hostAddress = "127.0.0.1";
    int port = 7070;

    // create UDP client and initialize it
    commutil::UdpClient udpClient(hostAddress, port);

    // continue with DataClient API for initializing and starting the data client
    // receive incoming message through data listener

Server-side communication is almost identical:

    const char* hostAddress = "127.0.0.1";
    int port = 7070;

    // create UDP server and initialize it
    commutil::UdpServer udpServer(hostAddress, port);

    // continue with DataServer API for initializing and starting the data server
    // receive incoming message through data listener

It is important to note that the commutil's UdpServer class implements a notion of virtual UDP connection, 
so that the data listener could have uniform handling for all types of transport channels.
The idea is that a UDP connection is established during the first incoming message from a client address not seen before.
The virtual UDP connection is declared as disconnected after not hearing from it for some time.
The expiry timeout is configurable in the UdpServer's API.

The reason for doing so, is that upper layer implementation typically allocates resources per client, 
so this would give an opportunity to the upper layer to prepare for client I/O.
The data listener is free to ignore these connect/disconnect events.

### Pipe Communication

Pipe communication is rather similar to TCP communication, differing only by method of initialization.
Client-side communication may be done as follows:

    const char* pipeName = "testPipe";
    uint64_t reconnectTimeoutMillis = 5000;

    // create pipe client and initialize it
    commutil::PipeClient pipeClient(pipeName, reconnectTimeoutMillis);

    // continue with DataClient API for initializing and starting the data client
    // receive incoming message through data listener

Server-side communication is almost identical:

    const char* pipeName = "testPipe";

    // allow for at most 20 TCP connections pending accept at any given moment
    int backlog = 20;

    // use 8 I/O threads for processing incoming messages
    uint32_t concurrency = 8;  

    // create TCP server and initialize it
    commutil::PipeServer pipeServer(pipeName, backlog, concurrency);

    // continue with DataServer API for initializing and starting the data server
    // receive incoming message through data listener

## Messaging Layer

Commutil provides a messaging framework on top the abstract transport layer, which includes:

- message framing protocol (including message batches)
- message client and server
- message listener
- message multiplexing (control message handling parallelism)
- message sender (for managing backlog of failed messages and periodic resending)

In particular a framing protocol is defined such that the application can decide on the binary format of its

### Framing Protocol

The framing protocol of commutil is defined such that message boundaries on the wire can be determined.
This includes:

- Message header with magic bytes for message frame validation
- Resyncing on start of message in case of error (i.e. does not require disconnect and reconnect)
- Sending and receiving batches of messages in one frame (e.g. for reporting frequent events)
- Compression/decompression via gzip
- Matching client requests with server responses

Future versions may support also checksum and keep-alive.

The framing protocol uses the @ref Msg class, which is a message frame, or a message container.

### Message Listener

Both the message client and message server operate following an event-driven model and work with a message listener which can react to 3 main events:

- Connect event
- Disconnect event
- Incoming message event

User code should derive from the message listener and implement the virtual methods:

    class MyMsgListener : public commutil::MsgListener {
    public:
        ~MyMsgListener() override {}

        bool onConnect(const commutil::ConnectionDetails& connectionDetails, int status) override;
        void onDisconnect(const commutil::ConnectionDetails& connectionDetails) override;
        commutil::MsgAction onMsg(const commutil::ConnectionDetails& connectionDetails, commutil::Msg* msg,
                                bool canSaveMsg) override;
    };

The incoming message is a message frame, or a message container, such that the binary payload of the message frame contains the actual application's message(s) being passed back and forth between the application client and the server. 

### Message Frame Reader and Listener

In order to further unpack the message(s) contained within the message frame, a MsgFrameReader is required, and it uses an additional MsgFrameListener:

    class MyFrameListener : public commutil::MsgFrameLister {
    public:
        // handle a single application message contained within the message frame
        commutil::ErrorCode handleMsg(const commutil::ConnectionDetails& connectionDetails,
                            const commutil::MsgHeader& msgHeader, const char* msgBuffer,
                            uint32_t bufferSize, bool lastInBatch, uint32_t batchSize) override;

        // handle error during message frame unpacking
        void handleMsgError(const commutil::ConnectionDetails& connectionDetails,
                            const commutil::MsgHeader& msgHeader, int status) = 0;
    };

The frame reader deals with compression and message batches, and has a simple API:

    commutil::MsgFrameReader frameReader;
    commutil::ByteOrder byteOrder = commutil::ByteOrder::NETWORK_ORDER;
    MyFrameListener frameListener;
    frameReader.initialize(byteOrder, &frameListener);

Typically, the frame reader will be invoked from the message listener, when an incoming message arrives:

    commutil::MsgAction onMsg(const commutil::ConnectionDetails& connectionDetails, commutil::Msg* msg,
                              bool canSaveMsg) {
        // read message(s) from frame, the message will be dispatched to the registered frame listener
        commutil::ErrorCode rc = frameReader.readMsgFrame(connectionDetails, msg);
        if (rc != commutil::ErrorCode::E_OK) {
            fprintf(stderr, "Failed to read message frame: %s\n", commutil::errorCodeToString(rc));
        }
        return commutil::MsgAction::MSG_CAN_DELETE;
    }

The frame reader is used both by the message server and the message sender (a utility class used on top of the message client).

For more details, check the [framing protocol](#packing-and-unpacking-frames) below.

### Message Client

The message client works on top of an initialized data client and implements the commutil framing protocol for sending and receiving messages:

    // client parameters
    commutil::DataClient* dataClient = ...;
    commutil::MsgClient msgClient;
    uint32_t maxConcurrentRequests = 10;
    MyMsgListener listener;
    
    // initialize the message client
    commutil::ErrorCode rc = msgClient.initialize(dataClient, maxConcurrentRequests, &listener);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to initialize message client: %s\n", commutil::errorCodeToString(rc));
        return 1;
    }

Once the message client is initialized, the transport layer's I/O thread/loop can be started:

    rc = msgClient.start();
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to start message client: %s\n", commutil::errorCodeToString(rc));
        return 2;
    }

From this point onward, the message client can be used both for synchronous and asynchronous messaging:

    // prepare a message and send it without blocking
    Msg* msg = ...;
    rc = msgClient.sendMsg(msg);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to send message: %s\n", commutil::errorCodeToString(rc));
        return 3;
    }

    // listener will receive the response later

In order to do synchronous messaging, the transactMsgResponse API should be used:

    // prepare a message
    Msg* msg = ...;
    Msg* response = nullptr;

    // send message and wait for matching response to arrive
    rc = msgClient.transactMsgResponse(msg, &response);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to send/receive message: %s\n", commutil::errorCodeToString(rc));
        return 4;
    }

    // handle response (unpack frame, deserialize payload, process application message)
    ...

    // deallocate response when done
    commutil::freeMsg(response);

Pay attention that when synchronous messaging is used, the registered message listener will not be notified of the incoming message.
The transaction API is designed mainly for CLI applications that communicate with a server.

### Message Server

The message server operates in a similar manner on top of the transport layer, but uses a frame listener instead of a message listener:

    // server parameters
    commutil::DataServer* dataServer = ...;
    uint32_t maxConnections = 100;
    uint32_t concurrency = 8;
    bufferSize = 1024;
    
    // initialize the message server
    commutil::MsgServer msgServer;
    MyMsgListener listener;
    commutil::ErrorCode rc = msgServer.initialize(dataServer, maxConnections, concurrency, bufferSize, &listener);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to initialize message server: %s\n", commutil::errorCodeToString(rc));
        return 1;
    }

Once the message server is initialized, the transport layer's I/O thread/loop can be started:

    rc = msgServer.start();
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to start message server: %s\n", commutil::errorCodeToString(rc));
        return 2;
    }

From this point onward, incoming messages (already unpacked from the message frame), will be dispatched to the registered listener.
The listener in return can reply to messages as follows:

    commutil::ErrorCode handleMsg(const commutil::ConnectionDetails& connectionDetails,
                                const commutil::MsgHeader& msgHeader, const char* msgBuffer,
                                uint32_t bufferSize, bool lastInBatch, uint32_t batchSize) {
        // compose application message from the message buffer
        // handle incoming message

        // prepare response
        Msg* response = ...;

        // NOTE: server might consider to reply only when processing the last message in a batch

        // reply to client
        commutil::ErrorCode rc = msgServer.replyMsg(connectionDetails, response);
        if (rc != commutil::ErrorCode::E_OK) {
            fprintf(stderr, "Failed to reply to client %s, request %" PRIu64 ": %s\n",
                    connectionDetails.toString(), msgHeader.getRequestId(), commutil::errorCodeToString(rc));
        }
        return rc;
    }

### Message Sender

The message sender is a utility class for adding the following handy functionality on top of the message client:

- Packing raw messages into message frames before sending
- Resending failed messages, either due to transport layer errors, or due to missing or bad responses from the server

The resend mechanism is rather complex, and is implemented as a timer task within the I/O loop of the transport layer.
It provides the following functionality:

- Keeps failed outgoing messages in a backlog for periodic resending
- Stop resending messages after some configurable expiration period
- Drop excess backlog messages if exceeding some configurable space limit
- Take care of late responses
- Renew request id and resend when server responds with an error

The following example illustrates how to use a message sender:

    // client parameters
    commutil::DataClient* dataClient = ...;
    commutil::MsgClient msgClient;
    uint32_t maxConcurrentRequests = 10;
    MyMsgListener listener;
    
    // initialize the message client
    commutil::ErrorCode rc = msgClient.initialize(dataClient, maxConcurrentRequests, &listener);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to initialize message client: %s\n", commutil::errorCodeToString(rc));
        return 1;
    }

    // now initialize the message sender
    MsgSender msgSender;
    rc = msgSender.initialize(&msgClient);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to initialize message sender: %s\n", commutil::errorCodeToString(rc));
        msgClient.terminate();
        return 2;
    }

    // and start it running
    rc = msgSender.start();
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to start message sender: %s\n", commutil::errorCodeToString(rc));
        msgSender.terminate();
        msgClient.terminate();
        return 3;
    }

Once the message sender is ready, we can start sending messages.  
The message sender takes care of packing messages into message frames:

    // pack single message into message frame and send to server
    uint16_t msgId = TEST_MSG_ID;
    const char* msg = ...;
    size_t len = ...;
    rc = msgSender.send(msgId, msg, len);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to send a message through the message sender: %s\n", commutil::errorCodeToString(rc));
        return 4;
    }

    // the response will arrive unpacked to the registered response handler

The message sender also supports request-response transactions:

    // send message and block until the matching response arrives from the server
    rc = msgSender.transactMsg(msgId, msg, len);
    if (rc != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to send/receive a message through the message sender: %s\n", commutil::errorCodeToString(rc));
        return 4;
    }

### Message Assembly

The framing protocol defines a Msg class which is actually a protocol message container. In other words the binary payload of the Msg class contains serialized application messages. The framing protocol provides re-assembly services on top of incoming data buffers from the protocol layer. The class that is responsible for that is @ref MsgAssembler.

The message assembler is typically registered as DataListener at the transport layer, and then processes incoming buffers to produces whole message frames. It notifies a registered MsgListener of ready messages. The message client and message server are registered as the message listener of their respective message assemblers.

### Packing and Sending Frames

In the examples above, we either saw a ready Msg* message frame, or that flat buffers of serialized messages were sent to the server.  
In this section we will take a look into how such a message frame can be built.

The first option is to use the message sender's API for packing single messages:

    ErrorCode sendMsg(uint16_t msgId, const char* body, size_t len, bool compress = false,
                      uint16_t flags = 0);

In order to build a message frame that contains a batch of messages (of the same message type), the batch writer can be used as follows:

- Serialize all messages into a MsgBufferArray
- Use MsgBufferArrayWriter to serialize the buffer array into the message frame payload
- Send the message frame

The following code snippet illustrates that:

    commutil::MsgBufferArray msgBufferArray;
    commutil::MsgBuffer& msgBuffer = m_msgBufferArray.emplace_back();
    // serialize message into buffer
    ...

    // send buffer array as a batch
    commutil::ErrorCode rc = m_msgSender.sendMsgBatch(msgId, msgBufferArray, compress, COMMUTIL_MSG_FLAG_BATCH);

The following full example from the [ELog Logging Framework](https://github.com/oa-333/elog) illustrates theses ideas:

    // pick the next buffer from the buffer array
    commutil::MsgBuffer& msgBuffer = m_msgBufferArray.emplace_back();

    // serialize the log record into the next buffer
    ...

    // when a few buffers are serialized, we send a full frame
    bool res = true;
    if (!m_msgBufferArray.empty()) {
        if (m_syncMode) {
            // synchronous mode, we block until a response arrives or we time out
            commutil::ErrorCode rc =
                m_msgSender.transactMsgBatch(ELOG_RECORD_MSG_ID, m_msgBufferArray, m_compress,
                                             0, m_msgConfig.m_sendTimeoutMillis);

            if (rc != commutil::ErrorCode::E_OK) {
                ELOG_REPORT_ERROR("Failed to transact message: %s",
                                  commutil::errorCodeToString(rc));
                res = false;
            }
        } else {
            // asynchronous mode, we send the message batch and don't block for a response
            commutil::ErrorCode rc = m_msgSender.sendMsgBatch(ELOG_RECORD_MSG_ID, m_msgBufferArray, m_compress);

            if (rc != commutil::ErrorCode::E_OK) {
                ELOG_REPORT_ERROR("Failed to transact message: %s",
                                  commutil::errorCodeToString(rc));
                res = false;
            }
        }

        // clear the buffer array for next round
        m_msgBufferArray.clear();
    }

    // NOTE: if resend needs to take place, then the body has already been copied to the backlog
    return res;