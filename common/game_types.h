#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#pragma pack(push, 1)

struct Vec2 {
    float x;
    float y;
};

struct Player {
    uint32_t id;
    char name[32];
    Vec2 pos;
    Vec2 dir;
    
    uint32_t gold_carried;
    uint32_t gold_in_base;
    uint32_t total_gold_all_rounds;
    uint32_t rounds_won;

    bool is_speed_upgraded;
    bool is_gold_multiplier_active;
    bool is_base_defense_active;
    bool is_attack_weapon_active;
    bool is_thief_upgrade_active;

    float slow_timer;
    float stun_timer;
    float attack_cooldown;
    
    bool is_ready;
    bool is_active;
    uint32_t color_index;
};

struct Base {
    uint32_t owner_id;
    Vec2 pos;
    float angle;
    bool is_active;
};

struct GoldItem {
    uint32_t id;
    Vec2 pos;
    uint32_t value;
    bool is_active;
};

#define MAX_PLAYERS 4
#define MAX_GOLD_ITEMS 15

struct GameState {
    uint32_t state;
    uint32_t round_number;
    float round_timer;
    
    uint32_t player_count;
    Player players[MAX_PLAYERS];
    Base bases[MAX_PLAYERS];
    
    uint32_t gold_count;
    GoldItem gold_items[MAX_GOLD_ITEMS];
    
    uint32_t winner_id;
};

#pragma pack(pop)

// Game
const float MAP_WIDTH = 1280.0f;
const float MAP_HEIGHT = 720.0f;
const float CENTER_X = 640.0f;
const float CENTER_Y = 360.0f;
const float MINE_RADIUS = 50.0f;
const float BASE_ROTATION_RADIUS = 250.0f;
const float BASE_ROTATION_SPEED = 0.15f;
const float BASE_RADIUS = 40.0f;
const float PLAYER_RADIUS = 22.0f;
const float GOLD_RADIUS = 12.0f;

// Gameplay
const float PLAYER_BASE_SPEED = 180.0f; // Pixels per second
const float PLAYER_UPGRADED_SPEED = 260.0f;
const float BASE_SLOW_FACTOR = 0.5f; // Slow down when carrying gold
const float BASE_DEFENSE_SLOW_FACTOR = 0.5f;
const float BASE_DEFENSE_SLOW_DURATION = 5.0f;

const float PLAYER_STUN_DURATION = 2.5f;
const float PLAYER_ATTACK_RANGE = 60.0f;
const float PLAYER_ATTACK_COOLDOWN = 3.0f;

const uint32_t GOLD_SPAWN_INTERVAL = 3; // Spawn every X seconds
const uint32_t MAX_GOLD_ON_MAP = 8;
const uint32_t ROUND_DURATION = 30;
const uint32_t TOTAL_ROUNDS = 3;

// Shop costs
const uint32_t COST_SPEED_BOOST = 15;
const uint32_t COST_GOLD_MULTIPLIER = 25;
const uint32_t COST_BASE_DEFENSE = 20;
const uint32_t COST_ATTACK_WEAPON = 30;
const uint32_t COST_THIEF_UPGRADE = 20;

#endif // GAME_TYPES_H
