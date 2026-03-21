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

#define REQ_JOIN (unsigned char)0x06

// The Game State Packet
#pragma pack(push, 1)
struct GameStatePacket {
    uint32_t sequenceNum;
    float playerX;
    float playerY;
};
#pragma pack(pop)

std::atomic<bool> g_running{ true };
SOCKET g_serverUDPSocket = INVALID_SOCKET;

// Shared Game State
float g_circleX = 0.0f;
float g_circleY = 0.0f;
float g_velocityX = 0.01f;

// Connected Client Data
std::mutex g_clientMtx;
sockaddr_in g_clientUDPAddr{};
bool g_clientConnected = false;

// Send exactly n bytes
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

// Fixed-step game loop (~60 ticks per second)
void serverGameLoop() {
    uint32_t sequence = 0;
    std::cout << "[Server] Game loop started.\n";

    while (g_running) {
        // 1. Update Game Logic (Bounce circle left and right)
        g_circleX += g_velocityX;
        if (g_circleX > 0.8f || g_circleX < -0.8f) {
            g_velocityX = -g_velocityX; 
        }

        // 2. Build Packet
        GameStatePacket pkt;
        pkt.sequenceNum = htonl(sequence++);
        pkt.playerX = g_circleX;
        pkt.playerY = g_circleY;

        // 3. Broadcast to client (Fire and Forget)
        {
            std::lock_guard<std::mutex> lk(g_clientMtx);
            if (g_clientConnected) {
                sendto(g_serverUDPSocket, reinterpret_cast<char*>(&pkt), sizeof(pkt), 0, 
                       reinterpret_cast<sockaddr*>(&g_clientUDPAddr), sizeof(g_clientUDPAddr));
            }
        }

        // 4. Sleep to maintain tick rate
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::string tcpPortStr = "27015";
    std::string udpPortStr = "27016";

    // TCP Setup
    addrinfo tcpHints{}, *tcpInfo = nullptr;
    tcpHints.ai_family = AF_INET;
    tcpHints.ai_socktype = SOCK_STREAM;
    tcpHints.ai_protocol = IPPROTO_TCP;
    tcpHints.ai_flags = AI_PASSIVE;
    getaddrinfo(nullptr, tcpPortStr.c_str(), &tcpHints, &tcpInfo);
    
    SOCKET listenerSocket = socket(tcpHints.ai_family, tcpHints.ai_socktype, tcpHints.ai_protocol);
    bind(listenerSocket, tcpInfo->ai_addr, (int)tcpInfo->ai_addrlen);
    listen(listenerSocket, SOMAXCONN);
    freeaddrinfo(tcpInfo);

    // UDP Setup
    addrinfo udpHints{}, *udpInfo = nullptr;
    udpHints.ai_family = AF_INET;
    udpHints.ai_socktype = SOCK_DGRAM;
    udpHints.ai_protocol = IPPROTO_UDP;
    udpHints.ai_flags = AI_PASSIVE;
    getaddrinfo(nullptr, udpPortStr.c_str(), &udpHints, &udpInfo);
    
    g_serverUDPSocket = socket(udpHints.ai_family, udpHints.ai_socktype, udpHints.ai_protocol);
    bind(g_serverUDPSocket, udpInfo->ai_addr, (int)udpInfo->ai_addrlen);
    freeaddrinfo(udpInfo);

    std::cout << "Server running on TCP: " << tcpPortStr << ", UDP: " << udpPortStr << "\n";
    std::cout << "Waiting for client connection...\n";

    // Start game loop in background
    std::thread gameThread(serverGameLoop);

    // Main thread accepts connections
    while (g_running) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenerSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        
        if (clientSocket != INVALID_SOCKET) {
            std::cout << "[Server] Client connected via TCP.\n";
            char cmdBuf[1];
            
            // Wait for commands
            while (recvAll(clientSocket, cmdBuf, 1)) {
                unsigned char cmd = static_cast<unsigned char>(cmdBuf[0]);
                
                if (cmd == REQ_JOIN) {
                    uint32_t clientIP_net{};
                    uint16_t clientPort_net{};
                    
                    if (recvAll(clientSocket, &clientIP_net, 4) && recvAll(clientSocket, &clientPort_net, 2)) {
                        std::lock_guard<std::mutex> lk(g_clientMtx);
                        g_clientUDPAddr.sin_family = AF_INET;
                        g_clientUDPAddr.sin_addr.s_addr = clientIP_net;
                        g_clientUDPAddr.sin_port = clientPort_net;
                        g_clientConnected = true;
                        std::cout << "[Server] Client joined game. Broadcasting UDP to port " << ntohs(clientPort_net) << "\n";
                    }
                }
            }
            std::cout << "[Server] Client disconnected.\n";
            {
                std::lock_guard<std::mutex> lk(g_clientMtx);
                g_clientConnected = false;
            }
            closesocket(clientSocket);
        }
    }

    g_running = false;
    gameThread.join();
    closesocket(listenerSocket);
    closesocket(g_serverUDPSocket);
    WSACleanup();
    return 0;
}
