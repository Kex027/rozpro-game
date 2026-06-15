#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include "../common/game_types.h"
#include <string>
#include <vector>

bool network_connect(const char* ip, int port, const char* name);
void network_disconnect();
bool network_send_user_position(float dx, float dy);
bool network_send_ready(bool ready);
bool network_send_buy(uint32_t item_idx);
GameState network_get_state();
std::vector<std::string> network_get_alerts();
bool network_is_connected();
std::string network_get_error();
uint32_t network_get_player_id();

#endif // NETWORK_CLIENT_H
