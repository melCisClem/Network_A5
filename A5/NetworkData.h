/* Start Header
*****************************************************************/
/*!
\file NetworkData.cpp
\authors
\   Lai Jun Jie Clement (junjieclement.lai@digipen.edu)
\   Aryan bin Mohamed Isran (aryan.b@digipen.edu)
\   Lee Hwee Min (l.hweemin@digipen.edu)
\par
\date
\brief
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
struct PlayerState {
    uint32_t playerID;
    float x;
    float y;
    float aimAngle;
};

struct ProjectileState {
    float x;
    float y;
};

struct GameStateHeader {
    uint32_t sequenceNum;
    uint32_t numPlayers;
    uint32_t numProjectiles;
};

struct InputPacket {
    uint32_t sequenceNum;
    uint32_t playerID;
    bool w_pressed;
    bool a_pressed;
    bool s_pressed;
    bool d_pressed;
    bool space_pressed;
    float aimAngle;
};
#pragma pack(pop)

constexpr int MAX_PLAYERS = 4;
constexpr int MAX_PROJECTILES = 100;
constexpr int UDPPACKET_BUFFER_SIZE = 4096;

constexpr float tank_width = 0.04f;
constexpr float tank_height = 0.03f;
constexpr float tank_gunLength = 0.05f;
constexpr float tank_outline_thickness = 2.0f;

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