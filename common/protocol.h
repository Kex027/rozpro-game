#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "game_types.h"

#pragma pack(push, 1)

// Packet framing header
struct PacketHeader {
    uint16_t type;    // PacketType
    uint32_t length;  // Length of the payload following this header
};

// Client to Server packets
enum PacketType : uint16_t {
    // Client -> Server
    MSG_CLIENT_JOIN = 101,     // Payload: MsgClientJoin
    MSG_CLIENT_READY = 102,    // Payload: MsgClientReady
    MSG_CLIENT_INPUT = 103,    // Payload: MsgClientInput
    MSG_CLIENT_BUY = 104,      // Payload: MsgClientBuy
    MSG_CLIENT_ATTACK = 105,   // Payload: none (attacks nearest player in range)
    
    // Server -> Client
    MSG_SERVER_JOIN_ACK = 201, // Payload: MsgServerJoinAck
    MSG_SERVER_STATE = 202,    // Payload: GameState
    MSG_SERVER_ALERT = 203     // Payload: MsgServerAlert (text message)
};

struct MsgClientJoin {
    char name[32];
};

struct MsgClientReady {
    bool is_ready;
};

struct MsgClientInput {
    float dx; // X direction (-1 to 1)
    float dy; // Y direction (-1 to 1)
};

struct MsgClientBuy {
    uint32_t item_index; // 0: speed, 1: gold mult, 2: base defense, 3: weapon, 4: thief
};

struct MsgServerJoinAck {
    uint32_t player_id;
};

struct MsgServerAlert {
    char message[128];
};

#pragma pack(pop)

#endif // PROTOCOL_H
