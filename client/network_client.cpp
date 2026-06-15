#include "network_client.h"
#include "../common/protocol.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <cstring>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

static SOCKET socket = INVALID_SOCKET;
static bool is_connected = false;
static uint32_t player_id = 0;
static bool is_running = true;
static GameState game_state;
static std::mutex state_mutex;

static std::vector<std::string> alerts;
static std::mutex alerts_mutex;
static std::string error = "";

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
    
    while (is_running && is_connected) {
        PacketHeader header;
        if (!recv_all(socket, reinterpret_cast<char*>(&header), sizeof(header))) {
            std::cout << "[Network] Server closed connection." << std::endl;
            is_connected = false;
            break;
        }
        
        if (header.length > sizeof(read_buffer)) {
            std::cerr << "[Network] Payload size too large: " << header.length << std::endl;
            is_connected = false;
            break;
        }
        
        if (header.length > 0) {
            if (!recv_all(socket, read_buffer, header.length)) {
                is_connected = false;
                break;
            }
        }
        
        switch (header.type) {
            case MSG_SERVER_JOIN_ACK: {
                MsgServerJoinAck* ack = reinterpret_cast<MsgServerJoinAck*>(read_buffer);
                player_id = ack->player_id;
                break;
            }
            case MSG_SERVER_STATE: {
                std::lock_guard<std::mutex> lock(state_mutex);
                std::memcpy(&game_state, read_buffer, sizeof(GameState));
                break;
            }
            case MSG_SERVER_ALERT: {
                MsgServerAlert* alert = reinterpret_cast<MsgServerAlert*>(read_buffer);
                std::lock_guard<std::mutex> lock(alerts_mutex);
                alerts.push_back(alert->message);
                break;
            }
            default:
                break;
        }
    }
    
    closesocket(socket);
    socket = INVALID_SOCKET;
    is_connected = false;
}

bool network_connect(const char* ip, int port, const char* name) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        error = "winsock init failed";
        return false;
    }

    socket = socket(AF_INET, SOCK_STREAM, 0);
    if (socket == INVALID_SOCKET) {
        error = "socket creation failed";
        return false;
    }

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        error = "invalid ip";
        closesocket(socket);
        return false;
    }

    // handshake
    if (connect(socket, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) == SOCKET_ERROR) {
        error = "connection refused";
        closesocket(socket);
        return false;
    }

    is_connected = true;
    is_running = true;
    player_id = 0;
    
    // clear alerts
    {
        std::lock_guard<std::mutex> lock(alerts_mutex);
        alerts.clear();
    }
    
    // send join packet
    MsgClientJoin join_msg;
    std::strncpy(join_msg.name, name, sizeof(join_msg.name) - 1);
    join_msg.name[sizeof(join_msg.name) - 1] = '\0';
    
    if (!send_raw_packet(socket, MSG_CLIENT_JOIN, &join_msg, sizeof(join_msg))) {
        error = "failed to send join request";
        closesocket(socket);
        is_connected = false;
        return false;
    }

    std::thread receive_thread(network_receive_loop);
    receive_thread.detach();

    return true;
}

void network_disconnect() {
    is_running = false;
    is_connected = false;
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }

    WSACleanup();
}

bool network_send_input(float dx, float dy) {
    if (!is_connected) return false;
    MsgClientInput msg;
    msg.dx = dx;
    msg.dy = dy;
    return send_raw_packet(socket, MSG_CLIENT_INPUT, &msg, sizeof(msg));
}

bool network_send_ready(bool ready) {
    if (!is_connected) return false;
    MsgClientReady msg;
    msg.is_ready = ready;
    return send_raw_packet(socket, MSG_CLIENT_READY, &msg, sizeof(msg));
}

bool network_send_buy(uint32_t item_idx) {
    if (!is_connected) return false;
    MsgClientBuy msg;
    msg.item_index = item_idx;
    return send_raw_packet(socket, MSG_CLIENT_BUY, &msg, sizeof(msg));
}

GameState network_get_state() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return game_state;
}

std::vector<std::string> network_get_alerts() {
    std::lock_guard<std::mutex> lock(alerts_mutex);
    std::vector<std::string> result = alerts;
    alerts.clear();
    return result;
}

bool network_is_connected() {
    return is_connected;
}

std::string network_get_error() {
    return error;
}

uint32_t network_get_player_id() {
    return player_id;
}
