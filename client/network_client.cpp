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

using namespace std;

static SOCKET net_socket = INVALID_SOCKET;
static bool is_connected = false;
static uint32_t player_id = 0;
static bool is_running = true;
static GameState game_state;
static mutex state_mutex;

static vector<string> alerts;
static mutex alerts_mutex;
static string error_msg = "";

// helper to send all bytes
static bool send_all(SOCKET sock, const char* buffer, int size) {
    int bytes_sent = 0;
    while (bytes_sent < size) {
        int n = send(sock, buffer + bytes_sent, size - bytes_sent, 0);
        if (n <= 0) return false;
        bytes_sent += n;
    }
    return true;
}

static bool send_raw_packet(SOCKET sock, uint16_t type, const void* payload, uint32_t payload_len) {
    PacketHeader header;
    header.type = type;
    header.length = payload_len;

    if (!send_all(sock, (const char*)&header, sizeof(header))) {
        return false;
    }

    if (payload_len > 0 && payload != nullptr) {
        if (!send_all(sock, (const char*)payload, payload_len)) {
            return false;
        }
    }
    return true;
}

// read exact amount of bytes from socket
static bool recv_all(SOCKET sock, char* buffer, int size) {
    int bytes_read = 0;
    while (bytes_read < size) {
        int n = recv(sock, buffer + bytes_read, size - bytes_read, 0);
        if (n <= 0) return false;
        bytes_read += n;
    }
    return true;
}

// loop to receive packets
static void network_receive_loop() {
    char read_buffer[65536];
    
    while (is_running && is_connected) {
        PacketHeader header;
        if (!recv_all(net_socket, (char*)&header, sizeof(header))) {
            cout << "[Network] Server closed connection." << endl;
            is_connected = false;
            break;
        }
        
        if (header.length > sizeof(read_buffer)) {
            cerr << "[Network] Payload size too large: " << header.length << endl;
            is_connected = false;
            break;
        }
        
        if (header.length > 0) {
            if (!recv_all(net_socket, read_buffer, header.length)) {
                is_connected = false;
                break;
            }
        }
        
        switch (header.type) {
            case MSG_SERVER_JOIN_ACK: {
                MsgServerJoinAck* ack = (MsgServerJoinAck*)read_buffer;
                player_id = ack->player_id;
                break;
            }
            case MSG_SERVER_STATE: {
                lock_guard<mutex> lock(state_mutex);
                memcpy(&game_state, read_buffer, sizeof(GameState));
                break;
            }
            case MSG_SERVER_ALERT: {
                MsgServerAlert* alert = (MsgServerAlert*)read_buffer;
                lock_guard<mutex> lock(alerts_mutex);
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

// connect to game server
bool network_connect(const char* ip, int port, const char* name) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        error_msg = "winsock init failed";
        return false;
    }

    net_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (net_socket == INVALID_SOCKET) {
        error_msg = "socket creation failed";
        return false;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        error_msg = "invalid ip";
        closesocket(net_socket);
        return false;
    }

    if (connect(net_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        error_msg = "connection refused";
        closesocket(net_socket);
        return false;
    }

    is_connected = true;
    is_running = true;
    player_id = 0;
    
    {
        lock_guard<mutex> lock(alerts_mutex);
        alerts.clear();
    }
    
    // send join packet with player name
    MsgClientJoin join_msg;
    strncpy(join_msg.name, name, sizeof(join_msg.name) - 1);
    join_msg.name[sizeof(join_msg.name) - 1] = '\0';
    
    if (!send_raw_packet(net_socket, MSG_CLIENT_JOIN, &join_msg, sizeof(join_msg))) {
        error_msg = "failed to send join request";
        closesocket(net_socket);
        is_connected = false;
        return false;
    }

    thread receive_thread(network_receive_loop);
    receive_thread.detach();

    return true;
}

// close socket and cleanup winsock
void network_disconnect() {
    is_running = false;
    is_connected = false;

    if (net_socket != INVALID_SOCKET) {
        closesocket(net_socket);
        net_socket = INVALID_SOCKET;
    }

    WSACleanup();
}

// send move direction input
bool network_send_input(float dx, float dy) {
    if (!is_connected) return false;
    MsgClientInput msg;
    msg.dx = dx;
    msg.dy = dy;
    return send_raw_packet(net_socket, MSG_CLIENT_INPUT, &msg, sizeof(msg));
}

// send lobby ready status
bool network_send_ready(bool is_ready) {
    if (!is_connected) return false;

    MsgClientReady msg;
    msg.is_ready = is_ready;
    return send_raw_packet(net_socket, MSG_CLIENT_READY, &msg, sizeof(msg));
}

// send buy upgrade request
bool network_send_buy(uint32_t item_idx) {
    if (!is_connected) return false;

    MsgClientBuy msg;
    msg.item_index = item_idx;
    return send_raw_packet(net_socket, MSG_CLIENT_BUY, &msg, sizeof(msg));
}

// get last game state copy safely
GameState network_get_state() {
    lock_guard<mutex> lock(state_mutex);
    return game_state;
}

// get copy of new alert messages
vector<string> network_get_alerts() {
    lock_guard<mutex> lock(alerts_mutex);
    vector<string> result = alerts;
    alerts.clear();
    return result;
}

bool network_is_connected() {
    return is_connected;
}

string network_get_error() {
    return error_msg;
}

uint32_t network_get_player_id() {
    return player_id;
}