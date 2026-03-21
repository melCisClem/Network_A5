/* Start Header
*****************************************************************/
/*!
\file Client.cpp
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
#pragma comment(lib, "opengl32.lib") 

#include <GLFW/glfw3.h> 

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>
#include <string>

#include "../NetworkData.h"

#define REQ_JOIN (unsigned char)0x06

std::atomic<bool> g_running{ true };
SOCKET g_udpSocket = INVALID_SOCKET;

// Render State
std::mutex g_stateMtx;
float g_p0X = 0.0f, g_p0Y = 0.0f;
float g_p1X = 0.0f, g_p1Y = 0.0f;
uint32_t g_lastSeq = 0;

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

void udpReceiverThread() {
    std::vector<char> buf(sizeof(GameStatePacket));
    sockaddr_in from{};
    int fromLen = sizeof(from);

    while (g_running) {
        int r = recvfrom(g_udpSocket, buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fromLen);
        if (r == sizeof(GameStatePacket)) {
            auto* pkt = reinterpret_cast<GameStatePacket*>(buf.data());
            uint32_t seq = ntohl(pkt->sequenceNum);

            std::lock_guard<std::mutex> lock(g_stateMtx);
            if (seq > g_lastSeq) {
                g_p0X = pkt->p0X; g_p0Y = pkt->p0Y;
                g_p1X = pkt->p1X; g_p1Y = pkt->p1Y;
                g_lastSeq = seq;
            }
        }
    }
}

void drawCircle(float x, float y, float radius, float r, float g, float b) {
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glBegin(GL_POLYGON);
    glColor3f(r, g, b);
    for (int i = 0; i < 360; i += 10) {
        float theta = i * 3.14159f / 180.0f;
        glVertex2f(radius * cos(theta), radius * sin(theta));
    }
    glEnd();
    glPopMatrix();
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::string serverIPStr = "127.0.0.1";
    std::string tcpPortStr = "27015";
    uint16_t serverUDPPort = 27016;

    // TCP Connect
    addrinfo tcpHints{}, * tcpInfo = nullptr;
    tcpHints.ai_family = AF_INET;
    tcpHints.ai_socktype = SOCK_STREAM;
    getaddrinfo(serverIPStr.c_str(), tcpPortStr.c_str(), &tcpHints, &tcpInfo);

    SOCKET g_tcpSocket = socket(tcpHints.ai_family, tcpHints.ai_socktype, tcpHints.ai_protocol);
    if (connect(g_tcpSocket, tcpInfo->ai_addr, (int)tcpInfo->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to server TCP.\n";
        return 1;
    }
    freeaddrinfo(tcpInfo);

    // UDP Bind (Port 0 lets the OS pick an available port automatically)
    g_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in udpBind{};
    udpBind.sin_family = AF_INET;
    udpBind.sin_addr.s_addr = INADDR_ANY;
    udpBind.sin_port = 0; // <--- MAGIC HAPPENS HERE
    bind(g_udpSocket, (sockaddr*)&udpBind, sizeof(udpBind));

    // Find out which port the OS actually gave us
    sockaddr_in localAddr{};
    int localLen = sizeof(localAddr);
    getsockname(g_udpSocket, (sockaddr*)&localAddr, &localLen);
    uint16_t myUDPPort = localAddr.sin_port;

    // Send REQ_JOIN with our IP and Port
    std::vector<char> msg;
    msg.push_back(REQ_JOIN);
    in_addr myIp; inet_pton(AF_INET, "127.0.0.1", &myIp);
    msg.insert(msg.end(), reinterpret_cast<char*>(&myIp.s_addr), reinterpret_cast<char*>(&myIp.s_addr) + 4);
    msg.insert(msg.end(), reinterpret_cast<char*>(&myUDPPort), reinterpret_cast<char*>(&myUDPPort) + 2);
    sendAll(g_tcpSocket, msg.data(), (int)msg.size());

    // Wait for Server to assign us an ID
    char idRsp;
    if (!recvAll(g_tcpSocket, &idRsp, 1) || idRsp == -1) {
        std::cerr << "Server is full or rejected connection.\n";
        return 1;
    }
    uint32_t myPlayerID = static_cast<uint32_t>(idRsp);
    std::cout << "[Client] Joined as Player " << myPlayerID << "\n";

    // Setup Server UDP info
    sockaddr_in serverUdpAddr{};
    serverUdpAddr.sin_family = AF_INET;
    inet_pton(AF_INET, serverIPStr.c_str(), &serverUdpAddr.sin_addr);
    serverUdpAddr.sin_port = htons(serverUDPPort);

    std::thread tUDP(udpReceiverThread);

    if (!glfwInit()) return -1;

    // Change window title based on ID
    std::string title = "Player " + std::to_string(myPlayerID);
    GLFWwindow* window = glfwCreateWindow(600, 600, title.c_str(), NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    uint32_t inputSeq = 0;

    while (!glfwWindowShouldClose(window) && g_running) {

        // --- WINDOW FOCUS CHECK ---
        bool isFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;

        InputPacket inputPkt;
        inputPkt.sequenceNum = htonl(inputSeq++);
        inputPkt.playerID = htonl(myPlayerID); // Tell server who we are!

        // Only read inputs if the window is currently selected by the user
        inputPkt.w_pressed = isFocused && (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);
        inputPkt.a_pressed = isFocused && (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
        inputPkt.s_pressed = isFocused && (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
        inputPkt.d_pressed = isFocused && (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS);

        sendto(g_udpSocket, reinterpret_cast<const char*>(&inputPkt), sizeof(inputPkt), 0,
            (sockaddr*)&serverUdpAddr, sizeof(serverUdpAddr));

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        float p0x, p0y, p1x, p1y;
        {
            std::lock_guard<std::mutex> lock(g_stateMtx);
            p0x = g_p0X; p0y = g_p0Y;
            p1x = g_p1X; p1y = g_p1Y;
        }

        // Draw Player 0 (Green) and Player 1 (Blue)
        drawCircle(p0x, p0y, 0.15f, 0.2f, 0.8f, 0.2f);
        drawCircle(p1x, p1y, 0.15f, 0.2f, 0.4f, 0.9f);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    g_running = false;
    shutdown(g_tcpSocket, SD_BOTH);
    closesocket(g_tcpSocket);
    closesocket(g_udpSocket);
    tUDP.join();
    glfwTerminate();
    WSACleanup();
    return 0;
}