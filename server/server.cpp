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

// client info for socket tracking
struct Client {
    SOCKET socket;
    uint32_t player_id;
    bool is_active;
    std::string name;
};

// global game data stuff
GameState game_state;
std::vector<Client> clients;
std::mutex state_mutex;
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
    if (!send_all(sock, reinterpret_cast<const char*>(&header), sizeof(header))) {
        return false;
    }
    // send payload
    if (payload_len > 0 && payload != nullptr) {
        if (!send_all(sock, reinterpret_cast<const char*>(payload), payload_len)) {
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
    std::strncpy(p.name, name, sizeof(p.name) - 1);
    p.name[sizeof(p.name) - 1] = '\0';
    
    // starting positions around the map center
    float angle = (2.0f * 3.14159f / MAX_PLAYERS) * color_idx;
    p.pos.x = CENTER_X + (BASE_ROTATION_RADIUS - 50.0f) * std::cos(angle);
    p.pos.y = CENTER_Y + (BASE_ROTATION_RADIUS - 50.0f) * std::sin(angle);
    p.dir.x = 0;
    p.dir.y = 0;
    
    p.gold_carried = 0;
    p.gold_in_base = 0;
    p.total_gold_all_rounds = 0;
    p.rounds_won = 0;
    
    p.is_speed_upgraded = false;
    p.is_gold_multiplier_active = false;

    p.slow_timer = 0;
    p.stun_timer = 0;

    p.is_ready = false;
    p.is_active = true;
    p.color_index = color_idx;
}

// spawn gold nugget in random place
void spawn_gold() {
    // check how much gold is already on map
    uint32_t active_count = 0;
    for (uint32_t i = 0; i < MAX_GOLD_ITEMS; ++i) {
        if (game_state.gold_items[i].is_active) {
            active_count++;
        }
    }
    
    if (active_count >= MAX_GOLD_ON_MAP) {
        return; // do not spawn if map is full of gold
    }
    
    // look for free slot to put new gold
    for (uint32_t i = 0; i < MAX_GOLD_ITEMS; ++i) {
        if (!game_state.gold_items[i].is_active) {
            game_state.gold_items[i].id = i;
            
            // random angle and distance from middle
            float angle = static_cast<float>(std::rand()) / RAND_MAX * 2.0f * 3.14159f;
            // keep it between mine edge and outer circle
            float dist = MINE_RADIUS + 20.0f + (static_cast<float>(std::rand()) / RAND_MAX * 25.0f);
            
            game_state.gold_items[i].pos.x = CENTER_X + dist * std::cos(angle);
            game_state.gold_items[i].pos.y = CENTER_Y + dist * std::sin(angle);
            
            // clamp to map boundaries so gold is not offscreen
            if (game_state.gold_items[i].pos.x < GOLD_RADIUS + 30.0f) game_state.gold_items[i].pos.x = GOLD_RADIUS + 30.0f;
            if (game_state.gold_items[i].pos.x > MAP_WIDTH - GOLD_RADIUS - 30.0f) game_state.gold_items[i].pos.x = MAP_WIDTH - GOLD_RADIUS - 30.0f;
            if (game_state.gold_items[i].pos.y < GOLD_RADIUS + 30.0f) game_state.gold_items[i].pos.y = GOLD_RADIUS + 30.0f;
            if (game_state.gold_items[i].pos.y > MAP_HEIGHT - GOLD_RADIUS - 30.0f) game_state.gold_items[i].pos.y = MAP_HEIGHT - GOLD_RADIUS - 30.0f;
            
            // gold nugget has random value between 5 and 15
            game_state.gold_items[i].value = 5 + (std::rand() % 11);
            game_state.gold_items[i].is_active = true;
            
            // print gold spawn message
            std::cout << "[Server] A new gold nugget was deposited near (" << game_state.gold_items[i].pos.x << ", " << game_state.gold_items[i].pos.y << ")" << std::endl;
            break;
        }
    }
}

// start a fresh round of gold fever
void reset_round() {
    game_state.round_timer = static_cast<float>(ROUND_DURATION);
    game_state.winner_id = 0;
    
    // clear existing gold items
    for (uint32_t i = 0; i < MAX_GOLD_ITEMS; ++i) {
        game_state.gold_items[i].is_active = false;
    }
    
    // reset positions and carried gold, but keep upgrades
    float divisor = game_state.player_count > 0 ? static_cast<float>(game_state.player_count) : 4.0f;
    for (uint32_t i = 0; i < game_state.player_count; ++i) {
        Player& p = game_state.players[i];
        p.gold_carried = 0;
        p.gold_in_base = 0;
 
        float angle = (2.0f * 3.14159f / divisor) * p.color_index;
        p.pos.x = CENTER_X + (BASE_ROTATION_RADIUS - 50.0f) * std::cos(angle);
        p.pos.y = CENTER_Y + (BASE_ROTATION_RADIUS - 50.0f) * std::sin(angle);
        p.dir.x = 0;
        p.dir.y = 0;
    }
    
    // set up base positions based on active players
    for (uint32_t i = 0; i < game_state.player_count; ++i) {
        Base& b = game_state.bases[i];
        b.owner_id = game_state.players[i].id;
        b.angle = (2.0f * 3.14159f / divisor) * game_state.players[i].color_index;
        b.pos.x = CENTER_X + BASE_ROTATION_RADIUS * std::cos(b.angle);
        b.pos.y = CENTER_Y + BASE_ROTATION_RADIUS * std::sin(b.angle);
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
        if (!recv_all(client_socket, reinterpret_cast<char*>(&header), sizeof(header))) {
            break; // connection dropped
        }
        
        // read message payload
        if (header.length > sizeof(read_buffer)) {
            std::cerr << "[Server] Packet payload size too big: " << header.length << std::endl;
            break;
        }
        
        if (header.length > 0) {
            if (!recv_all(client_socket, read_buffer, header.length)) {
                break;
            }
        }
        
        // lock thread to safely edit shared game state
        std::lock_guard<std::mutex> lock(state_mutex);
        
        // locate this player in our array
        int player_idx = -1;
        for (uint32_t i = 0; i < game_state.player_count; ++i) {
            if (game_state.players[i].id == player_id) {
                player_idx = i;
                break;
            }
        }
        
        if (player_idx == -1) {
            continue; // couldn't find player, skip
        }
        
        Player& player = game_state.players[player_idx];
        
        if (!player.is_active) {
            continue; // skip if player has disconnected
        }
        
        switch (header.type) {
            case MSG_CLIENT_READY: {
                if (game_state.state == 0) { // players can only toggle ready in lobby
                    MsgClientReady* msg = reinterpret_cast<MsgClientReady*>(read_buffer);
                    player.is_ready = msg->is_ready;
                    std::cout << "[Server] Player " << player.name << " is " << (player.is_ready ? "READY" : "NOT READY") << std::endl;
                }
                break;
            }
            case MSG_CLIENT_INPUT: {
                if (player.stun_timer <= 0) {
                    MsgClientInput* msg = reinterpret_cast<MsgClientInput*>(read_buffer);
                    player.dir.x = std::max(-1.0f, std::min(1.0f, msg->dx));
                    player.dir.y = std::max(-1.0f, std::min(1.0f, msg->dy));
                } else {
                    player.dir.x = 0;
                    player.dir.y = 0;
                }
                break;
            }
            case MSG_CLIENT_BUY: {
                MsgClientBuy* msg = reinterpret_cast<MsgClientBuy*>(read_buffer);
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
                        std::cout << "[Server] " << player.name << " bought upgrade " << item << "!" << std::endl;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    
    // handle connection loss cleanup
    std::lock_guard<std::mutex> lock(state_mutex);
    std::cout << "[Server] Player with socket " << client_socket << " disconnected." << std::endl;
    closesocket(client_socket);
    
    for (auto& client : clients) {
        if (client.socket == client_socket) {
            client.is_active = false;
        }
    }
    
    for (uint32_t i = 0; i < game_state.player_count; ++i) {
        if (game_state.players[i].id == player_id) {
            game_state.players[i].is_active = false;
            game_state.players[i].dir.x = 0;
            game_state.players[i].dir.y = 0;
            
            // disable base for disconnected player
            for (uint32_t j = 0; j < game_state.player_count; ++j) {
                if (game_state.bases[j].owner_id == player_id) {
                    game_state.bases[j].is_active = false;
                }
            }
            
            std::cout << "[Server] " << game_state.players[i].name << " left the game." << std::endl;
            break;
        }
    }
}

// server simulation tick loop at 30hz
void game_tick_loop() {
    const float dt = 0.033f; // 30 ticks per second
    auto tick_duration = std::chrono::milliseconds(33);
    
    float gold_spawn_timer = 0.0f;
    float state_timer = 0.0f; // timer for round/game over screen delays
    
    while (is_server_running) {
        auto start_time = std::chrono::steady_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            
            // state machine to handle lobby, playing, round over, game over
            if (game_state.state == 0) { // state: lobby
                // count how many connected guys are ready
                bool is_all_ready = (game_state.player_count > 0);
                for (uint32_t i = 0; i < game_state.player_count; ++i) {
                    if (game_state.players[i].is_active && !game_state.players[i].is_ready) {
                        is_all_ready = false;
                    }
                }
                
                if (is_all_ready) {
                    game_state.state = 1; // all ready, move to playing state
                    game_state.round_number = 1;
                    reset_round();
                    
                    std::cout << "[Server] Round 1 begins!" << std::endl;
                }
                
                // let players move around in lobby for fun
                for (uint32_t i = 0; i < game_state.player_count; ++i) {
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
                
            } else if (game_state.state == 1) { // state: round gameplay
                // count down round duration
                game_state.round_timer -= dt;
                
                // spawn a gold item when timer ticks
                gold_spawn_timer += dt;
                if (gold_spawn_timer >= static_cast<float>(GOLD_SPAWN_INTERVAL)) {
                    gold_spawn_timer = 0.0f;
                    spawn_gold();
                }
                
                // rotate player bases around the center circle
                for (uint32_t i = 0; i < game_state.player_count; ++i) {
                    Base& b = game_state.bases[i];
                    if (!b.is_active) continue;
                    
                    b.angle += BASE_ROTATION_SPEED * dt;
                    if (b.angle > 2.0f * 3.14159f) {
                        b.angle -= 2.0f * 3.14159f;
                    }
                    
                    b.pos.x = CENTER_X + BASE_ROTATION_RADIUS * std::cos(b.angle);
                    b.pos.y = CENTER_Y + BASE_ROTATION_RADIUS * std::sin(b.angle);
                }
                
                // run player physics, timers, interactions
                for (uint32_t i = 0; i < game_state.player_count; ++i) {
                    Player& p = game_state.players[i];
                    if (!p.is_active) continue;
 
                    // figure out how fast player should move
                    float current_speed = p.is_speed_upgraded ? PLAYER_UPGRADED_SPEED : PLAYER_BASE_SPEED;
                    if (p.slow_timer > 0) {
                        current_speed *= BASE_DEFENSE_SLOW_FACTOR;
                    }
 
                    if (p.stun_timer > 0) p.stun_timer -= dt;
                    if (p.slow_timer > 0) p.slow_timer -= dt;
 
                    if (p.stun_timer <= 0) {
                        p.pos.x += p.dir.x * current_speed * dt;
                        p.pos.y += p.dir.y * current_speed * dt;
                    }
 
                    // keep player on screen
                    if (p.pos.x < PLAYER_RADIUS) p.pos.x = PLAYER_RADIUS;
                    if (p.pos.x > MAP_WIDTH - PLAYER_RADIUS) p.pos.x = MAP_WIDTH - PLAYER_RADIUS;
                    if (p.pos.y < PLAYER_RADIUS) p.pos.y = PLAYER_RADIUS;
                    if (p.pos.y > MAP_HEIGHT - PLAYER_RADIUS) p.pos.y = MAP_HEIGHT - PLAYER_RADIUS;
                    
                    // check if player hits central gold mine
                    float dist_to_center = std::sqrt((p.pos.x - CENTER_X)*(p.pos.x - CENTER_X) + 
                                                     (p.pos.y - CENTER_Y)*(p.pos.y - CENTER_Y));
                    if (dist_to_center < MINE_RADIUS + PLAYER_RADIUS) {
                        // don't let players walk inside the mine structure
                        float push_x = (p.pos.x - CENTER_X) / dist_to_center;
                        float push_y = (p.pos.y - CENTER_Y) / dist_to_center;
                        p.pos.x = CENTER_X + (MINE_RADIUS + PLAYER_RADIUS) * push_x;
                        p.pos.y = CENTER_Y + (MINE_RADIUS + PLAYER_RADIUS) * push_y;
                    }
                    
                    // pick up gold nuggets if close enough
                    for (uint32_t j = 0; j < MAX_GOLD_ITEMS; ++j) {
                        GoldItem& gold = game_state.gold_items[j];
                        if (!gold.is_active) continue;
                        
                        float dx = p.pos.x - gold.pos.x;
                        float dy = p.pos.y - gold.pos.y;
                        float dist = std::sqrt(dx*dx + dy*dy);
                        
                        if (dist < PLAYER_RADIUS + GOLD_RADIUS) {
                            // server decides who gets the gold first
                            gold.is_active = false;
                            
                            // check if player has gold multiplier upgrade
                            uint32_t added_gold = gold.value;
                            if (p.is_gold_multiplier_active) {
                                added_gold = static_cast<uint32_t>(added_gold * 1.50f);
                            }
                            p.gold_carried += added_gold;
                            
                            std::cout << "[Server] " << p.name << " picked up " << added_gold << " gold!" << std::endl;
                        }
                    }
                    
                    // check if player deposits gold in their own base
                    for (uint32_t j = 0; j < game_state.player_count; ++j) {
                        Base& b = game_state.bases[j];
                        if (!b.is_active) continue;
                        
                        float dx = p.pos.x - b.pos.x;
                        float dy = p.pos.y - b.pos.y;
                        float dist = std::sqrt(dx*dx + dy*dy);
                        
                        if (dist < PLAYER_RADIUS + BASE_RADIUS) {
                            if (b.owner_id == p.id) {
                                // deposit carried gold to base score
                                if (p.gold_carried > 0) {
                                    p.gold_in_base += p.gold_carried;
                                    
                                    std::cout << "[Server] " << p.name << " deposited " << p.gold_carried << " gold in base!" << std::endl;
                                    
                                    p.gold_carried = 0;
                                }
                            }
                        }
                    }
                }
                
                // push players apart if they overlap
                for (uint32_t i = 0; i < game_state.player_count; ++i) {
                    Player& p1 = game_state.players[i];
                    if (!p1.is_active) continue;
                    
                    for (uint32_t j = i + 1; j < game_state.player_count; ++j) {
                        Player& p2 = game_state.players[j];
                        if (!p2.is_active) continue;
                        
                        float dx = p2.pos.x - p1.pos.x;
                        float dy = p2.pos.y - p1.pos.y;
                        float dist = std::sqrt(dx*dx + dy*dy);
                        
                        if (dist < 2.0f * PLAYER_RADIUS && dist > 0.001f) {
                            float overlap = (2.0f * PLAYER_RADIUS) - dist;
                            // push players back along collision vector
                            float push_x = dx / dist;
                            float push_y = dy / dist;
                            
                            p1.pos.x -= 0.5f * overlap * push_x;
                            p1.pos.y -= 0.5f * overlap * push_y;
                            p2.pos.x += 0.5f * overlap * push_x;
                            p2.pos.y += 0.5f * overlap * push_y;
                        }
                    }
                }
                
                // check if round timer ran out
                if (game_state.round_timer <= 0) {
                    // figure out who won this round
                    uint32_t max_gold = 0;
                    int winner_idx = -1;
                    bool is_tie = false;
                    
                    for (uint32_t i = 0; i < game_state.player_count; ++i) {
                        Player& p = game_state.players[i];
                        if (!p.is_active) continue;
                        
                        p.total_gold_all_rounds += p.gold_in_base; // save score for tiebreakers
                        
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
                        
                        std::cout << "[Server] " << game_state.players[winner_idx].name << " wins Round " << game_state.round_number << " with " << max_gold << " gold!" << std::endl;
                    } else {
                        game_state.winner_id = 0; // Tie or no gold
                        std::cout << "[Server] Round " << game_state.round_number << " ends in a tie!" << std::endl;
                    }
                    
                    // if 3 rounds passed, game is over
                    if (game_state.round_number >= TOTAL_ROUNDS) {
                        game_state.state = 3; // Game Over
                        state_timer = 8.0f;    // stay on game over screen for 8s
                        
                        // check who won most rounds
                        uint32_t max_rounds = 0;
                        uint32_t tiebreaker_gold = 0;
                        int match_winner_idx = -1;
                        bool is_match_tie = false;
                        
                        for (uint32_t i = 0; i < game_state.player_count; ++i) {
                            Player& p = game_state.players[i];
                            if (!p.is_active) continue;
                            
                            if (p.rounds_won > max_rounds) {
                                max_rounds = p.rounds_won;
                                tiebreaker_gold = p.total_gold_all_rounds;
                                match_winner_idx = i;
                                is_match_tie = false;
                            } else if (p.rounds_won == max_rounds) {
                                // total gold collected is the tiebreaker
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
                            game_state.winner_id = 0; // Joint 1st place / Tie
                        }
                    } else {
                        game_state.state = 2; // transition back to playing next round
                        state_timer = 5.0f;    // delay on round scores screen
                    }
                }
                
            } else if (game_state.state == 2) { // state: round ended screen
                state_timer -= dt;
                if (state_timer <= 0) {
                    game_state.round_number++;
                    game_state.state = 1; // transition back to playing next round
                    reset_round();
                    
                    std::cout << "[Server] Round " << game_state.round_number << " begins!" << std::endl;
                }
            } else if (game_state.state == 3) { // state: game over screen
                state_timer -= dt;
                if (state_timer <= 0) {
                    // Reset everything to Lobby
                    game_state.state = 0;
                    game_state.round_number = 0;
                    game_state.player_count = 0;
                    game_state.gold_count = 0;
                    clients.clear();
                    std::cout << "[Server] Game reset to Lobby." << std::endl;
                }
            }
            
            // sync everyone with latest state
            broadcast_packet(MSG_SERVER_STATE, &game_state, sizeof(game_state));
        }
        
        // sleep to cap tick rate at 30hz
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed < tick_duration) {
            std::this_thread::sleep_for(tick_duration - elapsed);
        }
    }
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[Server] Failed to initialize Winsock." << std::endl;
        return 1;
    }

    SOCKET listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == INVALID_SOCKET) {
        std::cerr << "[Server] Socket creation failed: " << WSAGetLastError() << std::endl;
        return 1;
    }
    
    // avoid port in use errors
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(5000);

    if (bind(listenfd, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) == SOCKET_ERROR) {
        std::cerr << "[Server] Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(listenfd);
        return 1;
    }

    if (listen(listenfd, 10) == SOCKET_ERROR) {
        std::cerr << "[Server] Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(listenfd);
        return 1;
    }

    std::cout << "[Server] Gold Fever Server running on port 5000..." << std::endl;
    
    // set up initial server variables
    game_state.state = 0; // state: lobby
    game_state.round_number = 0;
    game_state.player_count = 0;
    game_state.gold_count = 0;
    
    // start physics simulation thread
    std::thread tick_thread(game_tick_loop);
    tick_thread.detach();

    uint32_t next_player_id = 1;

    for (;;) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET connfd = accept(listenfd, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
        
        if (connfd == INVALID_SOCKET) {
            std::cerr << "[Server] Accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        std::cout << "[Server] New client connection accepted." << std::endl;
        
        // read incoming client join payload
        PacketHeader header;
        if (!recv_all(connfd, reinterpret_cast<char*>(&header), sizeof(header))) {
            closesocket(connfd);
            continue;
        }
        
        if (header.type != MSG_CLIENT_JOIN || header.length > 128) {
            closesocket(connfd);
            continue;
        }
        
        std::vector<char> join_payload(header.length);
        if (!recv_all(connfd, join_payload.data(), header.length)) {
            closesocket(connfd);
            continue;
        }
        
        MsgClientJoin* join_msg = reinterpret_cast<MsgClientJoin*>(join_payload.data());
        
        std::lock_guard<std::mutex> lock(state_mutex);
        
        if (game_state.state != 0 || game_state.player_count >= MAX_PLAYERS) {
            std::cout << "[Server] Rejecting connection: Game already in progress or server full." << std::endl;
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
        
        std::cout << "[Server] Player '" << client.name << "' assigned ID " << player_id << std::endl;
        
        std::cout << "[Server] " << client.name << " joined the lobby!" << std::endl;
        
        // spawn a thread for this player's socket
        std::thread client_thread(client_handler, connfd, player_id);
        client_thread.detach();
    }

    is_server_running = false;
    closesocket(listenfd);

    WSACleanup();
    return 0;
}
