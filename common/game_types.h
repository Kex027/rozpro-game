#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#pragma pack(push, 1)

// Simple vector struct for positions
struct Vec2 {
    float x;
    float y;
};

// Player statistics and status
struct Player {
    uint32_t id;
    char name[32];
    Vec2 pos;
    Vec2 dir; // Input movement direction (normalized or raw WASD vector)
    
    // Gold stats
    uint32_t gold_carried;      // Gold in hands (stolen on attack)
    uint32_t gold_in_base;       // Gold safely stored in base for the current round
    uint32_t total_gold_all_rounds; // Cumulative gold for tiebreaker
    uint32_t rounds_won;        // Score of won rounds
    
    // Upgrade statuses
    bool is_speed_upgraded;     // Pasywne: szybsze poruszanie
    bool is_gold_multiplier_active;   // Pasywne: złoto warte więcej
    bool is_base_defense_active;      // Ulepszenie bazy: spowalnia wrogów
    bool is_attack_weapon_active;     // Broń: pozwala atakować innych i kraść niesione złoto
    bool is_thief_upgrade_active;     // Pasywne: kradnie więcej % z wrogiej bazy
    
    // Status timers (in seconds, managed by server tick)
    float slow_timer;           // If > 0, player is slowed (e.g. from entering enemy protected base)
    float stun_timer;           // If > 0, player is stunned (e.g. from attack) and cannot move/act
    float attack_cooldown;      // Cooldown for attack action
    
    bool is_ready;              // Ready status in lobby
    bool is_active;             // False if player disconnected
    uint32_t color_index;       // Index to map to a color on the client
};

// Base structures rotating around the center
struct Base {
    uint32_t owner_id;          // Owner player ID
    Vec2 pos;                   // Position in 2D space
    float angle;                // Current angle of rotation (in radians)
    bool is_active;             // Set to false if owner disconnected
};

// Spawning gold items
struct GoldItem {
    uint32_t id;
    Vec2 pos;
    uint32_t value;             // Base value (e.g. 5 gold)
    bool is_active;             // True if spawned on map
};

// Constant game configurations
#define MAX_PLAYERS 4
#define MAX_GOLD_ITEMS 15

// Entire game state synced from server to clients
struct GameState {
    uint32_t state;             // 0 = Lobby, 1 = Playing, 2 = RoundEnd, 3 = GameOver
    uint32_t round_number;      // Current round
    float round_timer;          // Seconds remaining in round
    
    uint32_t player_count;
    Player players[MAX_PLAYERS];
    Base bases[MAX_PLAYERS];
    
    uint32_t gold_count;
    GoldItem gold_items[MAX_GOLD_ITEMS];
    
    uint32_t winner_id;         // Used in RoundEnd / GameOver states
};

#pragma pack(pop)

// Game constants (unpacked, compile time constants)
const float MAP_WIDTH = 1280.0f;
const float MAP_HEIGHT = 720.0f;
const float CENTER_X = 640.0f;
const float CENTER_Y = 360.0f;
const float MINE_RADIUS = 50.0f;
const float BASE_ROTATION_RADIUS = 250.0f;
const float BASE_ROTATION_SPEED = 0.15f; // Radians per second
const float BASE_RADIUS = 40.0f;
const float PLAYER_RADIUS = 22.0f;
const float GOLD_RADIUS = 12.0f;

// Physics / Gameplay balance constants
const float PLAYER_BASE_SPEED = 180.0f; // Pixels per second
const float PLAYER_UPGRADED_SPEED = 260.0f;
const float BASE_SLOW_FACTOR = 0.5f; // Slow down when carrying gold (optional, or just from base defense)
const float BASE_DEFENSE_SLOW_FACTOR = 0.5f;
const float BASE_DEFENSE_SLOW_DURATION = 5.0f;

const float PLAYER_STUN_DURATION = 2.5f;
const float PLAYER_ATTACK_RANGE = 60.0f;
const float PLAYER_ATTACK_COOLDOWN = 3.0f;

const uint32_t GOLD_SPAWN_INTERVAL = 3; // Spawn every X seconds
const uint32_t MAX_GOLD_ON_MAP = 8;
const uint32_t ROUND_DURATION = 60; // 60 seconds per round
const uint32_t TOTAL_ROUNDS = 3;

// Shop costs
const uint32_t COST_SPEED_BOOST = 15;
const uint32_t COST_GOLD_MULTIPLIER = 25;
const uint32_t COST_BASE_DEFENSE = 20;
const uint32_t COST_ATTACK_WEAPON = 30;
const uint32_t COST_THIEF_UPGRADE = 20;

#endif // GAME_TYPES_H
