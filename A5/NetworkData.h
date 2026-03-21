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
struct GameStatePacket {
    uint32_t sequenceNum; // To ignore out-of-order older packets
    float playerX;
    float playerY;
};
#pragma pack(pop)


