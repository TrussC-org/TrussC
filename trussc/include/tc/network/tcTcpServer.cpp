// =============================================================================
// tcTcpServer.cpp - TCP server socket implementation
// =============================================================================

#include "tc/network/tcTcpServer.h"
#include "tc/utils/tcLog.h"
#include <cstring>

#ifdef _WIN32
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <errno.h>
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace trussc {

std::atomic<int> TcpServer::instanceCount_{0};

// =============================================================================
// Winsock initialization (Windows only)
// =============================================================================
void TcpServer::initWinsock() {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            tcLogError() << "Winsock initialization failed";
        }
        initialized = true;
    }
#endif
}

void TcpServer::cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// =============================================================================
// Constructor / Destructor
// =============================================================================
TcpServer::TcpServer() {
    if (instanceCount_++ == 0) {
        initWinsock();
    }
}

TcpServer::~TcpServer() {
    stop();
    if (--instanceCount_ == 0) {
        cleanupWinsock();
    }
}

// =============================================================================
// Server management
// =============================================================================
bool TcpServer::start(int port, int maxClients) {
    if (running_) {
        stop();
    }

    port_ = port;
    maxClients_ = maxClients;

    // Create socket
    serverSocket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (serverSocket_ == INVALID_SOCKET) {
#else
    if (serverSocket_ < 0) {
#endif
        notifyError("Failed to create server socket", SOCKET_ERROR_CODE);
        return false;
    }

    // Set SO_REUSEADDR option (to allow immediate port reuse on restart)
    int opt = 1;
#ifdef _WIN32
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // Bind
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (::bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        notifyError("Failed to bind server socket to port " + std::to_string(port), SOCKET_ERROR_CODE);
        CLOSE_SOCKET(serverSocket_);
#ifdef _WIN32
        serverSocket_ = INVALID_SOCKET;
#else
        serverSocket_ = -1;
#endif
        return false;
    }

    // Start listening
    if (::listen(serverSocket_, maxClients) == SOCKET_ERROR) {
        notifyError("Failed to listen on port " + std::to_string(port), SOCKET_ERROR_CODE);
        CLOSE_SOCKET(serverSocket_);
#ifdef _WIN32
        serverSocket_ = INVALID_SOCKET;
#else
        serverSocket_ = -1;
#endif
        return false;
    }

    running_ = true;
    acceptThread_ = std::thread(&TcpServer::acceptThreadFunc, this);

    tcLogNotice() << "TCP server started on port " << port;
    return true;
}

void TcpServer::stop() {
    running_ = false;

    // Close server socket (unblocks accept)
#ifdef _WIN32
    if (serverSocket_ != INVALID_SOCKET) {
        shutdown(serverSocket_, SD_BOTH);
        CLOSE_SOCKET(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }
#else
    if (serverSocket_ >= 0) {
        shutdown(serverSocket_, SHUT_RDWR);  // Required on Linux to unblock accept()
        CLOSE_SOCKET(serverSocket_);
        serverSocket_ = -1;
    }
#endif

    // Wait for accept thread
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    // Disconnect all clients
    disconnectAllClients();

    tcLogNotice() << "TCP server stopped";
}

bool TcpServer::isRunning() const {
    return running_;
}

// =============================================================================
// Accept thread
// =============================================================================
void TcpServer::acceptThreadFunc() {
    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);

#ifdef _WIN32
        SOCKET clientSocket = ::accept(serverSocket_, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) {
#else
        int clientSocket = ::accept(serverSocket_, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket < 0) {
#endif
            if (running_) {
                // Only notify error if occurred while running
                // (ignore errors during server shutdown)
            }
            continue;
        }

        // Get client information
        char hostStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, hostStr, INET_ADDRSTRLEN);
        int clientPort = ntohs(clientAddr.sin_port);

        // Register client
        int clientId;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clientId = nextClientId_++;
            TcpServerClient client;
            client.id_ = clientId;
            client.host_ = hostStr;
            client.port_ = clientPort;
            client.socket_ = clientSocket;
            clients_[clientId] = client;
        }

        tcLogNotice() << "Client " << clientId << " connected from " << hostStr << ":" << clientPort;

        // Notify connection event
        TcpClientConnectEventArgs args;
        args.clientId = clientId;
        args.host = hostStr;
        args.port = clientPort;
        onClientConnect.notify(args);

        // Start receive thread for client
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clientThreads_[clientId] = std::thread(&TcpServer::clientThreadFunc, this, clientId);
        }
    }
}

// =============================================================================
// Client receive thread
// =============================================================================
void TcpServer::clientThreadFunc(int clientId) {
    std::vector<char> buffer(receiveBufferSize_);

#ifdef _WIN32
    SOCKET clientSocket;
#else
    int clientSocket;
#endif

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it == clients_.end()) return;
        clientSocket = it->second.socket_;
    }

    while (running_) {
        int received = static_cast<int>(recv(clientSocket, buffer.data(), buffer.size(), 0));

        if (received > 0) {
            TcpServerReceiveEventArgs args;
            args.clientId = clientId;
            args.data.assign(buffer.begin(), buffer.begin() + received);
            onReceive.notify(args);
        } else if (received == 0) {
            // Client closed connection
            TcpClientDisconnectEventArgs args;
            args.clientId = clientId;
            args.reason = "Connection closed by client";
            args.wasClean = true;
            onClientDisconnect.notify(args);

            tcLogNotice() << "Client " << clientId << " disconnected";
            removeClient(clientId);
            break;
        } else {
            // Error
            int err = SOCKET_ERROR_CODE;
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK) continue;
#else
            if (err == EWOULDBLOCK || err == EAGAIN) continue;
#endif
            if (running_) {
                TcpClientDisconnectEventArgs args;
                args.clientId = clientId;
                args.reason = "Connection error";
                args.wasClean = false;
                onClientDisconnect.notify(args);

                removeClient(clientId);
            }
            break;
        }
    }
}

// =============================================================================
// Client management
// =============================================================================
void TcpServer::disconnectClient(int clientId) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
#ifdef _WIN32
        shutdown(it->second.socket_, SD_BOTH);
        CLOSE_SOCKET(it->second.socket_);
#else
        shutdown(it->second.socket_, SHUT_RDWR);
        CLOSE_SOCKET(it->second.socket_);
#endif
        clients_.erase(it);
    }

    // Detach thread (let it self-terminate)
    auto threadIt = clientThreads_.find(clientId);
    if (threadIt != clientThreads_.end()) {
        if (threadIt->second.joinable()) {
            threadIt->second.detach();
        }
        clientThreads_.erase(threadIt);
    }
}

void TcpServer::disconnectAllClients() {
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& pair : clients_) {
            ids.push_back(pair.first);
        }
    }

    for (int id : ids) {
        disconnectClient(id);
    }

    // Collect threads to join (without holding lock during join to avoid deadlock)
    std::vector<std::thread> threadsToJoin;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& pair : clientThreads_) {
            if (pair.second.joinable()) {
                threadsToJoin.push_back(std::move(pair.second));
            }
        }
        clientThreads_.clear();
    }

    // Join threads without holding mutex
    for (auto& t : threadsToJoin) {
        t.join();
    }
}

void TcpServer::removeClient(int clientId) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        CLOSE_SOCKET(it->second.socket_);
        clients_.erase(it);
    }
}

int TcpServer::getClientCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return static_cast<int>(clients_.size());
}

std::vector<int> TcpServer::getClientIds() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    std::vector<int> ids;
    for (const auto& pair : clients_) {
        ids.push_back(pair.first);
    }
    return ids;
}

const TcpServerClient* TcpServer::getClient(int clientId) const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        return &it->second;
    }
    return nullptr;
}

// =============================================================================
// Data send
// =============================================================================
bool TcpServer::send(int clientId, const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end()) {
        notifyError("Client not found", 0, clientId);
        return false;
    }

    const char* ptr = static_cast<const char*>(data);
    size_t remaining = size;

    while (remaining > 0) {
        int sent = static_cast<int>(::send(it->second.socket_, ptr, remaining, 0));
        if (sent == SOCKET_ERROR) {
            notifyError("Send failed", SOCKET_ERROR_CODE, clientId);
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }

    return true;
}

bool TcpServer::send(int clientId, const std::vector<char>& data) {
    return send(clientId, data.data(), data.size());
}

bool TcpServer::send(int clientId, const std::string& message) {
    return send(clientId, message.data(), message.size());
}

void TcpServer::broadcast(const void* data, size_t size) {
    std::vector<int> ids = getClientIds();
    for (int id : ids) {
        send(id, data, size);
    }
}

void TcpServer::broadcast(const std::vector<char>& data) {
    broadcast(data.data(), data.size());
}

void TcpServer::broadcast(const std::string& message) {
    broadcast(message.data(), message.size());
}

// =============================================================================
// Settings
// =============================================================================
void TcpServer::setReceiveBufferSize(size_t size) {
    receiveBufferSize_ = size;
}

// =============================================================================
// Information retrieval
// =============================================================================
int TcpServer::getPort() const {
    return port_;
}

// =============================================================================
// Error notification
// =============================================================================
void TcpServer::notifyError(const std::string& msg, int code, int clientId) {
    TcpServerErrorEventArgs args;
    args.message = msg;
    args.errorCode = code;
    args.clientId = clientId;
    onError.notify(args);
}

} // namespace trussc
