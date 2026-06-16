#define NOMINMAX
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <string>
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "../common/game_types.h"
#include "../common/protocol.h"

using namespace std;

// client info for socket tracking
struct Client {
    SOCKET socket;
    uint32_t player_id;
    bool is_active;
    string name;
};

// global game data stuff
GameState game_state;
vector<Client> clients;
mutex state_mutex;
bool is_server_running = true;

// helper to send all bytes since tcp might send in chunks
static bool send_all(SOCKET sock, const char* buffer, int size) {
    int bytes_sent = 0;
    while (bytes_sent < size) {
        int n = send(sock, buffer + bytes_sent, size - bytes_sent, 0);
        if (n <= 0) return false;
        bytes_sent += n;
    }
    return true;
}

// send packet directly, no vector buffers or allocations
bool send_packet(SOCKET sock, uint16_t type, const void* payload, uint32_t payload_len) {
    PacketHeader header;
    header.type = type;
    header.length = payload_len;

    // send header
    if (!send_all(sock, (const char*)&header, sizeof(header))) {
        return false;
    }
    // send payload
    if (payload_len > 0 && payload != nullptr) {
        if (!send_all(sock, (const char*)payload, payload_len)) {
            return false;
        }
    }
    return true;
}

// broadcast packet to all playing clients
void broadcast_packet(uint16_t type, const void* payload, uint32_t payload_len) {
    for (auto& client : clients) {
        if (client.is_active) {
            send_packet(client.socket, type, payload, payload_len);
        }
    }
}

// read exactly size bytes from socket
bool recv_all(SOCKET sock, char* buffer, int size) {
    int bytes_read = 0;
    while (bytes_read < size) {
        int n = recv(sock, buffer + bytes_read, size - bytes_read, 0);
        if (n <= 0) {
            return false;
        }
        bytes_read += n;
    }
    return true;
}

// set up new player properties
void init_player_state(Player& p, uint32_t id, const char* name, uint32_t color_idx) {
    p.id = id;
    strncpy(p.name, name, sizeof(p.name) - 1);
    p.name[sizeof(p.name) - 1] = '\0';
    
    // starting positions around the map center
    float angle = (2.0f * 3.14159f / MAX_PLAYERS) * color_idx;
    p.pos.x = CENTER_X + (BASE_ROTATION_RADIUS - 50.0f) * cos(angle);
    p.pos.y = CENTER_Y + (BASE_ROTATION_RADIUS - 50.0f) * sin(angle);
    p.dir.x = 0;
    p.dir.y = 0;
    
    p.gold_carried = 0;
    p.gold_in_base = 0;
    p.total_gold_all_rounds = 0;
    p.rounds_won = 0;
    
    p.is_speed_upgraded = false;
    p.is_gold_multiplier_active = false;

    p.is_ready = false;
    p.is_active = true;
    p.color_index = color_idx;
}

// spawn gold nugget in random place
void spawn_gold() {
    // check how much gold is already on map
    uint32_t active_count = 0;
    for (int i = 0; i < MAX_GOLD_ITEMS; ++i) {
        if (game_state.gold_items[i].is_active) {
            active_count++;
        }
    }
    
    if (active_count >= MAX_GOLD_ON_MAP) {
        return; // do not spawn if map is full of gold
    }
    
    // look for free slot to put new gold
    for (int i = 0; i < MAX_GOLD_ITEMS; ++i) {
        if (!game_state.gold_items[i].is_active) {
            game_state.gold_items[i].id = i;
            
            // random angle and distance from middle
            float angle = (float)rand() / RAND_MAX * 2.0f * 3.14159f;
            // keep it between mine edge and outer circle, safe from player spawns
            float dist = MINE_RADIUS + 20.0f + ((float)rand() / RAND_MAX * 80.0f);
            
            game_state.gold_items[i].pos.x = CENTER_X + dist * cos(angle);
            game_state.gold_items[i].pos.y = CENTER_Y + dist * sin(angle);
            
            // clamp to map boundaries so gold is not offscreen
            if (game_state.gold_items[i].pos.x < GOLD_RADIUS + 30.0f) game_state.gold_items[i].pos.x = GOLD_RADIUS + 30.0f;
            if (game_state.gold_items[i].pos.x > MAP_WIDTH - GOLD_RADIUS - 30.0f) game_state.gold_items[i].pos.x = MAP_WIDTH - GOLD_RADIUS - 30.0f;
            if (game_state.gold_items[i].pos.y < GOLD_RADIUS + 30.0f) game_state.gold_items[i].pos.y = GOLD_RADIUS + 30.0f;
            if (game_state.gold_items[i].pos.y > MAP_HEIGHT - GOLD_RADIUS - 30.0f) game_state.gold_items[i].pos.y = MAP_HEIGHT - GOLD_RADIUS - 30.0f;
            
            // gold nugget has random value between 5 and 15
            game_state.gold_items[i].value = 5 + (rand() % 11);
            game_state.gold_items[i].is_active = true;
            
            // print gold spawn message
            cout << "[Server] A new gold nugget was deposited near (" << game_state.gold_items[i].pos.x << ", " << game_state.gold_items[i].pos.y << ")" << endl;
            break;
        }
    }
}

// start a fresh round of gold fever
void reset_round() {
    game_state.round_timer = (float)ROUND_DURATION;
    game_state.winner_id = 0;
    
    // clear existing gold items
    for (int i = 0; i < MAX_GOLD_ITEMS; ++i) {
        game_state.gold_items[i].is_active = false;
    }
    
    // reset positions and carried gold, but keep upgrades
    float divisor = game_state.player_count > 0 ? (float)game_state.player_count : 4.0f;
    for (int i = 0; i < (int)game_state.player_count; ++i) {
        Player& p = game_state.players[i];
        p.gold_carried = 0;
        p.gold_in_base = 0;
 
        float angle = (2.0f * 3.14159f / divisor) * p.color_index;
        p.pos.x = CENTER_X + (BASE_ROTATION_RADIUS - 50.0f) * cos(angle);
        p.pos.y = CENTER_Y + (BASE_ROTATION_RADIUS - 50.0f) * sin(angle);
        p.dir.x = 0;
        p.dir.y = 0;
    }
    
    // set up base positions based on active players
    for (int i = 0; i < (int)game_state.player_count; ++i) {
        Base& b = game_state.bases[i];
        b.owner_id = game_state.players[i].id;
        b.angle = (2.0f * 3.14159f / divisor) * game_state.players[i].color_index;
        b.pos.x = CENTER_X + BASE_ROTATION_RADIUS * cos(b.angle);
        b.pos.y = CENTER_Y + BASE_ROTATION_RADIUS * sin(b.angle);
        b.is_active = game_state.players[i].is_active;
    }
    
    // spawn a couple of initial gold nuggets
    spawn_gold();
}

// main thread loop for each connected client
void client_handler(SOCKET client_socket, uint32_t player_id) {
    char read_buffer[4096];
    
    while (is_server_running) {
        // read message type and length
        PacketHeader header;
        if (!recv_all(client_socket, (char*)&header, sizeof(header))) {
            break; // connection dropped
        }
        
        // read message payload
        if (header.length > sizeof(read_buffer)) {
            cerr << "[Server] Packet payload size too big: " << header.length << endl;
            break;
        }
        
        if (header.length > 0) {
            if (!recv_all(client_socket, read_buffer, header.length)) {
                break;
            }
        }

        lock_guard<mutex> lock(state_mutex);

        int player_idx = -1;
        for (int i = 0; i < (int)game_state.player_count; ++i) {
            if (game_state.players[i].id == player_id) {
                player_idx = i;
                break;
            }
        }
        
        if (player_idx == -1) {
            continue;
        }
        
        Player& player = game_state.players[player_idx];
        
        if (!player.is_active) {
            continue;
        }
        
        switch (header.type) {
            case MSG_CLIENT_READY: {
                if (game_state.state == 0) {
                    MsgClientReady* msg = (MsgClientReady*)read_buffer;
                    player.is_ready = msg->is_ready;
                    cout << "[Server] Player " << player.name << " is " << (player.is_ready ? "READY" : "NOT READY") << endl;
                }
                break;
            }
            case MSG_CLIENT_INPUT: {
                MsgClientInput* msg = (MsgClientInput*)read_buffer;
                player.dir.x = max(-1.0f, min(1.0f, msg->dx));
                player.dir.y = max(-1.0f, min(1.0f, msg->dy));
                break;
            }
            case MSG_CLIENT_BUY: {
                MsgClientBuy* msg = (MsgClientBuy*)read_buffer;
                uint32_t item = msg->item_index;
                
                // calculate shop purchase costs
                uint32_t cost = 999;
                if (item == 0) cost = COST_SPEED_BOOST;
                else if (item == 1) cost = COST_GOLD_MULTIPLIER;
                
                if (player.gold_in_base >= cost) {
                    bool is_bought = false;
                    if (item == 0 && !player.is_speed_upgraded) {
                        player.is_speed_upgraded = true;
                        is_bought = true;
                    } else if (item == 1 && !player.is_gold_multiplier_active) {
                        player.is_gold_multiplier_active = true;
                        is_bought = true;
                    }
                    if (is_bought) {
                        player.gold_in_base -= cost;
                        cout << "[Server] " << player.name << " bought upgrade " << item << "!" << endl;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    
    // handle connection loss cleanup
    lock_guard<mutex> lock(state_mutex);
    cout << "[Server] Player with socket " << client_socket << " disconnected." << endl;
    closesocket(client_socket);
    
    for (auto& client : clients) {
        if (client.socket == client_socket) {
            client.is_active = false;
        }
    }
    
    for (int i = 0; i < (int)game_state.player_count; ++i) {
        if (game_state.players[i].id == player_id) {
            game_state.players[i].is_active = false;
            game_state.players[i].dir.x = 0;
            game_state.players[i].dir.y = 0;
            
            // disable base for disconnected player
            for (int j = 0; j < (int)game_state.player_count; ++j) {
                if (game_state.bases[j].owner_id == player_id) {
                    game_state.bases[j].is_active = false;
                }
            }
            
            cout << "[Server] " << game_state.players[i].name << " left the game." << endl;
            break;
        }
    }
}

// update lobby state loop
void update_lobby(float dt) {
    // count how many connected players are ready
    bool is_all_ready = (game_state.player_count > 0);
    for (int i = 0; i < (int)game_state.player_count; ++i) {
        if (game_state.players[i].is_active && !game_state.players[i].is_ready) {
            is_all_ready = false;
        }
    }
    
    if (is_all_ready) {
        game_state.state = 1; // all ready, move to playing state
        game_state.round_number = 1;
        reset_round();
        
        cout << "[Server] Round 1 begins!" << endl;
    }
    
    // let players move around in lobby for fun
    for (int i = 0; i < (int)game_state.player_count; ++i) {
        Player& p = game_state.players[i];
        if (!p.is_active) continue;
        
        p.pos.x += p.dir.x * PLAYER_BASE_SPEED * dt;
        p.pos.y += p.dir.y * PLAYER_BASE_SPEED * dt;
        
        // keep player inside screen
        if (p.pos.x < PLAYER_RADIUS) p.pos.x = PLAYER_RADIUS;
        if (p.pos.x > MAP_WIDTH - PLAYER_RADIUS) p.pos.x = MAP_WIDTH - PLAYER_RADIUS;
        if (p.pos.y < PLAYER_RADIUS) p.pos.y = PLAYER_RADIUS;
        if (p.pos.y > MAP_HEIGHT - PLAYER_RADIUS) p.pos.y = MAP_HEIGHT - PLAYER_RADIUS;
    }
}

// prevent player from walking into miner
void check_mine_collisions(Player& p) {
    float dist_to_center = sqrt((p.pos.x - CENTER_X)*(p.pos.x - CENTER_X) + 
                                (p.pos.y - CENTER_Y)*(p.pos.y - CENTER_Y));
    if (dist_to_center < MINE_RADIUS + PLAYER_RADIUS) {
        float push_x = (p.pos.x - CENTER_X) / dist_to_center;
        float push_y = (p.pos.y - CENTER_Y) / dist_to_center;
        p.pos.x = CENTER_X + (MINE_RADIUS + PLAYER_RADIUS) * push_x;
        p.pos.y = CENTER_Y + (MINE_RADIUS + PLAYER_RADIUS) * push_y;
    }
}

// check if player picked up gold
void check_gold_pickups(Player& p) {
    for (int j = 0; j < MAX_GOLD_ITEMS; ++j) {
        GoldItem& gold = game_state.gold_items[j];
        if (!gold.is_active) continue;
        
        float dx = p.pos.x - gold.pos.x;
        float dy = p.pos.y - gold.pos.y;
        float dist = sqrt(dx*dx + dy*dy);
        
        if (dist < PLAYER_RADIUS + GOLD_RADIUS) {
            gold.is_active = false;
            uint32_t added_gold = gold.value;
            if (p.is_gold_multiplier_active) {
                added_gold = (uint32_t)(added_gold * 1.50f);
            }
            p.gold_carried += added_gold;
            cout << "[Server] " << p.name << " picked up " << added_gold << " gold!" << endl;
        }
    }
}

// deposit player gold inside their base
void check_base_deposits(Player& p) {
    for (int j = 0; j < (int)game_state.player_count; ++j) {
        Base& b = game_state.bases[j];
        if (!b.is_active) continue;
        
        float dx = p.pos.x - b.pos.x;
        float dy = p.pos.y - b.pos.y;
        float dist = sqrt(dx*dx + dy*dy);
        
        if (dist < PLAYER_RADIUS + BASE_RADIUS) {
            if (b.owner_id == p.id) {
                if (p.gold_carried > 0) {
                    p.gold_in_base += p.gold_carried;
                    cout << "[Server] " << p.name << " deposited " << p.gold_carried << " gold in base!" << endl;
                    p.gold_carried = 0;
                }
            }
        }
    }
}

// prevent players from overlapping
void resolve_player_collisions() {
    for (int i = 0; i < (int)game_state.player_count; ++i) {
        Player& p1 = game_state.players[i];
        if (!p1.is_active) continue;
        
        for (int j = i + 1; j < (int)game_state.player_count; ++j) {
            Player& p2 = game_state.players[j];
            if (!p2.is_active) continue;
            
            float dx = p2.pos.x - p1.pos.x;
            float dy = p2.pos.y - p1.pos.y;
            float dist = sqrt(dx*dx + dy*dy);
            
            if (dist < 2.0f * PLAYER_RADIUS && dist > 0.001f) {
                float overlap = (2.0f * PLAYER_RADIUS) - dist;
                float push_x = dx / dist;
                float push_y = dy / dist;
                
                p1.pos.x -= 0.5f * overlap * push_x;
                p1.pos.y -= 0.5f * overlap * push_y;
                p2.pos.x += 0.5f * overlap * push_x;
                p2.pos.y += 0.5f * overlap * push_y;
            }
        }
    }
}

void evaluate_round_end(float& state_timer) {
    if (game_state.round_timer <= 0) {
        uint32_t max_gold = 0;
        int winner_idx = -1;
        bool is_tie = false;
        
        for (int i = 0; i < (int)game_state.player_count; ++i) {
            Player& p = game_state.players[i];
            if (!p.is_active) continue;
            
            p.total_gold_all_rounds += p.gold_in_base;
            
            if (p.gold_in_base > max_gold) {
                max_gold = p.gold_in_base;
                winner_idx = i;
                is_tie = false;
            } else if (p.gold_in_base == max_gold && max_gold > 0) {
                is_tie = true;
            }
        }
        
        if (winner_idx != -1 && !is_tie) {
            game_state.winner_id = game_state.players[winner_idx].id;
            game_state.players[winner_idx].rounds_won++;
            cout << "[Server] " << game_state.players[winner_idx].name << " wins Round " << game_state.round_number << " with " << max_gold << " gold!" << endl;
        } else {
            game_state.winner_id = 0;
            cout << "[Server] Round " << game_state.round_number << " ends in a tie!" << endl;
        }
        
        if (game_state.round_number >= TOTAL_ROUNDS) {
            game_state.state = 3;
            state_timer = 8.0f;
            
            uint32_t max_rounds = 0;
            uint32_t tiebreaker_gold = 0;
            int match_winner_idx = -1;
            bool is_match_tie = false;
            
            for (int i = 0; i < (int)game_state.player_count; ++i) {
                Player& p = game_state.players[i];
                if (!p.is_active) continue;
                
                if (p.rounds_won > max_rounds) {
                    max_rounds = p.rounds_won;
                    tiebreaker_gold = p.total_gold_all_rounds;
                    match_winner_idx = i;
                    is_match_tie = false;
                } else if (p.rounds_won == max_rounds) {
                    if (p.total_gold_all_rounds > tiebreaker_gold) {
                        tiebreaker_gold = p.total_gold_all_rounds;
                        match_winner_idx = i;
                        is_match_tie = false;
                    } else if (p.total_gold_all_rounds == tiebreaker_gold) {
                        is_match_tie = true;
                    }
                }
            }
            
            if (match_winner_idx != -1 && !is_match_tie) {
                game_state.winner_id = game_state.players[match_winner_idx].id;
            } else {
                game_state.winner_id = 0;
            }
        } else {
            game_state.state = 2;
            state_timer = 5.0f;
        }
    }
}

void update_playing(float dt, float& state_timer, float& gold_spawn_timer) {
    game_state.round_timer -= dt;
    
    gold_spawn_timer += dt;
    if (gold_spawn_timer >= (float)GOLD_SPAWN_INTERVAL) {
        gold_spawn_timer = 0.0f;
        spawn_gold();
    }
    
    for (int i = 0; i < (int)game_state.player_count; ++i) {
        Base& b = game_state.bases[i];
        if (!b.is_active) continue;
        
        b.angle += BASE_ROTATION_SPEED * dt;
        if (b.angle > 2.0f * 3.14159f) {
            b.angle -= 2.0f * 3.14159f;
        }
        
        b.pos.x = CENTER_X + BASE_ROTATION_RADIUS * cos(b.angle);
        b.pos.y = CENTER_Y + BASE_ROTATION_RADIUS * sin(b.angle);
    }
    
    for (int i = 0; i < (int)game_state.player_count; ++i) {
        Player& p = game_state.players[i];
        if (!p.is_active) continue;
 
        float current_speed = p.is_speed_upgraded ? PLAYER_UPGRADED_SPEED : PLAYER_BASE_SPEED;
        p.pos.x += p.dir.x * current_speed * dt;
        p.pos.y += p.dir.y * current_speed * dt;
 
        if (p.pos.x < PLAYER_RADIUS) p.pos.x = PLAYER_RADIUS;
        if (p.pos.x > MAP_WIDTH - PLAYER_RADIUS) p.pos.x = MAP_WIDTH - PLAYER_RADIUS;
        if (p.pos.y < PLAYER_RADIUS) p.pos.y = PLAYER_RADIUS;
        if (p.pos.y > MAP_HEIGHT - PLAYER_RADIUS) p.pos.y = MAP_HEIGHT - PLAYER_RADIUS;
        
        check_mine_collisions(p);
        check_gold_pickups(p);
        check_base_deposits(p);
    }
    
    resolve_player_collisions();
    evaluate_round_end(state_timer);
}

void update_round_end_screen(float dt, float& state_timer) {
    state_timer -= dt;
    if (state_timer <= 0) {
        game_state.round_number++;
        game_state.state = 1;
        reset_round();
        cout << "[Server] Round " << game_state.round_number << " begins!" << endl;
    }
}

void update_game_over_screen(float dt, float& state_timer) {
    state_timer -= dt;
    if (state_timer <= 0) {
        game_state.state = 0;
        game_state.round_number = 0;
        game_state.player_count = 0;
        game_state.gold_count = 0;
        clients.clear();
        cout << "[Server] Game reset to Lobby." << endl;
    }
}

void game_tick_loop() {
    const float dt = 0.033f; // 30 ticks per second
    auto tick_duration = chrono::milliseconds(33);
    
    float gold_spawn_timer = 0.0f;
    float state_timer = 0.0f; // timer for round
    
    while (is_server_running) {
        auto start_time = chrono::steady_clock::now();
        
        {
            lock_guard<mutex> lock(state_mutex);
            
            // state machine to handle lobby, playing, round over, game over
            if (game_state.state == 0) {
                update_lobby(dt);
            } else if (game_state.state == 1) {
                update_playing(dt, state_timer, gold_spawn_timer);
            } else if (game_state.state == 2) {
                update_round_end_screen(dt, state_timer);
            } else if (game_state.state == 3) {
                update_game_over_screen(dt, state_timer);
            }
            
            // sync everyone with latest state
            broadcast_packet(MSG_SERVER_STATE, &game_state, sizeof(game_state));
        }

        auto elapsed = chrono::steady_clock::now() - start_time;
        if (elapsed < tick_duration) {
            this_thread::sleep_for(tick_duration - elapsed);
        }
    }
}

int main() {
    srand((unsigned)time(nullptr));
    
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        cerr << "[Server] Failed to initialize Winsock." << endl;
        return 1;
    }

    SOCKET listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == INVALID_SOCKET) {
        cerr << "[Server] Socket creation failed: " << WSAGetLastError() << endl;
        return 1;
    }
    
    // avoid port in use errors
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(5000);

    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        cerr << "[Server] Bind failed: " << WSAGetLastError() << endl;
        closesocket(listenfd);
        return 1;
    }

    if (listen(listenfd, 10) == SOCKET_ERROR) {
        cerr << "[Server] Listen failed: " << WSAGetLastError() << endl;
        closesocket(listenfd);
        return 1;
    }

    cout << "[Server] Gold Fever Server running on port 5000..." << endl;
    
    // set up initial server variables
    game_state.state = 0; // state: lobby
    game_state.round_number = 0;
    game_state.player_count = 0;
    game_state.gold_count = 0;
    
    // start physics simulation thread
    thread tick_thread(game_tick_loop);
    tick_thread.detach();

    uint32_t next_player_id = 1;

    while (true) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET connfd = accept(listenfd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (connfd == INVALID_SOCKET) {
            cerr << "[Server] Accept failed: " << WSAGetLastError() << endl;
            continue;
        }

        cout << "[Server] New client connection accepted." << endl;
        
        // read incoming client join payload
        PacketHeader header;
        if (!recv_all(connfd, (char*)&header, sizeof(header))) {
            closesocket(connfd);
            continue;
        }
        
        if (header.type != MSG_CLIENT_JOIN || header.length > 128) {
            closesocket(connfd);
            continue;
        }
        
        vector<char> join_payload(header.length);
        if (!recv_all(connfd, join_payload.data(), header.length)) {
            closesocket(connfd);
            continue;
        }
        
        MsgClientJoin* join_msg = (MsgClientJoin*)join_payload.data();
        
        lock_guard<mutex> lock(state_mutex);
        
        if (game_state.state != 0 || game_state.player_count >= MAX_PLAYERS) {
            cout << "[Server] Rejecting connection: Game already in progress or server full." << endl;
            closesocket(connfd);
            continue;
        }
        
        uint32_t player_id = next_player_id++;
        uint32_t color_idx = game_state.player_count;
        
        Client client;
        client.socket = connfd;
        client.player_id = player_id;
        client.is_active = true;
        client.name = join_msg->name;
        clients.push_back(client);
        
        // add new player slot
        Player& new_player = game_state.players[game_state.player_count];
        init_player_state(new_player, player_id, client.name.c_str(), color_idx);
        game_state.player_count++;
        
        // send join ok to client
        MsgServerJoinAck ack;
        ack.player_id = player_id;
        send_packet(connfd, MSG_SERVER_JOIN_ACK, &ack, sizeof(ack));
        
        cout << "[Server] Player '" << client.name << "' assigned ID " << player_id << endl;
        
        cout << "[Server] " << client.name << " joined the lobby!" << endl;
        
        // spawn a thread for this player's socket
        thread client_thread(client_handler, connfd, player_id);
        client_thread.detach();
    }

    is_server_running = false;
    closesocket(listenfd);

    WSACleanup();
    return 0;
}
