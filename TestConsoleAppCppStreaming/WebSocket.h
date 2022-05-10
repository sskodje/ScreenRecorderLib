#pragma once
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>

#pragma comment (lib, "Ws2_32.lib")
#define IDR_WebStreamHtml               102

/** Get handle to current DLL/EXE .*/
static HMODULE CurrentModule() {
    HMODULE module = nullptr;
    LPCTSTR module_ptr = (LPCTSTR)CurrentModule; // pointer to current function
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, module_ptr, &module);
    return module;
}

enum class Connect {
    END,
    CONTINUE,
    STREAM,
    SOCKET_EMPTY,
    SOCKET_FAILURE,
};

class ClientSock; // forward decl

class StreamSockSetter {
public:
    virtual ~StreamSockSetter() = default;
    virtual void SetStreamSocket(ClientSock &s) = 0;
    virtual void Unblock() = 0;
};

class ClientSock final {
public:
    explicit ClientSock(SOCKET cs) : m_sock(cs) {
    }

    // non-assignable
    ClientSock(const ClientSock &) = delete;
    ClientSock &operator = (const ClientSock &) = delete;
    // non-movable (not safe due to threading)
    ClientSock(ClientSock &&other) = delete;
    ClientSock &operator = (ClientSock &&other) = delete;

    Connect Handshake(HWND wnd) {
        // receive request
        std::string receive_buf;
        receive_buf.resize(1024);
        int res = recv(m_sock, const_cast<char *>(receive_buf.data()), static_cast<int>(receive_buf.size()), 0);
        if (res == SOCKET_ERROR)
            return Connect::SOCKET_FAILURE;
        if (res == 0)
            return Connect::SOCKET_EMPTY;
        receive_buf.resize(res);

        const std::string NO_CONTENT_RESPONSE = "HTTP/1.1 204 No Content\r\n\r\n";

        if ((receive_buf.find("Range: bytes=0-") != std::string::npos) || (receive_buf.find("GET /movie.mp4") != std::string::npos)) {
            // streaming video request

            // send HTTP header
            std::string header = "HTTP/1.1 200 OK\r\n";
            header += "Content-Type: video/mp4\r\n";
            header += "Accept-Ranges: none\r\n"; // no support for partial requests
            header += "Access-Control-Allow-Origin: *\r\n";
            header += "Cache-Control: no-store, must-revalidate\r\n";
            header += "\r\n";
            return SendResponse(header, Connect::STREAM);
        }
        else if (receive_buf.find("GET / ") != std::string::npos) {
            // index.html request 

            // load HTML page from resource embedded into DLL/EXE
            HRSRC   html_info = FindResource(CurrentModule(), MAKEINTRESOURCE(IDR_WebStreamHtml), RT_RCDATA);
            HGLOBAL html_handle = LoadResource(CurrentModule(), html_info);
            unsigned int html_len = SizeofResource(CurrentModule(), html_info);

            // send HTTP header
            std::string header = "HTTP/1.1 200 OK\r\n";
            header += "Content-Type: text/html; charset=utf-8\r\n";
            header += "Accept-Ranges: none\r\n"; // no support for partial requests
            header += "Cache-Control: no-store, must-revalidate\r\n";
            header += "Content-Length: " + std::to_string(html_len) + "\r\n";
            header += "\r\n";
            res = send(m_sock, header.data(), static_cast<int>(header.size()), 0);
            if (res == SOCKET_ERROR)
                return Connect::SOCKET_FAILURE;

            res = send(m_sock, (char *)LockResource(html_handle), html_len, 0);
            if (res == SOCKET_ERROR)
                return Connect::SOCKET_FAILURE;

            return Connect::CONTINUE;
        }
        else {
            // unknown request
            std::string header = "HTTP/1.1 404 Not found\r\n";
            header += "Content-Length: 0\r\n";
            header += "\r\n";
            return SendResponse(header, Connect::CONTINUE);
        }
    }

    Connect SendResponse(const std::string &message, Connect success_code) {
        int res = send(m_sock, message.data(), static_cast<int>(message.size()), 0);
        if (res == SOCKET_ERROR)
            return Connect::SOCKET_FAILURE;

        return success_code;
    }

    ~ClientSock() {
        if (m_sock == INVALID_SOCKET)
            return; // already destroyed

        int res = shutdown(m_sock, SD_SEND);
        // deliberately discard errors

        res = closesocket(m_sock);
        // deliberately discard errors

        m_sock = INVALID_SOCKET;

        if (m_thread.joinable())
            m_thread.join();
    }

    void Start(HWND wnd, StreamSockSetter *parent) {
        m_thread = std::thread(&ClientSock::ConnectionThread, this, wnd, parent);
    }

    SOCKET Socket() {
        return m_sock;
    }

private:
    void ConnectionThread(HWND wnd, StreamSockSetter *parent) {
        Connect type;
        do {
            type = Handshake(wnd);
        } while (type == Connect::CONTINUE);

        if (type == Connect::SOCKET_FAILURE) {
            parent->Unblock();
        }
        else if (type == Connect::STREAM) {
            parent->SetStreamSocket(*this);
            parent->Unblock();
        }
    }

    SOCKET      m_sock = INVALID_SOCKET;
    std::thread m_thread;
};


class ServerSock final {
public:
    ServerSock() {
    }
    ServerSock(const char *port_str) {
        WSADATA wsaData = {};
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res)
            throw std::runtime_error("WSAStartup failure");

        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        addrinfo *result = nullptr;
        res = getaddrinfo(NULL, port_str, &hints, &result);
        if (res)
            throw std::runtime_error("getaddrinfo failure");

        m_sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (m_sock == INVALID_SOCKET)
            throw std::runtime_error("socket failure");

        res = bind(m_sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
        if (res == SOCKET_ERROR)
            throw std::runtime_error("bind failure");

        freeaddrinfo(result);
        result = nullptr;

        res = listen(m_sock, SOMAXCONN);
        if (res == SOCKET_ERROR)
            throw std::runtime_error("listen failure");
    }

    // non-assignable class (only movable)
    ServerSock(const ServerSock &) = delete;
    ServerSock &operator = (const ServerSock &) = delete;

    /** Explicit move operators, since the default generated incorrectly assumes value semantics. */
    ServerSock(ServerSock &&other) {
        std::swap(m_sock, other.m_sock);
    }
    ServerSock &operator = (ServerSock &&other) {
        std::swap(m_sock, other.m_sock);
        return *this;
    }

    ~ServerSock() {
        if (m_sock == INVALID_SOCKET)
            return;

        int res = closesocket(m_sock);
        if (res)
            std::terminate();
        m_sock = INVALID_SOCKET;

        res = WSACleanup();
        if (res == SOCKET_ERROR)
            std::terminate();
    }

    std::unique_ptr<ClientSock> WaitForClient() {
        SOCKET cs = accept(m_sock, NULL, NULL);
        if (cs == INVALID_SOCKET)
            return std::unique_ptr<ClientSock>(); // aborted

                                                  // client is now connected
        return std::make_unique<ClientSock>(cs);
    }

private:
    SOCKET m_sock = INVALID_SOCKET;
};
