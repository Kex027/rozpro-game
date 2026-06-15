#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include "../common/game_types.h"
#include <string>
#include <vector>

bool Network_Connect(const char* ip, int port, const char* name);
void Network_Disconnect();
bool Network_SendInput(float dx, float dy);
bool Network_SendReady(bool ready);
bool Network_SendBuy(uint32_t item_idx);
bool Network_SendAttack();
GameState Network_GetState();
std::vector<std::string> Network_GetAlerts();
bool Network_IsConnected();
std::string Network_GetError();
uint32_t Network_GetMyPlayerId();

#endif // NETWORK_CLIENT_H
