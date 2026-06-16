#include "raylib.h"
#include "network_client.h"
#include "../common/game_types.h"
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using namespace std;

struct Notification {
    string text;
    float timer;
};
static vector<Notification> notifications;

static char name_input[32] = "kox";
static char ip_input[32] = "127.0.0.1";
static int active_input_box = 0;
static bool is_shop_open = false;

Color GetPlayerColor(uint32_t index) {
    switch (index) {
    case 0: return RED;
    case 1: return BLUE;
    case 2: return LIME;
    case 3: return ORANGE;
    default: return PURPLE;
    }
}

Color GetPlayerDarkColor(uint32_t index) {
    switch (index) {
        case 0: return MAROON;
        case 1: return DARKBLUE;
        case 2: return GREEN;
        case 3: return Color{ 200, 100, 0, 255 };
        default: return DARKPURPLE;
    }
}

void HandleTextBoxInput(char* buffer, int max_len, int key) {
    int len = (int)strlen(buffer);
    if (key >= 32 && key <= 125 && len < max_len - 1) {
        buffer[len] = (char)key;
        buffer[len + 1] = '\0';
    }
    if (key == KEY_BACKSPACE && len > 0) {
        buffer[len - 1] = '\0';
    }
}

void update_notifications(float dt, bool is_connected) {
    if (is_connected) {
        vector<string> new_alerts = network_get_alerts();
        for (const auto& alert : new_alerts) {
            Notification notif;
            notif.text = alert;
            notif.timer = 4.0f;
            notifications.push_back(notif);
        }
    }

    for (auto it = notifications.begin(); it != notifications.end();) {
        it->timer -= dt;
        if (it->timer <= 0) {
            it = notifications.erase(it);
        } else {
            ++it;
        }
    }
}

void handle_player_input(float dt, bool is_connected) {
    if (!is_connected) {
        int key = GetCharPressed();
        while (key > 0) {
            if (active_input_box == 0) {
                HandleTextBoxInput(name_input, 15, key);
            } else if (active_input_box == 1) {
                HandleTextBoxInput(ip_input, 16, key);
            }
            key = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            if (active_input_box == 0) {
                HandleTextBoxInput(name_input, 15, KEY_BACKSPACE);
            } else if (active_input_box == 1) {
                HandleTextBoxInput(ip_input, 16, KEY_BACKSPACE);
            }
        }

        if (IsKeyPressed(KEY_TAB)) {
            active_input_box = (active_input_box + 1) % 2;
        }

        if (IsKeyPressed(KEY_ENTER)) {
            network_connect(ip_input, 5000, name_input);
        }
    }
    else {
        GameState state_copy = network_get_state();
        uint32_t my_id = network_get_player_id();

        Player my_player;
        bool is_found_me = false;
        for (int i = 0; i < (int)state_copy.player_count; ++i) {
            if (state_copy.players[i].id == my_id) {
                my_player = state_copy.players[i];
                is_found_me = true;
                break;
            }
        }

        if (is_found_me && my_player.is_active) {
            if (IsKeyPressed(KEY_B)) {
                is_shop_open = !is_shop_open;
            }

            if (state_copy.state == 0 && IsKeyPressed(KEY_SPACE)) {
                network_send_ready(!my_player.is_ready);
            }

            float dx = 0.0f;
            float dy = 0.0f;
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) dy -= 1.0f;
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) dy += 1.0f;
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) dx -= 1.0f;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) dx += 1.0f;

            float len = sqrt(dx * dx + dy * dy);
            if (len > 0.001f) {
                dx /= len;
                dy /= len;
            }

            static float last_send_time = 0;
            last_send_time += dt;
            if (last_send_time >= 0.015f) {
                network_send_input(dx, dy);
                last_send_time = 0.0f;
            }
        }
    }
}

void draw_login_screen() {
    DrawText("GOLD FEVER", 490, 130, 48, GOLD);

    DrawText("NICKNAME:", 450, 250, 20, BLACK);
    Rectangle name_rect = Rectangle{ 450, 280, 380, 40 };
    DrawRectangleRec(name_rect, WHITE);
    DrawText(name_input, 465, 290, 22, BLACK);
    if (active_input_box == 0 && (int)(GetTime() * 2) % 2 == 0) {
        int cursor_pos = MeasureText(name_input, 22) + 470;
        DrawLine(cursor_pos, 285, cursor_pos, 315, BLACK);
    }

    DrawText("SERVER IP ADDRESS:", 450, 360, 20, BLACK);
    Rectangle ip_rect = Rectangle{ 450, 390, 380, 40 };
    DrawRectangleRec(ip_rect, WHITE);
    DrawText(ip_input, 465, 400, 22, BLACK);
    if (active_input_box == 1 && (int)(GetTime() * 2) % 2 == 0) {
        int cursor_pos = MeasureText(ip_input, 22) + 470;
        DrawLine(cursor_pos, 395, cursor_pos, 425, BLACK);
    }

    Rectangle btn_rect = Rectangle{ 530, 480, 220, 50 };
    Vector2 mouse = GetMousePosition();
    bool is_hovered = CheckCollisionPointRec(mouse, btn_rect);
    DrawRectangleRec(btn_rect, is_hovered ? GOLD : ORANGE);
    DrawText("ENTER LOBBY", 565, 492, 22, WHITE);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, name_rect)) active_input_box = 0;
        else if (CheckCollisionPointRec(mouse, ip_rect)) active_input_box = 1;
        else if (is_hovered) {
            network_connect(ip_input, 5000, name_input);
        }
    }

    string connection_err = network_get_error();
    if (!connection_err.empty()) {
        int err_w = MeasureText(connection_err.c_str(), 18);
        DrawText(connection_err.c_str(), 640 - err_w / 2, 560, 18, RED);
    }
}

void draw_game_world(const GameState& state) {
    // draw island background and borders
    DrawRectangle(30, 30, (int)MAP_WIDTH - 60, (int)MAP_HEIGHT - 60, Color{ 245, 222, 179, 255 });

    // draw player bases
    for (int i = 0; i < (int)state.player_count; ++i) {
        const Base& b = state.bases[i];
        if (!b.is_active) continue;

        Color c = GetPlayerColor(state.players[i].color_index);

        // draw base circle outline
        DrawCircle((int)b.pos.x, (int)b.pos.y, BASE_RADIUS - 6.0f, c);
        DrawCircle((int)b.pos.x, (int)b.pos.y, BASE_RADIUS - 12.0f, Color{ 245, 222, 179, 255 });

        char label[64];
        sprintf(label, "%s: %d", state.players[i].name, state.players[i].gold_in_base);
        int label_w = MeasureText(label, 14);
        DrawRectangle((int)(b.pos.x - label_w / 2 - 4), (int)(b.pos.y - BASE_RADIUS - 22), label_w + 8, 18, Color{ 0, 0, 0, 160 });
        DrawText(label, (int)(b.pos.x - label_w / 2), (int)(b.pos.y - BASE_RADIUS - 20), 14, WHITE);
    }

    // draw central gold mine
    DrawCircle((int)CENTER_X, (int)CENTER_Y, MINE_RADIUS + 5.0f, DARKGRAY);
    DrawText("MINE", (int)(CENTER_X - 16), (int)(CENTER_Y - 8), 14, GOLD);

    // draw gold items on the ground
    for (int i = 0; i < MAX_GOLD_ITEMS; ++i) {
        const GoldItem& gold = state.gold_items[i];
        if (!gold.is_active) continue;

        DrawPoly(Vector2{ gold.pos.x, gold.pos.y }, 4, GOLD_RADIUS, 45.0f, GOLD);

        char val_str[16];
        sprintf(val_str, "%d", gold.value);
        DrawText(val_str, (int)(gold.pos.x - MeasureText(val_str, 12) / 2), (int)(gold.pos.y - 6), 12, BLACK);
    }

    // draw players and details
    for (int i = 0; i < (int)state.player_count; ++i) {
        const Player& p = state.players[i];
        if (!p.is_active) continue;

        Color color = GetPlayerColor(p.color_index);
        DrawCircle((int)p.pos.x, (int)p.pos.y, PLAYER_RADIUS, color);

        if (p.gold_carried > 0) {
            DrawRectangle((int)(p.pos.x - 7), (int)(p.pos.y - PLAYER_RADIUS - 14), 14, 14, GOLD);
            char carry_txt[16];
            sprintf(carry_txt, "+%d", p.gold_carried);
            DrawText(carry_txt, (int)(p.pos.x - MeasureText(carry_txt, 10) / 2), (int)(p.pos.y - PLAYER_RADIUS - 12), 10, BLACK);
        }

        char name_tag[64];
        sprintf(name_tag, "%s", p.name);
        int tag_w = MeasureText(name_tag, 13);
        DrawRectangle((int)(p.pos.x - tag_w / 2 - 4), (int)(p.pos.y + PLAYER_RADIUS + 4), tag_w + 8, 16, Color{ 0, 0, 0, 160 });
        DrawText(name_tag, (int)(p.pos.x - tag_w / 2), (int)(p.pos.y + PLAYER_RADIUS + 6), 13, WHITE);
    }
}

void draw_status_bar(const GameState& state) {
    DrawRectangle(0, 0, 1280, 45, Color{ 0, 0, 0, 180 });

    char round_txt[64];
    if (state.state == 0) {
        strcpy(round_txt, "LOBBY");
    } else if (state.state == 1) {
        sprintf(round_txt, "ROUND %d / %d", state.round_number, TOTAL_ROUNDS);
    } else if (state.state == 2) {
        sprintf(round_txt, "ROUND %d OVER", state.round_number);
    } else {
        strcpy(round_txt, "GAME OVER");
    }
    DrawText(round_txt, 30, 12, 20, GOLD);

    char timer_txt[64];
    if (state.state == 1) {
        sprintf(timer_txt, "TIME REMAINING: %.0fs", state.round_timer);
    } else {
        strcpy(timer_txt, "");
    }
    DrawText(timer_txt, 980, 12, 20, WHITE);

    DrawText("[B] SHOP | [SPACE] READY", 420, 15, 14, LIGHTGRAY);
}

void draw_lobby_overlay(const GameState& state, uint32_t my_id) {
    DrawRectangle(440, 200, 400, 360, Color{ 0, 0, 0, 200 });
    DrawText("LOBBY", 530, 220, 24, GOLD);
    DrawText("Move can move with WSAD", 475, 260, 16, LIGHTGRAY);

    DrawText("PLAYERS:", 470, 300, 18, WHITE);
    for (int i = 0; i < (int)state.player_count; ++i) {
        const Player& p = state.players[i];
        Color c = GetPlayerColor(p.color_index);

        DrawCircle(485, 340 + i * 35, 10, c);
        DrawText(p.name, 510, 332 + i * 35, 16, WHITE);
        if (p.is_ready) {
            DrawText("READY", 720, 332 + i * 35, 16, LIME);
        } else {
            DrawText("NOT READY", 720, 332 + i * 35, 16, RED);
        }
    }

    for (int i = 0; i < (int)state.player_count; ++i) {
        if (state.players[i].id == my_id) {
            bool is_my_ready = state.players[i].is_ready;
            Rectangle r_btn = Rectangle{ 540, 490, 200, 45 };
            bool is_r_hover = CheckCollisionPointRec(GetMousePosition(), r_btn);

            DrawRectangleRec(r_btn, is_my_ready ? LIME : (is_r_hover ? GOLD : ORANGE));

            const char* r_txt = is_my_ready ? "MARKED READY" : "MARK READY";
            DrawText(r_txt, 540 + (200 - MeasureText(r_txt, 18)) / 2, 502, 18, BLACK);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && is_r_hover) {
                network_send_ready(!is_my_ready);
            }
        }
    }
}

void draw_round_over_overlay(const GameState& state) {
    DrawRectangle(400, 220, 480, 280, Color{ 0, 0, 0, 210 });
    DrawText("ROUND OVER!", 560, 240, 28, GOLD);

    string round_win_name = "Tie/None";
    if (state.winner_id != 0) {
        for (int i = 0; i < (int)state.player_count; ++i) {
            if (state.players[i].id == state.winner_id) {
                round_win_name = state.players[i].name;
                break;
            }
        }
    }

    char win_info[64];
    sprintf(win_info, "Winner: %s", round_win_name.c_str());
    DrawText(win_info, 640 - MeasureText(win_info, 20) / 2, 290, 20, LIME);

    DrawText("Round Scores:", 450, 330, 16, LIGHTGRAY);
    for (int i = 0; i < (int)state.player_count; ++i) {
        char score_line[64];
        sprintf(score_line, "%s: %d gold", state.players[i].name, state.players[i].gold_in_base);
        DrawText(score_line, 450, 360 + i * 24, 16, WHITE);
    }

    DrawText("Starting next round...", 525, 470, 14, GRAY);
}

void draw_game_over_overlay(const GameState& state) {
    DrawRectangle(380, 160, 520, 400, Color{ 0, 0, 0, 220 });
    DrawText("LAST ROUND", 530, 185, 34, GOLD);

    string grand_win_name = "Multiple Winners";
    if (state.winner_id != 0) {
        for (int i = 0; i < (int)state.player_count; ++i) {
            if (state.players[i].id == state.winner_id) {
                grand_win_name = string(state.players[i].name) + " wins the game!";
                break;
            }
        }
    }

    DrawText(grand_win_name.c_str(), 640 - MeasureText(grand_win_name.c_str(), 22) / 2, 240, 22, LIME);

    struct LeaderboardEntry {
        string name;
        uint32_t rounds_won;
        uint32_t total_gold;
    };
    vector<LeaderboardEntry> leaderboard;
    for (int i = 0; i < (int)state.player_count; ++i) {
        leaderboard.push_back({ state.players[i].name, state.players[i].rounds_won, state.players[i].total_gold_all_rounds });
    }
    sort(leaderboard.begin(), leaderboard.end(), [](const LeaderboardEntry& a, const LeaderboardEntry& b){
        if (a.rounds_won != b.rounds_won) return a.rounds_won > b.rounds_won;
        return a.total_gold > b.total_gold;
    });

    DrawText("FINAL LEADERBOARD:", 420, 290, 18, LIGHTGRAY);
    for (int i = 0; i < (int)leaderboard.size(); ++i) {
        char place_txt[128];
        sprintf(place_txt, "#%d %s  -  Rounds Won: %d  (Total Gold: %d)", 
                     i + 1, leaderboard[i].name.c_str(), leaderboard[i].rounds_won, leaderboard[i].total_gold);
        DrawText(place_txt, 420, 330 + i * 30, 16, i == 0 ? YELLOW : WHITE);
    }

    DrawText("Returning to Lobby", 555, 520, 14, GRAY);
}

void draw_shop_overlay(const GameState& state, uint32_t my_id) {
    Player my_player;
    bool is_found_me = false;
    for (int i = 0; i < (int)state.player_count; ++i) {
        if (state.players[i].id == my_id) {
            my_player = state.players[i];
            is_found_me = true;
            break;
        }
    }

    if (is_found_me) {
        DrawRectangle(0, 0, 1280, 720, Color{ 0, 0, 0, 180 });

        DrawRectangle(340, 80, 600, 560, Color{ 235, 195, 135, 255 });
        DrawText("STORE", 475, 105, 28, BLACK);

        char shop_gold_txt[64];
        sprintf(shop_gold_txt, "Gold Stored in base: %d", my_player.gold_in_base);
        DrawText(shop_gold_txt, 380, 150, 18, MAROON);

        struct ShopItem {
            int index;
            const char* name;
            const char* desc;
            int cost;
            bool is_owned;
        };

        vector<ShopItem> shop_items = {
            { 0, "Speed Boots", "Increases speed by 45% (Passive)", (int)COST_SPEED_BOOST, my_player.is_speed_upgraded },
            { 1, "Gold Pan Multiplier", "Gold is worth 1.5x (Passive)", (int)COST_GOLD_MULTIPLIER, my_player.is_gold_multiplier_active }
        };

        for (int i = 0; i < (int)shop_items.size(); ++i) {
            int y_pos = 210 + i * 80;
            DrawRectangle(370, y_pos, 540, 70, Color{ 255, 255, 255, 150 });

            DrawText(shop_items[i].name, 390, y_pos + 12, 18, BLACK);
            DrawText(shop_items[i].desc, 390, y_pos + 38, 13, DARKGRAY);

            char cost_txt[32];
            sprintf(cost_txt, "%d Gold", shop_items[i].cost);

            Rectangle buy_btn = Rectangle{ 780, (float)(y_pos + 15), 110, 40 };
            bool is_buy_hover = CheckCollisionPointRec(GetMousePosition(), buy_btn);

            if (shop_items[i].is_owned) {
                DrawRectangleRec(buy_btn, LIGHTGRAY);
                DrawText("OWNED", 780 + (110 - MeasureText("OWNED", 14)) / 2, y_pos + 27, 14, DARKGRAY);
            }
            else {
                bool is_affordable = (my_player.gold_in_base >= (uint32_t)shop_items[i].cost);
                DrawRectangleRec(buy_btn, is_buy_hover ? GOLD : (is_affordable ? ORANGE : RED));

                DrawText(cost_txt, 780 + (110 - MeasureText(cost_txt, 14)) / 2, y_pos + 27, 14, BLACK);

                if (is_affordable && is_buy_hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    network_send_buy((uint32_t)shop_items[i].index);
                }
            }
        }

        DrawText("Press [B] or click off screen to close", 510, 610, 13, DARKGRAY);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            if (mouse.x < 340 || mouse.x > 940 || mouse.y < 80 || mouse.y > 640) {
                is_shop_open = false;
            }
        }
    }
}

void draw_notifications() {
    int notif_y = 680;
    for (int i = (int)notifications.size() - 1; i >= 0 && i >= (int)notifications.size() - 5; --i) {
        Notification& n = notifications[i];
        unsigned char alpha = 255;
        if (n.timer < 1.0f) {
            alpha = (unsigned char)(n.timer * 255);
        }

        int text_w = MeasureText(n.text.c_str(), 14);
        DrawRectangle(25, notif_y - 22, text_w + 16, 24, Color{ 0, 0, 0, (unsigned char)(alpha * 0.7f) });
        DrawText(n.text.c_str(), 33, notif_y - 18, 14, Color{ 255, 255, 255, alpha });
        notif_y -= 28;
    }
}

int main() {
    srand((unsigned)time(nullptr));
    InitWindow(1280, 720, "Gold Fever");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        bool is_connected = network_is_connected();

        update_notifications(dt, is_connected);
        handle_player_input(dt, is_connected);

        BeginDrawing();
        ClearBackground(Color{ 40, 80, 150, 255 });

        if (!is_connected) {
            draw_login_screen();
        } else {
            GameState state = network_get_state();
            uint32_t my_id = network_get_player_id();

            draw_game_world(state);
            draw_status_bar(state);

            if (state.state == 0) {
                draw_lobby_overlay(state, my_id);
            } else if (state.state == 2) {
                draw_round_over_overlay(state);
            } else if (state.state == 3) {
                draw_game_over_overlay(state);
            }

            if (is_shop_open && state.state == 1) {
                draw_shop_overlay(state, my_id);
            }

            draw_notifications();
        }

        EndDrawing();
    }

    network_disconnect();
    CloseWindow();
    return 0;
}