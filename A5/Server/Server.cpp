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
#define REQ_TOGGLE_READY (unsigned char)0x09
#define REQ_CHEAT_WIN    (unsigned char)0x0A
#define REQ_CHAT         (unsigned char)0x0B
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

std::atomic<bool> g_running{ true };
SOCKET g_serverUDPSocket = INVALID_SOCKET;

struct Player {
    sockaddr_in udpAddr{};
    SOCKET tcpSocket = INVALID_SOCKET;
    std::string name = "Player";
    float x = 10.0f, y = 10.0f;
    float aimAngle = 0.0f;
    int shootCooldown = 0;
    int hp = MAX_HP;
    int kills = 0;
    bool connected = false;
    bool isReady = false;
    bool up = false, down = false, left = false, right = false;
    bool space = false;
    bool justShot = false;
    bool justHit = false;
};

struct Projectile {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    int ownerID = -1;
    int lifeTimer = 0;
    bool active = false;
};

std::mutex g_stateMtx;
int g_matchState = 0;
int g_winnerID = -1;
int g_gameOverTimer = 0;

Player g_players[MAX_PLAYERS];
Projectile g_projectiles[MAX_PROJECTILES];

float g_moveSpeed = 0.015f;
float g_turnSpeed = 3.0f;
float g_bulletSpeed = 0.05f;


// from A4
static bool recvAll(SOCKET s, void* buf, int len) 
{
    char* ptr = reinterpret_cast<char*>(buf);
    int rem = len;
    while (rem > 0) 
    {
        int r = recv(s, ptr, rem, 0);
        if (r == SOCKET_ERROR || r == 0) 
            return false;
        ptr += r; rem -= r;
    }
    return true;
}

// from A4
static bool sendAll(SOCKET s, const void* data, int len) 
{
    const char* ptr = reinterpret_cast<const char*>(data);
    int rem = len;
    while (rem > 0) 
    {
        int sent = send(s, ptr, rem, 0);
        if (sent == SOCKET_ERROR || sent == 0) 
            return false;
        ptr += sent; rem -= sent;
    }
    return true;
}

// listen for inputs from ALL clients
void udpReceiverThread() 
{
    std::vector<char> buf(sizeof(InputPacket));
    sockaddr_in from{};
    int fromLen = sizeof(from);

#ifdef _DEBUG
    std::cout << "[Server] Listening for client UDP inputs...\n";
#endif

    while (g_running) 
    {
        int r = recvfrom(g_serverUDPSocket, buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fromLen);
        if (r == sizeof(InputPacket)) 
        {
            auto* pkt = reinterpret_cast<InputPacket*>(buf.data());

            uint32_t id = ntohl(pkt->playerID);
            if (id < MAX_PLAYERS) 
            {
                std::lock_guard<std::mutex> lk(g_stateMtx);
                if (g_players[id].connected) 
                {
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
void serverGameLoop() 
{
    uint32_t sequence = 0;
    std::cout << "[Server] Game loop started | tickrate 60\n";

    while (g_running) 
    {
        int activePlayers = 0;
        int readyPlayers = 0;
        int activeProjectiles = 0;
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);

            // reset audio flags at start of evrey tick
            for (int i = 0; i < MAX_PLAYERS; i++) 
            {
                g_players[i].justShot = false;
                g_players[i].justHit = false;
                if (g_players[i].connected) 
                {
                    activePlayers++;
                    if (g_players[i].isReady) 
                        readyPlayers++;
                }
            }

            if (g_matchState == 0) // waiting room
            {
                if (activePlayers > 0 && activePlayers == readyPlayers) 
                {
                    g_matchState = 1;
                    std::cout << "[Server] All players ready! STARTING MATCH!\n";
                }
            }
            else if (g_matchState == 1) // in game
            {
                for (int i = 0; i < MAX_PLAYERS; i++)
                {
                    if (g_players[i].connected)
                    {
                        // A n D
                        if (g_players[i].left)  g_players[i].aimAngle += g_turnSpeed; // CCW
                        if (g_players[i].right) g_players[i].aimAngle -= g_turnSpeed; // CW

                        float rad = g_players[i].aimAngle * 3.14159f / 180.0f;

                        // W n S
                        if (g_players[i].up)
                        {
                            float newX = g_players[i].x + cos(rad) * g_moveSpeed;
                            float newY = g_players[i].y + sin(rad) * g_moveSpeed;

                            if (!isWall(newX, newY))
                            {
                                g_players[i].x = newX;
                                g_players[i].y = newY;
                            }
                        }
                        if (g_players[i].down)
                        {
                            float newX = g_players[i].x - cos(rad) * g_moveSpeed;
                            float newY = g_players[i].y - sin(rad) * g_moveSpeed;

                            if (!isWall(newX, newY))
                            {
                                g_players[i].x = newX;
                                g_players[i].y = newY;
                            }
                        }

                        if (g_players[i].space && g_players[i].shootCooldown <= 0)
                        {
                            for (int j = 0; j < MAX_PROJECTILES; j++)
                            {
                                if (!g_projectiles[j].active)
                                {
                                    g_projectiles[j].active = true;
                                    g_projectiles[j].lifeTimer = PROJECTILE_TTL;
                                    g_projectiles[j].ownerID = i;

                                    float gunOffset = tank_width + tank_gunLength;
                                    g_projectiles[j].x = g_players[i].x + cos(rad) * gunOffset;
                                    g_projectiles[j].y = g_players[i].y + sin(rad) * gunOffset;

                                    g_projectiles[j].vx = cos(rad) * g_bulletSpeed;
                                    g_projectiles[j].vy = sin(rad) * g_bulletSpeed;

                                    g_players[i].shootCooldown = tank_shootCooldown;
                                    g_players[i].justShot = true;
                                    break;
                                }
                            }
                        }
                        if (g_players[i].shootCooldown > 0) g_players[i].shootCooldown--;
                    }
                }

                // update projectiles
                for (int j = 0; j < MAX_PROJECTILES; j++) 
                {
                    if (g_projectiles[j].active) 
                    {
                        g_projectiles[j].x += g_projectiles[j].vx;
                        g_projectiles[j].y += g_projectiles[j].vy;
                        g_projectiles[j].lifeTimer--;

                        bool hitPlayer = false;

                        // check if projectile hit other player
                        for (int p = 0; p < MAX_PLAYERS; p++) 
                        {
                            // cant hit ownself n dead players
                            if (g_players[p].connected && p != g_projectiles[j].ownerID && g_players[p].hp > 0) 
                            {

                                float dx = g_projectiles[j].x - g_players[p].x;
                                float dy = g_projectiles[j].y - g_players[p].y;

                                if ((dx * dx + dy * dy) < (0.06f * 0.06f)) 
                                {
                                    g_players[p].hp -= BULLET_DAMAGE;
                                    g_players[p].justHit = true;

                                    // respawn
                                    if (g_players[p].hp <= 0) 
                                    {
                                        g_players[p].x = 0.0f; //  middle of the map
                                        g_players[p].y = 0.0f;
                                        g_players[p].hp = MAX_HP;

                                        int shooterID = g_projectiles[j].ownerID;
                                        if (shooterID != -1 && g_players[shooterID].connected) 
                                        {
                                            g_players[shooterID].kills++;

                                            // check for win cond
                                            if (g_players[shooterID].kills >= 5 && g_matchState == 1) 
                                            {
                                                g_matchState = 2;
                                                g_winnerID = shooterID;
                                                g_gameOverTimer = gameOverTimer; // 5 seconds ( 60 * 5 )
                                                std::cout << "[Server] Player " << shooterID << " won!\n";
                                            }
                                        }
                                    }
                                    hitPlayer = true;
                                    break;
                                }
                            }
                        }

                        // kill old proj
                        if (g_projectiles[j].lifeTimer <= 0 || isWall(g_projectiles[j].x, g_projectiles[j].y) || hitPlayer)
                            g_projectiles[j].active = false;
                        else
                            activeProjectiles++;
                    }
                }
            }
            else if (g_matchState == 2)
            {
                g_gameOverTimer--;
                if (g_gameOverTimer <= 0)
                {
                    for (int i = 0; i < MAX_PLAYERS; i++) 
                    {
                        g_players[i].isReady = false;
                        g_players[i].kills = 0;
                        g_players[i].hp = MAX_HP;
                        g_projectiles[i].active = false;
                    }
                    g_matchState = 0;
                    g_winnerID = -1;
                    std::cout << "[Server] Match reset. Returning players to menus.\n";
                }
            }

        }

        // building the packet
        GameStateHeader header;
        header.sequenceNum = htonl(sequence++);
        header.matchState = htonl(g_matchState);
        header.numPlayers = htonl(activePlayers);
        header.numProjectiles = htonl(activeProjectiles);
        header.winnerID = htonl(g_winnerID);

        std::vector<char> pktData;
        pktData.insert(pktData.end(), reinterpret_cast<char*>(&header), reinterpret_cast<char*>(&header) + sizeof(header));

        {
            std::lock_guard<std::mutex> lk(g_stateMtx);

            // pack players 1st
            for (int i = 0; i < MAX_PLAYERS; i++) 
            {
                if (g_players[i].connected) 
                {
                    PlayerState ps;
                    ps.playerID = htonl(i);
                    strncpy_s(ps.name, g_players[i].name.c_str(), 15);
                    ps.x = g_players[i].x;
                    ps.y = g_players[i].y;
                    ps.aimAngle = g_players[i].aimAngle;
                    ps.hp = htonl((uint32_t)g_players[i].hp);
                    ps.kills = htonl((uint32_t)g_players[i].kills);
                    ps.justShot = g_players[i].justShot;
                    ps.justHit = g_players[i].justHit;
                    ps.shootCooldown = htonl((uint32_t)g_players[i].shootCooldown);
                    ps.isReady = g_players[i].isReady;

                    pktData.insert(pktData.end(), reinterpret_cast<char*>(&ps), reinterpret_cast<char*>(&ps) + sizeof(ps));
                }
            }

            // then pack proj
            for (int j = 0; j < MAX_PROJECTILES; j++) 
            {
                if (g_projectiles[j].active)
                {
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
            for (int i = 0; i < MAX_PLAYERS; i++) 
            {
                if (g_players[i].connected) 
                    sendto(g_serverUDPSocket, pktData.data(), (int)pktData.size(), 0, reinterpret_cast<sockaddr*>(&g_players[i].udpAddr), sizeof(g_players[i].udpAddr));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

int main()
{
    WSADATA wsaData;
    SecureZeroMemory(&wsaData, sizeof(wsaData));

    int errorCode = WSAStartup(WINSOCK_VERSION, &wsaData);
    if (NO_ERROR != errorCode)
    {
        std::cerr << "WSAStartup() failed." << std::endl;
        return errorCode;
    }

    // display to cmd for connecting
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0)
    {
        addrinfo hints{}, * res = nullptr;
        hints.ai_family = AF_INET; // IPv4
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(hostname, nullptr, &hints, &res) == 0) 
        {
            std::cout << "Local Hostname: " << hostname << std::endl;

            for (addrinfo* p = res; p != nullptr; p = p->ai_next) 
            {
                sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));

                // ignore the loopback address
                if (std::string(ipStr) != "127.0.0.1")
                    std::cout << "Server IP Address: " << ipStr << std::endl;
            }
            freeaddrinfo(res);
        }
    }

    std::string tcpPortStr = "27015";
    std::string udpPortStr = "27016";
    std::cout << "Listening on TCP Port: " << tcpPortStr << std::endl;
    std::cout << "Listening on UDP Port: " << udpPortStr << std::endl;

    // TCP for establishing link
    addrinfo tcpHints{}, * tcpInfo = nullptr;
    tcpHints.ai_family = AF_INET;
    tcpHints.ai_socktype = SOCK_STREAM;
    tcpHints.ai_protocol = IPPROTO_TCP;
    tcpHints.ai_flags = AI_PASSIVE;
    getaddrinfo(nullptr, tcpPortStr.c_str(), &tcpHints, &tcpInfo);

    SOCKET listenerSocket = socket(tcpHints.ai_family, tcpHints.ai_socktype, tcpHints.ai_protocol);
    errorCode = bind(listenerSocket, tcpInfo->ai_addr, (int)tcpInfo->ai_addrlen);
    if (errorCode != NO_ERROR)
    {
        std::cerr << "bind() TCP failed: " << WSAGetLastError() << std::endl;
        closesocket(listenerSocket);
        WSACleanup();
        return RETURN_CODE_2;
    }
    listen(listenerSocket, SOMAXCONN);
    freeaddrinfo(tcpInfo);

    // UDP
    addrinfo udpHints{}, * udpInfo = nullptr;
    udpHints.ai_family = AF_INET;
    udpHints.ai_socktype = SOCK_DGRAM;
    udpHints.ai_protocol = IPPROTO_UDP;
    udpHints.ai_flags = AI_PASSIVE;
    getaddrinfo(nullptr, udpPortStr.c_str(), &udpHints, &udpInfo);

    g_serverUDPSocket = socket(udpHints.ai_family, udpHints.ai_socktype, udpHints.ai_protocol);
    errorCode = bind(g_serverUDPSocket, udpInfo->ai_addr, (int)udpInfo->ai_addrlen);
    if (errorCode != NO_ERROR)
    {
        std::cerr << "bind() UDP failed: " << WSAGetLastError() << std::endl;
        closesocket(g_serverUDPSocket);
        WSACleanup();
        return RETURN_CODE_3;
    }
    freeaddrinfo(udpInfo);

    std::cout << "Server running on TCP: " << tcpPortStr << ", UDP: " << udpPortStr << "\n";

    std::thread gameThread(serverGameLoop);
    std::thread udpThread(udpReceiverThread);

    while (g_running) 
    {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenerSocket, (sockaddr*)&clientAddr, &clientAddrLen);

        if (clientSocket != INVALID_SOCKET)
        {
            char cmdBuf[1];
            if (recvAll(clientSocket, cmdBuf, 1) && cmdBuf[0] == REQ_JOIN)
            {
                char nameBuf[16];
                uint32_t clientIP_net{};
                uint16_t clientPort_net{};

                if (recvAll(clientSocket, nameBuf, 16) &&
                    recvAll(clientSocket, &clientIP_net, 4) &&
                    recvAll(clientSocket, &clientPort_net, 2))
                {

                    int assignedID = -1;
                    {
                        std::lock_guard<std::mutex> lk(g_stateMtx);

                        for (int i = 0; i < MAX_PLAYERS; i++) 
                        {
                            if (!g_players[i].connected) 
                            {
                                assignedID = i;
                                break;
                            }
                        }

                        if (assignedID != -1) 
                        {
                            g_players[assignedID].udpAddr.sin_family = AF_INET;
                            g_players[assignedID].udpAddr.sin_addr = clientAddr.sin_addr;
                            g_players[assignedID].udpAddr.sin_port = clientPort_net;
                            g_players[assignedID].tcpSocket = clientSocket;
                            g_players[assignedID].connected = true;

                            nameBuf[15] = '\0';
                            g_players[assignedID].name = std::string(nameBuf);

                            char ipStr[INET_ADDRSTRLEN];
                            inet_ntop(AF_INET, &clientIP_net, ipStr, INET_ADDRSTRLEN);
                            std::cout << "[Server] Player " << assignedID << " joined from " << ipStr << ":" << ntohs(clientPort_net) << std::endl;

                            int spawnMarker = assignedID + 2;
                            bool foundSpawn = false;

                            for (int row = 0; row < MAP_HEIGHT; row++)
                            {
                                for (int col = 0; col < MAP_WIDTH; col++) 
                                {
                                    if (ARENA_MAP[row][col] == spawnMarker) 
                                    {

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
                            if (!foundSpawn) 
                            {
                                g_players[assignedID].x = 0.0f;
                                g_players[assignedID].y = 0.0f;
                                g_players[assignedID].aimAngle = 0.0f;
                            }

                            g_players[assignedID].hp = MAX_HP;

                            g_players[assignedID].up = false;
                            g_players[assignedID].down = false;
                            g_players[assignedID].left = false;
                            g_players[assignedID].right = false;
                            g_players[assignedID].space = false;
                        }
                    }

                    char idMsg = static_cast<char>(assignedID);
                    sendAll(clientSocket, &idMsg, 1);

                    if (assignedID != -1)
                    {
                        std::cout << "[Server] Player " << assignedID << " joined.\n";

                        std::thread clientTCPThread([clientSocket, assignedID]()
                        {
                            char cmdBuf[1];
                            while (recv(clientSocket, cmdBuf, 1, 0) > 0)
                            {
                                if (cmdBuf[0] == REQ_TOGGLE_READY)
                                {
                                    std::lock_guard<std::mutex> lk(g_stateMtx);
                                    g_players[assignedID].isReady = !g_players[assignedID].isReady;
                                    std::cout << "[Server] Player " << assignedID << " is now "
                                        << (g_players[assignedID].isReady ? "READY" : "UNREADY") << "\n";
                                }
                                else if (cmdBuf[0] == REQ_CHEAT_WIN)
                                {
                                    std::lock_guard<std::mutex> lk(g_stateMtx);

                                    if (g_matchState == 1)
                                    {
                                        g_matchState = 2;
                                        g_winnerID = assignedID;
                                        g_gameOverTimer = gameOverTimer;  // 5 sec
                                        std::cout << "[Server] Player " << assignedID << " used the INSTA-WIN cheat!\n";
                                    }
                                }
                                else if (cmdBuf[0] == REQ_CHAT)
                                {
                                    uint16_t msgLenNet;
                                    if (recvAll(clientSocket, &msgLenNet, 2))
                                    {
                                        uint16_t msgLen = ntohs(msgLenNet);
                                        std::vector<char> msgBuf(msgLen + 1, '\0');
                                        if (recvAll(clientSocket, msgBuf.data(), msgLen))
                                        {
                                            std::string senderName;
                                            {
                                                std::lock_guard<std::mutex> lk(g_stateMtx);
                                                senderName = g_players[assignedID].name;
                                            }
                                            std::string fullMsg = senderName + ": " + msgBuf.data();
                                            uint16_t fullLen = (uint16_t)fullMsg.length();
                                            uint16_t fullLenNet = htons(fullLen);

                                            std::vector<char> broadcastPkt;
                                            broadcastPkt.push_back(REQ_CHAT);
                                            broadcastPkt.insert(broadcastPkt.end(), reinterpret_cast<char*>(&fullLenNet), reinterpret_cast<char*>(&fullLenNet) + 2);
                                            broadcastPkt.insert(broadcastPkt.end(), fullMsg.begin(), fullMsg.end());

                                            std::lock_guard<std::mutex> lk(g_stateMtx);
                                            for (int i = 0; i < MAX_PLAYERS; i++)
                                            {
                                                if (g_players[i].connected && g_players[i].tcpSocket != INVALID_SOCKET)
                                                {
                                                    sendAll(g_players[i].tcpSocket, broadcastPkt.data(), (int)broadcastPkt.size());
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            std::cout << "[Server] Player " << assignedID << " disconnected.\n";
                            std::lock_guard<std::mutex> lk(g_stateMtx);
                            g_players[assignedID].connected = false;
                            g_players[assignedID].tcpSocket = INVALID_SOCKET;
                            g_players[assignedID].x = 10.0f;
                            g_players[assignedID].up = g_players[assignedID].down = g_players[assignedID].left = g_players[assignedID].right = false;
                            closesocket(clientSocket);
                        });
                        clientTCPThread.detach();

                    }
                    else 
                    {
                        std::cout << "[Server] Rejected connection (Server Full).\n";
                        closesocket(clientSocket);
                    }
                }
            }
            else
                closesocket(clientSocket);
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