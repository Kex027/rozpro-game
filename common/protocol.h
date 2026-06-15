#ifndef PROTOCOL_H
#define PROTOCOL_H

#pragma pack(push, 1)

struct PacketHeader {
    uint16_t type;
    uint32_t length;
};

// Client to Server packets
enum PacketType : uint16_t {
    // Client -> Server
    MSG_CLIENT_JOIN = 101,     // Payload: MsgClientJoin
    MSG_CLIENT_READY = 102,    // Payload: MsgClientReady
    MSG_CLIENT_INPUT = 103,    // Payload: MsgClientInput
    MSG_CLIENT_BUY = 104,      // Payload: MsgClientBuy

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
    uint32_t item_index; // 0: speed, 1: gold mult
};

struct MsgServerJoinAck {
    uint32_t player_id;
};

struct MsgServerAlert {
    char message[128];
};

#pragma pack(pop)

#endif // PROTOCOL_H
