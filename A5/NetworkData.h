/* Start Header
*****************************************************************/
/*!
\file NetworkData.h
\authors
\   Lai Jun Jie Clement (junjieclement.lai@digipen.edu)
\   Aryan bin Mohamed Isran (aryan.b@digipen.edu)
\   Lee Hwee Min (l.hweemin@digipen.edu)
\date 25/03/2026
\brief
\   the file that stores all the structs and vars for network stuff
\
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#pragma once

#include <cstdint>

#pragma pack(push, 1)

#define REQ_JOIN         (unsigned char)0x06
#define REQ_TOGGLE_READY (unsigned char)0x09
#define REQ_CHEAT_WIN    (unsigned char)0x0A
#define REQ_CHAT         (unsigned char)0x0B
#define REQ_BUY_UPGRADE  (unsigned char)0x0C
#define REQ_LEADERBOARD  (unsigned char)0x0D

struct JoinResponse {
    uint32_t playerID;
    int32_t totalKills;
    uint8_t hasUpgradedGun; // use 1 byte explicitly
};

struct LeaderboardEntry {
    char name[16];
    int32_t totalKills;
};

struct PlayerState {
    uint32_t playerID;
    char name[16];
    float x;
    float y;
    float aimAngle;
    int32_t hp;
    int32_t kills; // current match
    int32_t totalKills; // persistent
    uint8_t justShot;
    uint8_t justHit;
    int32_t shootCooldown;
    uint8_t isReady;
    uint8_t hasUpgradedGun;
};

struct ProjectileState {
    float x;
    float y;
    uint8_t isUpgraded;
};

struct GameStateHeader {
    uint32_t sequenceNum;
    uint32_t matchState; // 0 = waiting, 1 = in game, 2 = game over
    uint32_t numPlayers;
    uint32_t numProjectiles;
    int32_t winnerID;
};

struct InputPacket {
    uint32_t sequenceNum;
    uint32_t playerID;
    uint8_t w_pressed;
    uint8_t a_pressed;
    uint8_t s_pressed;
    uint8_t d_pressed;
    uint8_t space_pressed;
    float aimAngle;
    uint8_t hasUpgradedGun;
};

#pragma pack(pop)

constexpr int MAX_PLAYERS = 4;
constexpr int MAX_PROJECTILES = 100;
constexpr int UDPPACKET_BUFFER_SIZE = 4096;

constexpr int MAX_HP = 100;
constexpr int BULLET_DAMAGE = 25;
constexpr int PROJECTILE_TTL = 120; // ticks

constexpr float tank_width = 0.04f;
constexpr float tank_height = 0.03f;
constexpr float tank_gunLength = 0.05f;
constexpr float tank_outline_thickness = 2.0f;
constexpr float tank_hp_thickness = 0.003f;
constexpr int tank_shootCooldown = 120; // ticks

constexpr int gameOverTimer = 300; // ticks so abt 5secs;

// MAP stuff
constexpr int MAP_WIDTH = 10;
constexpr int MAP_HEIGHT = 10;

// 1 is wall 
// 0 is nth
// 2, 3, 4, 5 is spawn ptn for Player 0, 1 ,2 ,3 respectivly
constexpr int ARENA_MAP[MAP_HEIGHT][MAP_WIDTH] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 4, 0, 0, 0, 0, 0, 0, 5, 1},
    {1, 0, 1, 1, 0, 0, 1, 1, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 1, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 1, 0, 1},
    {1, 0, 1, 1, 0, 0, 1, 1, 0, 1},
    {1, 2, 0, 0, 0, 0, 0, 0, 3, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
};

inline bool isWall(float x, float y) {
    // convert (-1.0 to 1.0) into (0 to 9)
    int col = static_cast<int>((x + 1.0f) * (MAP_WIDTH / 2.0f));
    int row = static_cast<int>((y + 1.0f) * (MAP_HEIGHT / 2.0f));

    if (col < 0 || col >= MAP_WIDTH || row < 0 || row >= MAP_HEIGHT) 
        return true;

    return ARENA_MAP[row][col] == 1;
}
