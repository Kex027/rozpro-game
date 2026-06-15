#include "network_client.h"
#include "../common/protocol.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <cstring>
#include <vector>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define close_socket closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define close_socket close
#endif

static SOCKET s_socket = INVALID_SOCKET;
static bool s_connected = false;
static uint32_t s_my_player_id = 0;
static bool s_running = true;
static GameState s_state;
static std::mutex s_state_mutex;

static std::vector<std::string> s_alerts;
static std::mutex s_alerts_mutex;
static std::string s_error = "";

static bool send_raw_packet(SOCKET sock, uint16_t type, const void* payload, uint32_t payload_len) {
    PacketHeader header;
    header.type = type;
    header.length = payload_len;

    std::vector<char> send_buf(sizeof(header) + payload_len);
    std::memcpy(send_buf.data(), &header, sizeof(header));
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(send_buf.data() + sizeof(header), payload, payload_len);
    }

    int bytes_sent = 0;
    int total_bytes = static_cast<int>(send_buf.size());
    while (bytes_sent < total_bytes) {
        int n = send(sock, send_buf.data() + bytes_sent, total_bytes - bytes_sent, 0);
        if (n <= 0) return false;
        bytes_sent += n;
    }
    return true;
}

static bool recv_all(SOCKET sock, char* buffer, int size) {
    int bytes_read = 0;
    while (bytes_read < size) {
        int n = recv(sock, buffer + bytes_read, size - bytes_read, 0);
        if (n <= 0) return false;
        bytes_read += n;
    }
    return true;
}

static void network_receive_loop() {
    char read_buffer[65536];
    
    while (s_running && s_connected) {
        PacketHeader header;
        if (!recv_all(s_socket, reinterpret_cast<char*>(&header), sizeof(header))) {
            std::cout << "[Network] Server closed connection." << std::endl;
            s_connected = false;
            break;
        }
        
        if (header.length > sizeof(read_buffer)) {
            std::cerr << "[Network] Payload size too large: " << header.length << std::endl;
            s_connected = false;
            break;
        }
        
        if (header.length > 0) {
            if (!recv_all(s_socket, read_buffer, header.length)) {
                s_connected = false;
                break;
            }
        }
        
        switch (header.type) {
            case MSG_SERVER_JOIN_ACK: {
                MsgServerJoinAck* ack = reinterpret_cast<MsgServerJoinAck*>(read_buffer);
                s_my_player_id = ack->player_id;
                break;
            }
            case MSG_SERVER_STATE: {
                std::lock_guard<std::mutex> lock(s_state_mutex);
                std::memcpy(&s_state, read_buffer, sizeof(GameState));
                break;
            }
            case MSG_SERVER_ALERT: {
                MsgServerAlert* alert = reinterpret_cast<MsgServerAlert*>(read_buffer);
                std::lock_guard<std::mutex> lock(s_alerts_mutex);
                s_alerts.push_back(alert->message);
                break;
            }
            default:
                break;
        }
    }
    
    close_socket(s_socket);
    s_socket = INVALID_SOCKET;
    s_connected = false;
}

bool Network_Connect(const char* ip, int port, const char* name) {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        s_error = "Winsock Init Failed";
        return false;
    }
#endif

    s_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (s_socket == INVALID_SOCKET) {
        s_error = "Socket creation failed";
        return false;
    }

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        s_error = "Invalid IP address";
        close_socket(s_socket);
        return false;
    }

    if (connect(s_socket, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) == SOCKET_ERROR) {
        s_error = "Connection refused";
        close_socket(s_socket);
        return false;
    }

    s_connected = true;
    s_running = true;
    s_my_player_id = 0;
    
    // Clear alerts
    {
        std::lock_guard<std::mutex> lock(s_alerts_mutex);
        s_alerts.clear();
    }
    
    // Send join packet
    MsgClientJoin join_msg;
    std::strncpy(join_msg.name, name, sizeof(join_msg.name) - 1);
    join_msg.name[sizeof(join_msg.name) - 1] = '\0';
    
    if (!send_raw_packet(s_socket, MSG_CLIENT_JOIN, &join_msg, sizeof(join_msg))) {
        s_error = "Failed to send join request";
        close_socket(s_socket);
        s_connected = false;
        return false;
    }

    std::thread receive_thread(network_receive_loop);
    receive_thread.detach();

    return true;
}

void Network_Disconnect() {
    s_running = false;
    s_connected = false;
    if (s_socket != INVALID_SOCKET) {
        close_socket(s_socket);
        s_socket = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

bool Network_SendInput(float dx, float dy) {
    if (!s_connected) return false;
    MsgClientInput msg;
    msg.dx = dx;
    msg.dy = dy;
    return send_raw_packet(s_socket, MSG_CLIENT_INPUT, &msg, sizeof(msg));
}

bool Network_SendReady(bool ready) {
    if (!s_connected) return false;
    MsgClientReady msg;
    msg.is_ready = ready;
    return send_raw_packet(s_socket, MSG_CLIENT_READY, &msg, sizeof(msg));
}

bool Network_SendBuy(uint32_t item_idx) {
    if (!s_connected) return false;
    MsgClientBuy msg;
    msg.item_index = item_idx;
    return send_raw_packet(s_socket, MSG_CLIENT_BUY, &msg, sizeof(msg));
}

bool Network_SendAttack() {
    if (!s_connected) return false;
    return send_raw_packet(s_socket, MSG_CLIENT_ATTACK, nullptr, 0);
}

GameState Network_GetState() {
    std::lock_guard<std::mutex> lock(s_state_mutex);
    return s_state;
}

std::vector<std::string> Network_GetAlerts() {
    std::lock_guard<std::mutex> lock(s_alerts_mutex);
    std::vector<std::string> result = s_alerts;
    s_alerts.clear(); // consume notifications
    return result;
}

bool Network_IsConnected() {
    return s_connected;
}

std::string Network_GetError() {
    return s_error;
}

uint32_t Network_GetMyPlayerId() {
    return s_my_player_id;
}
