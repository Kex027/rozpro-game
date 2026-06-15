#ifndef PROTOCOL_H
#define PROTOCOL_H

// packing headers so bytes match on server and client
#pragma pack(push, 1)

// header prepended to every single message
struct PacketHeader {
    uint16_t type;
    uint32_t length;
};

// packet types. client is 1xx, server is 2xx
enum PacketType : uint16_t {
    // client requests
    MSG_CLIENT_JOIN = 101,     // player wants to join lobby
    MSG_CLIENT_READY = 102,    // player toggles ready state
    MSG_CLIENT_INPUT = 103,    // player input direction
    MSG_CLIENT_BUY = 104,      // player buys an upgrade

    // server responses
    MSG_SERVER_JOIN_ACK = 201, // server says ok and gives player ID
    MSG_SERVER_STATE = 202,    // broadcasted game state
    MSG_SERVER_ALERT = 203     // text message display (used in console now)
};

// join payload with player name
struct MsgClientJoin {
    char name[32];
};

// ready toggle status
struct MsgClientReady {
    bool is_ready;
};

// player input direction vectors
struct MsgClientInput {
    float dx; // horizontal direction (-1 to 1)
    float dy; // vertical direction (-1 to 1)
};

// upgrade buy choice
struct MsgClientBuy {
    uint32_t item_index; // 0 for speed, 1 for gold mult, etc.
};

// player join acknowledgment
struct MsgServerJoinAck {
    uint32_t player_id;
};

// system message / alert payload
struct MsgServerAlert {
    char message[128];
};

#pragma pack(pop)

#endif // PROTOCOL_H
