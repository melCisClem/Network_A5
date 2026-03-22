/* Start Header
*****************************************************************/
/*!
\file Server.cpp
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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define NOMINMAX
#include <Windows.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#include "../NetworkData.h"

#define REQ_JOIN (unsigned char)0x06

std::atomic<bool> g_running{ true };
SOCKET g_serverUDPSocket = INVALID_SOCKET;

struct Player {
    sockaddr_in udpAddr{};
    bool connected = false;
    float x = 10.0f, y = 10.0f;
    bool up = false, down = false, left = false, right = false;
    bool space = false;
    int shootCooldown = 0;
    float aimAngle = 0.0f;
};

struct Projectile {
    bool active = false;
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    int lifeTimer = 0;
};

std::mutex g_stateMtx;
Player g_players[MAX_PLAYERS];
Projectile g_projectiles[MAX_PROJECTILES];

float g_moveSpeed = 0.015f;
float g_turnSpeed = 3.0f;
float g_bulletSpeed = 0.05f;

static bool recvAll(SOCKET s, void* buf, int len) {
    char* ptr = reinterpret_cast<char*>(buf);
    int rem = len;
    while (rem > 0) {
        int r = recv(s, ptr, rem, 0);
        if (r == SOCKET_ERROR || r == 0) return false;
        ptr += r; rem -= r;
    }
    return true;
}

static bool sendAll(SOCKET s, const void* data, int len) {
    const char* ptr = reinterpret_cast<const char*>(data);
    int rem = len;
    while (rem > 0) {
        int sent = send(s, ptr, rem, 0);
        if (sent == SOCKET_ERROR || sent == 0) return false;
        ptr += sent; rem -= sent;
    }
    return true;
}

// Listen for inputs from ALL clients
void udpReceiverThread() {
    std::vector<char> buf(sizeof(InputPacket));
    sockaddr_in from{};
    int fromLen = sizeof(from);

    std::cout << "[Server] Listening for client UDP inputs...\n";

    while (g_running) {
        int r = recvfrom(g_serverUDPSocket, buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fromLen);
        if (r == sizeof(InputPacket)) {
            auto* pkt = reinterpret_cast<InputPacket*>(buf.data());

            uint32_t id = ntohl(pkt->playerID);
            if (id < MAX_PLAYERS) {
                std::lock_guard<std::mutex> lk(g_stateMtx);
                if (g_players[id].connected) {
                    g_players[id].up = pkt->w_pressed;
                    g_players[id].down = pkt->s_pressed;
                    g_players[id].left = pkt->a_pressed;
                    g_players[id].right = pkt->d_pressed;
                    g_players[id].space = pkt->space_pressed;
                }
            }
        }
    }
}

// 60 Tick Game Loop
void serverGameLoop() {
    uint32_t sequence = 0;
    std::cout << "[Server] Game loop started... tickrate 60\n";

    while (g_running) {
        int activePlayers = 0;
        int activeProjectiles = 0;
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (g_players[i].connected) {
                    activePlayers++;

                    // A n D
                    if (g_players[i].left)  g_players[i].aimAngle += g_turnSpeed; // CCW
                    if (g_players[i].right) g_players[i].aimAngle -= g_turnSpeed; // CW

                    float rad = g_players[i].aimAngle * 3.14159f / 180.0f;

                    // W n S
                    if (g_players[i].up) {
                        float newX = g_players[i].x + cos(rad) * g_moveSpeed;
                        float newY = g_players[i].y + sin(rad) * g_moveSpeed;

                        if (!isWall(newX, newY)) {
                            g_players[i].x = newX;
                            g_players[i].y = newY;
                        }
                    }
                    if (g_players[i].down) {
                        float newX = g_players[i].x - cos(rad) * g_moveSpeed;
                        float newY = g_players[i].y - sin(rad) * g_moveSpeed;

                        if (!isWall(newX, newY)) {
                            g_players[i].x = newX;
                            g_players[i].y = newY;
                        }
                    }

                    if (g_players[i].space && g_players[i].shootCooldown <= 0) {
                        for (int j = 0; j < MAX_PROJECTILES; j++) {
                            if (!g_projectiles[j].active) {
                                g_projectiles[j].active = true;
                                g_projectiles[j].lifeTimer = 120; // 60 * 2 ticks

                                float gunOffset = tank_width + tank_gunLength;
                                g_projectiles[j].x = g_players[i].x + cos(rad) * gunOffset;
                                g_projectiles[j].y = g_players[i].y + sin(rad) * gunOffset;

                                g_projectiles[j].vx = cos(rad) * g_bulletSpeed;
                                g_projectiles[j].vy = sin(rad) * g_bulletSpeed;

                                g_players[i].shootCooldown = 15; // 15 ticks
                                break;
                            }
                        }
                    }
                    if (g_players[i].shootCooldown > 0) g_players[i].shootCooldown--;
                }
            }

            // update projectiles
            for (int j = 0; j < MAX_PROJECTILES; j++) {
                if (g_projectiles[j].active) {
                    g_projectiles[j].x += g_projectiles[j].vx;
                    g_projectiles[j].y += g_projectiles[j].vy;
                    g_projectiles[j].lifeTimer--;

                    // kill old proj
                    if (g_projectiles[j].lifeTimer <= 0 || isWall(g_projectiles[j].x, g_projectiles[j].y))
                        g_projectiles[j].active = false;
                    else
                        activeProjectiles++;
                }
            }
        }

        // building the packet
        GameStateHeader header;
        header.sequenceNum = htonl(sequence++);
        header.numPlayers = htonl(activePlayers);
        header.numProjectiles = htonl(activeProjectiles);

        std::vector<char> pktData;
        pktData.insert(pktData.end(), reinterpret_cast<char*>(&header), reinterpret_cast<char*>(&header) + sizeof(header));

        {
            std::lock_guard<std::mutex> lk(g_stateMtx);

            // pack players 1st
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (g_players[i].connected) {
                    PlayerState ps;
                    ps.playerID = htonl(i);
                    ps.x = g_players[i].x;
                    ps.y = g_players[i].y;
                    ps.aimAngle = g_players[i].aimAngle;

                    pktData.insert(pktData.end(), reinterpret_cast<char*>(&ps), reinterpret_cast<char*>(&ps) + sizeof(ps));
                }
            }

            // then pack proj
            for (int j = 0; j < MAX_PROJECTILES; j++) {
                if (g_projectiles[j].active) {
                    ProjectileState proj;
                    proj.x = g_projectiles[j].x;
                    proj.y = g_projectiles[j].y;
                    pktData.insert(pktData.end(), reinterpret_cast<char*>(&proj), reinterpret_cast<char*>(&proj) + sizeof(proj));
                }
            }
        }

        // broadcast
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (g_players[i].connected) {
                    sendto(g_serverUDPSocket, pktData.data(), (int)pktData.size(), 0,
                        reinterpret_cast<sockaddr*>(&g_players[i].udpAddr), sizeof(g_players[i].udpAddr));
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::string tcpPortStr = "27015";
    std::string udpPortStr = "27016";

    addrinfo tcpHints{}, * tcpInfo = nullptr;
    tcpHints.ai_family = AF_INET;
    tcpHints.ai_socktype = SOCK_STREAM;
    tcpHints.ai_flags = AI_PASSIVE;
    getaddrinfo(nullptr, tcpPortStr.c_str(), &tcpHints, &tcpInfo);
    SOCKET listenerSocket = socket(tcpHints.ai_family, tcpHints.ai_socktype, IPPROTO_TCP);
    bind(listenerSocket, tcpInfo->ai_addr, (int)tcpInfo->ai_addrlen);
    listen(listenerSocket, SOMAXCONN);
    freeaddrinfo(tcpInfo);

    addrinfo udpHints{}, * udpInfo = nullptr;
    udpHints.ai_family = AF_INET;
    udpHints.ai_socktype = SOCK_DGRAM;
    udpHints.ai_flags = AI_PASSIVE;
    getaddrinfo(nullptr, udpPortStr.c_str(), &udpHints, &udpInfo);
    g_serverUDPSocket = socket(udpHints.ai_family, udpHints.ai_socktype, IPPROTO_UDP);
    bind(g_serverUDPSocket, udpInfo->ai_addr, (int)udpInfo->ai_addrlen);
    freeaddrinfo(udpInfo);

    std::cout << "Server running on TCP: " << tcpPortStr << ", UDP: " << udpPortStr << "\n";
    std::cout << "Waiting for clients...\n";

    std::thread gameThread(serverGameLoop);
    std::thread udpThread(udpReceiverThread);

    while (g_running) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenerSocket, (sockaddr*)&clientAddr, &clientAddrLen);

        if (clientSocket != INVALID_SOCKET) {
            char cmdBuf[1];
            if (recvAll(clientSocket, cmdBuf, 1) && cmdBuf[0] == REQ_JOIN) {
                uint32_t clientIP_net{};
                uint16_t clientPort_net{};

                if (recvAll(clientSocket, &clientIP_net, 4) && recvAll(clientSocket, &clientPort_net, 2)) {

                    int assignedID = -1;
                    {
                        std::lock_guard<std::mutex> lk(g_stateMtx);

                        for (int i = 0; i < MAX_PLAYERS; i++) {
                            if (!g_players[i].connected) {
                                assignedID = i;
                                break;
                            }
                        }

                        if (assignedID != -1) {
                            g_players[assignedID].udpAddr.sin_family = AF_INET;
                            g_players[assignedID].udpAddr.sin_addr.s_addr = clientIP_net;
                            g_players[assignedID].udpAddr.sin_port = clientPort_net;
                            g_players[assignedID].connected = true;

                            int spawnMarker = assignedID + 2;
                            bool foundSpawn = false;

                            for (int row = 0; row < MAP_HEIGHT; row++) {
                                for (int col = 0; col < MAP_WIDTH; col++) {
                                    if (ARENA_MAP[row][col] == spawnMarker) {

                                        g_players[assignedID].x = ((col + 0.5f) * 2.0f / MAP_WIDTH) - 1.0f;
                                        g_players[assignedID].y = ((row + 0.5f) * 2.0f / MAP_HEIGHT) - 1.0f;

                                        g_players[assignedID].aimAngle = atan2(-g_players[assignedID].y, -g_players[assignedID].x) * 180.0f / 3.14159f;

                                        foundSpawn = true;
                                        break;
                                    }
                                }
                                if (foundSpawn) break;
                            }

                            // if nvr set spawn ptn then auto spawn in center
                            if (!foundSpawn) {
                                g_players[assignedID].x = 0.0f;
                                g_players[assignedID].y = 0.0f;
                                g_players[assignedID].aimAngle = 0.0f;
                            }

                            g_players[assignedID].up = false;
                            g_players[assignedID].down = false;
                            g_players[assignedID].left = false;
                            g_players[assignedID].right = false;
                            g_players[assignedID].space = false;
                        }
                    }

                    char idMsg = static_cast<char>(assignedID);
                    sendAll(clientSocket, &idMsg, 1);

                    if (assignedID != -1) {
                        std::cout << "[Server] Player " << assignedID << " joined.\n";

                        std::thread clientTCPThread([clientSocket, assignedID]() {
                            char dummy;
                            while (recv(clientSocket, &dummy, 1, 0) > 0) {} // Block *only* this thread

                            std::cout << "[Server] Player " << assignedID << " disconnected.\n";
                            std::lock_guard<std::mutex> lk(g_stateMtx);
                            g_players[assignedID].connected = false;
                            g_players[assignedID].x = 10.0f; // Teleport them back off-screen
                            g_players[assignedID].up = g_players[assignedID].down = g_players[assignedID].left = g_players[assignedID].right = false;
                            closesocket(clientSocket);
                            });
                        clientTCPThread.detach(); // Let the thread run free

                    }
                    else {
                        std::cout << "[Server] Rejected connection (Server Full).\n";
                        closesocket(clientSocket);
                    }
                }
            }
            else {
                closesocket(clientSocket);
            }
        }
    }

    g_running = false;
    closesocket(listenerSocket);
    closesocket(g_serverUDPSocket);
    gameThread.join();
    udpThread.join();
    WSACleanup();
    return 0;
}