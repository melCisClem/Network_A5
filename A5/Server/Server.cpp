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

// Player Data Structure
struct Player {
    sockaddr_in udpAddr{};
    bool connected = false;
    // FIX 2: Default to 10.0f so they spawn off-screen when not connected!
    float x = 10.0f, y = 10.0f;
    bool up = false, down = false, left = false, right = false;
};

std::mutex g_stateMtx;
Player g_players[2]; // Slot 0 and Slot 1
float g_moveSpeed = 0.02f;

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
            if (id < 2) {
                std::lock_guard<std::mutex> lk(g_stateMtx);
                if (g_players[id].connected) {
                    g_players[id].up = pkt->w_pressed;
                    g_players[id].down = pkt->s_pressed;
                    g_players[id].left = pkt->a_pressed;
                    g_players[id].right = pkt->d_pressed;
                }
            }
        }
    }
}

// 60 Tick Game Loop
void serverGameLoop() {
    uint32_t sequence = 0;
    std::cout << "[Server] Game loop started.\n";

    while (g_running) {
        GameStatePacket pkt;
        pkt.sequenceNum = htonl(sequence++);

        // 1. Update positions and build packet
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            for (int i = 0; i < 2; i++) {
                if (g_players[i].connected) {
                    if (g_players[i].up)    g_players[i].y += g_moveSpeed;
                    if (g_players[i].down)  g_players[i].y -= g_moveSpeed;
                    if (g_players[i].left)  g_players[i].x -= g_moveSpeed;
                    if (g_players[i].right) g_players[i].x += g_moveSpeed;
                }
            }

            pkt.p0X = g_players[0].x; pkt.p0Y = g_players[0].y;
            pkt.p1X = g_players[1].x; pkt.p1Y = g_players[1].y;
        }

        // 2. Broadcast to all connected clients
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            for (int i = 0; i < 2; i++) {
                if (g_players[i].connected) {
                    sendto(g_serverUDPSocket, reinterpret_cast<char*>(&pkt), sizeof(pkt), 0,
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
                        if (!g_players[0].connected) assignedID = 0;
                        else if (!g_players[1].connected) assignedID = 1;

                        if (assignedID != -1) {
                            g_players[assignedID].udpAddr.sin_family = AF_INET;
                            g_players[assignedID].udpAddr.sin_addr.s_addr = clientIP_net;
                            g_players[assignedID].udpAddr.sin_port = clientPort_net;
                            g_players[assignedID].connected = true;

                            // Teleport them onto the screen
                            g_players[assignedID].x = (assignedID == 0) ? -0.5f : 0.5f;
                            g_players[assignedID].y = 0.0f;
                            g_players[assignedID].up = g_players[assignedID].down = g_players[assignedID].left = g_players[assignedID].right = false;
                        }
                    }

                    char idMsg = static_cast<char>(assignedID);
                    sendAll(clientSocket, &idMsg, 1);

                    if (assignedID != -1) {
                        std::cout << "[Server] Player " << assignedID << " joined.\n";

                        // FIX 1: Spin up a background thread to watch this player's TCP connection!
                        // This frees up the main loop to accept Player 1 immediately.
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