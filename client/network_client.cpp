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

static SOCKET net_socket = INVALID_SOCKET;
static bool is_connected = false;
static bool is_running = true;
static uint32_t player_id = 0;

static GameState game_state;
static std::mutex state_mutex;

static std::vector<std::string> alerts;
static std::mutex alerts_mutex;

static std::string error_msg = "";

static bool send_all(SOCKET sock, const char* buffer, int size) {
    int sent = 0;
    while (sent < size) {
        int n = send(sock, buffer + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static bool send_raw_packet(SOCKET sock, uint16_t type, const void* payload, uint32_t payload_len) {
    PacketHeader header;
    header.type = type;
    header.length = payload_len;

    if (!send_all(sock, (const char*)&header, sizeof(header)))
        return false;

    if (payload_len && payload)
        if (!send_all(sock, (const char*)payload, payload_len))
            return false;

    return true;
}

static bool recv_all(SOCKET sock, char* buffer, int size) {
    int read = 0;
    while (read < size) {
        int n = recv(sock, buffer + read, size - read, 0);
        if (n <= 0) return false;
        read += n;
    }
    return true;
}

static void network_receive_loop() {
    char buffer[65536];

    while (is_running && is_connected) {
        PacketHeader header;

        if (!recv_all(net_socket, (char*)&header, sizeof(header))) {
            std::cout << "[Network] Server closed connection.\n";
            is_connected = false;
            break;
        }

        if (header.length > sizeof(buffer)) {
            std::cerr << "[Network] Payload too large\n";
            is_connected = false;
            break;
        }

        if (header.length) {
            if (!recv_all(net_socket, buffer, header.length)) {
                is_connected = false;
                break;
            }
        }

        switch (header.type) {
            case MSG_SERVER_JOIN_ACK: {
                auto* ack = (MsgServerJoinAck*)buffer;
                player_id = ack->player_id;
                break;
            }

            case MSG_SERVER_STATE: {
                std::lock_guard lock(state_mutex);
                std::memcpy(&game_state, buffer, sizeof(GameState));
                break;
            }

            case MSG_SERVER_ALERT: {
                auto* alert = (MsgServerAlert*)buffer;
                std::lock_guard lock(alerts_mutex);
                alerts.push_back(alert->message);
                break;
            }

            default:
                break;
        }
    }

    closesocket(net_socket);
    net_socket = INVALID_SOCKET;
    is_connected = false;
}

bool network_connect(const char* ip, int port, const char* name) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        error_msg = "winsock init failed";
        return false;
    }

    net_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (net_socket == INVALID_SOCKET) {
        error_msg = "socket failed";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        error_msg = "invalid ip";
        closesocket(net_socket);
        return false;
    }

    if (connect(net_socket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        error_msg = "connect failed";
        closesocket(net_socket);
        return false;
    }

    is_connected = true;
    is_running = true;
    player_id = 0;

    {
        std::lock_guard lock(alerts_mutex);
        alerts.clear();
    }

    MsgClientJoin join{};
    std::strncpy(join.name, name, sizeof(join.name) - 1);
    join.name[sizeof(join.name) - 1] = '\0';

    if (!send_raw_packet(net_socket, MSG_CLIENT_JOIN, &join, sizeof(join))) {
        error_msg = "join failed";
        closesocket(net_socket);
        is_connected = false;
        return false;
    }

    std::thread(network_receive_loop).detach();
    return true;
}

void network_disconnect() {
    is_running = false;
    is_connected = false;

    if (net_socket != INVALID_SOCKET) {
        closesocket(net_socket);
        net_socket = INVALID_SOCKET;
    }

    WSACleanup();
}

bool network_send_input(float dx, float dy) {
    if (!is_connected) return false;

    MsgClientInput msg;
    msg.dx = dx;
    msg.dy = dy;

    return send_raw_packet(net_socket, MSG_CLIENT_INPUT, &msg, sizeof(msg));
}

bool network_send_ready(bool is_ready) {
    if (!is_connected) return false;

    MsgClientReady msg;
    msg.is_ready = is_ready;

    return send_raw_packet(net_socket, MSG_CLIENT_READY, &msg, sizeof(msg));
}

bool network_send_buy(uint32_t item_idx) {
    if (!is_connected) return false;

    MsgClientBuy msg;
    msg.item_index = item_idx;

    return send_raw_packet(net_socket, MSG_CLIENT_BUY, &msg, sizeof(msg));
}

GameState network_get_state() {
    std::lock_guard lock(state_mutex);
    return game_state;
}

std::vector<std::string> network_get_alerts() {
    std::lock_guard lock(alerts_mutex);
    auto copy = alerts;
    alerts.clear();
    return copy;
}

bool network_is_connected() {
    return is_connected;
}

std::string network_get_error() {
    return error_msg;
}

uint32_t network_get_player_id() {
    return player_id;
}