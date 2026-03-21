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
    float p0X, p0Y;
    float p1X, p1Y;
};

struct InputPacket {
    uint32_t sequenceNum;
    uint32_t playerID;
    bool w_pressed;
    bool a_pressed;
    bool s_pressed;
    bool d_pressed;
};
#pragma pack(pop)


