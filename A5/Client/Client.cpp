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
#include <map>

#include "../NetworkData.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../Audiomanager.h"

#define REQ_JOIN (unsigned char)0x06
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

std::atomic<bool> g_running{ true };
SOCKET g_udpSocket = INVALID_SOCKET;

std::mutex g_stateMtx;
uint32_t g_lastSeq = 0;
struct ClientPlayer {
    float x, y;
    float aimAngle;
    int hp;
    int shootCooldown;
}; 
std::map<uint32_t, ClientPlayer> g_renderPlayers;
std::vector<ProjectileState> g_renderProjectiles;

AudioManager* g_audio = nullptr;
constexpr const char* ingame_BGM_audio = "bgm.wav";
constexpr const char* shooting_audio = "shoot.mp3";
constexpr const char* explosion_audio = "explode.mp3";
constexpr const char* mainmenu_BGM_audio = "mm_bgm.wav";

bool g_isPaused = false;
bool g_escWasPressed = false;
float g_currentVolume = 0.5f; // 0.0 to 1.0

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

void udpReceiverThread() 
{
    std::vector<char> buf(UDPPACKET_BUFFER_SIZE);
    sockaddr_in from{};
    int fromLen = sizeof(from);

    while (g_running) 
    {
        int r = recvfrom(g_udpSocket, buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fromLen);

        if (r >= sizeof(GameStateHeader)) 
        {
            auto* header = reinterpret_cast<GameStateHeader*>(buf.data());
            uint32_t seq = ntohl(header->sequenceNum);
            uint32_t numPlayers = ntohl(header->numPlayers);
            uint32_t numProjs = ntohl(header->numProjectiles);

            int expectedSize = sizeof(GameStateHeader) + (numPlayers * sizeof(PlayerState)) + (numProjs * sizeof(ProjectileState));
            if (r >= expectedSize) 
            {
                std::lock_guard<std::mutex> lock(g_stateMtx);
                if (seq > g_lastSeq) 
                {
                    g_lastSeq = seq;

                    char* payloadPtr = buf.data() + sizeof(GameStateHeader);

                    // players
                    std::map<uint32_t, ClientPlayer> activePlayersThisTick;
                    for (uint32_t i = 0; i < numPlayers; i++) 
                    {
                        auto* ps = reinterpret_cast<PlayerState*>(payloadPtr);
                        uint32_t pID = ntohl(ps->playerID);
                        activePlayersThisTick[pID].x = ps->x;
                        activePlayersThisTick[pID].y = ps->y;
                        activePlayersThisTick[pID].aimAngle = ps->aimAngle;
                        activePlayersThisTick[pID].hp = (int)ntohl(ps->hp);
                        activePlayersThisTick[pID].shootCooldown = (int)ntohl(ps->shootCooldown);

                        if (g_audio)
                        {
                            if (ps->justShot) g_audio->PlaySFX(shooting_audio);
                            if (ps->justHit) g_audio->PlaySFX(explosion_audio);
                        }

                        payloadPtr += sizeof(PlayerState);
                    }
                    g_renderPlayers = activePlayersThisTick;

                    // projectiles
                    uint32_t numProjs = ntohl(header->numProjectiles);
                    g_renderProjectiles.clear(); // clean last frames projectiles
                    for (uint32_t i = 0; i < numProjs; i++) 
                    {
                        auto* proj = reinterpret_cast<ProjectileState*>(payloadPtr);
                        g_renderProjectiles.push_back(*proj);
                        payloadPtr += sizeof(ProjectileState);
                    }
                }
            }
        }
    }
}

void drawMap() 
{
    float cellW = 2.0f / MAP_WIDTH;
    float cellH = 2.0f / MAP_HEIGHT;

    glBegin(GL_QUADS);
    glColor3f(0.3f, 0.3f, 0.3f); // Dark Gray Walls

    for (int row = 0; row < MAP_HEIGHT; row++) 
    {
        for (int col = 0; col < MAP_WIDTH; col++) 
        {
            if (ARENA_MAP[row][col] == 1) {
                // calc the bottom left corner of the grid cell
                float x1 = -1.0f + (col * cellW);
                float y1 = -1.0f + (row * cellH);

                // calc the top-right corner
                float x2 = x1 + cellW;
                float y2 = y1 + cellH;

                glVertex2f(x1, y1); // Bottom-left
                glVertex2f(x2, y1); // Bottom-right
                glVertex2f(x2, y2); // Top-right
                glVertex2f(x1, y2); // Top-left
            }
        }
    }
    glEnd();
}

// draws tank, hp bar, shot cooldown bar
void drawTank(float x, float y, float r, float g, float b, float facingAngle, int hp, int cooldown, bool isLocalPlayer) 
{
    float width = tank_width;
    float height = tank_height;
    float gunLength = tank_gunLength;
    float outline_thickness = tank_outline_thickness;
    float hp_thickness = tank_hp_thickness;

    glPushMatrix();
    glTranslatef(x, y, 0.0f); 

    // Draw HP n Cooldown bar
    glPushMatrix();
    glTranslatef(0.0f, height + 0.04f, 0.0f); // hp bar above tank body

    float BarWidth = width * 0.9f;
    float BarHeight = hp_thickness;

    // Draw Dark Background
    glBegin(GL_QUADS);
    glColor3f(0.2f, 0.2f, 0.2f);
    glVertex2f(-BarWidth, -BarHeight);
    glVertex2f(BarWidth, -BarHeight);
    glVertex2f(BarWidth, BarHeight);
    glVertex2f(-BarWidth, BarHeight);
    glEnd();

    // Draw Red HP Bar
    float hpPct = fmax(0.0f, (float)hp / (float)MAX_HP);
    float currentWidth = -BarWidth + (2.0f * BarWidth * hpPct);

    glBegin(GL_QUADS);
    glColor3f(0.9f, 0.1f, 0.1f);
    glVertex2f(-BarWidth, -BarHeight);
    glVertex2f(currentWidth, -BarHeight);
    glVertex2f(currentWidth, BarHeight);
    glVertex2f(-BarWidth, BarHeight);
    glEnd();

    // Draw Shoot Cooldown bar
    float maxCD = (float)tank_shootCooldown;
    float cooldownPct = (maxCD - (float)cooldown) / maxCD;

    if (cooldownPct < 0.0f) cooldownPct = 0.0f;
    if (cooldownPct > 1.0f) cooldownPct = 1.0f;

    float cdFill = -BarWidth + (2.0f * BarWidth * cooldownPct);
    float cdY = -0.01f; // Positioned below HP bar
    float cdHeight = hp_thickness;

    // Background
    glColor3f(0.1f, 0.1f, 0.1f); // Dark gray
    glBegin(GL_QUADS);
    glVertex2f(-BarWidth, cdY);
    glVertex2f(BarWidth, cdY);
    glVertex2f(BarWidth, cdY + cdHeight);
    glVertex2f(-BarWidth, cdY + cdHeight);
    glEnd();

    if (cooldown > 0)
        glColor3f(0.3f, 0.8f, 1.0f); // Loading Cyan
    else
        glColor3f(0.2f, 0.8f, 0.2f); // Ready Green

    glBegin(GL_QUADS);
    glVertex2f(-BarWidth, cdY);
    glVertex2f(cdFill, cdY);
    glVertex2f(cdFill, cdY + cdHeight);
    glVertex2f(-BarWidth, cdY + cdHeight);
    glEnd();

    glPopMatrix();

    
    glRotatef(facingAngle, 0.0f, 0.0f, 1.0f);

    // draw gun barrel
    glLineWidth(4.0f);
    glBegin(GL_LINES);
    glColor3f(0.6f, 0.6f, 0.6f);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(width + gunLength, 0.0f);
    glEnd();
    glLineWidth(1.0f);

    // draw Body
    glBegin(GL_QUADS);
    glColor3f(r, g, b);
    glVertex2f(-width, -height); // Bottom-left
    glVertex2f(width, -height); // Bottom-right
    glVertex2f(width, height); // Top-right
    glVertex2f(-width, height); // Top-left
    glEnd();

    // draw outline only for local player
    if (isLocalPlayer) {
        glLineWidth(outline_thickness);
        glBegin(GL_LINE_LOOP);
        glColor3f(1.0f, 1.0f, 1.0f);

        float offset = 0.001f;
        glVertex2f(-width - offset, -height - offset);
        glVertex2f(width + offset, -height - offset);
        glVertex2f(width + offset, height + offset);
        glVertex2f(-width - offset, height + offset);
        glEnd();
        glLineWidth(1.0f);
    }

    glPopMatrix();
}

void drawProjectile(float x, float y) 
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glBegin(GL_POLYGON);
    glColor3f(1.0f, 1.0f, 0.0f); // yellow
    for (int i = 0; i < 360; i += 30) 
    {
        float theta = i * 3.14159f / 180.0f;
        glVertex2f(0.02f * cos(theta), 0.02f * sin(theta));
    }
    glEnd();
    glPopMatrix();
}

void drawPauseMenu() 
{
    // Dark Overlay
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f); glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f); glVertex2f(-1.0f, 1.0f);
    glEnd();
    glDisable(GL_BLEND);

    // Pause Icon
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(-0.08f, 0.2f); glVertex2f(-0.03f, 0.2f); glVertex2f(-0.03f, 0.4f); glVertex2f(-0.08f, 0.4f);
    glVertex2f(0.03f, 0.2f); glVertex2f(0.08f, 0.2f); glVertex2f(0.08f, 0.4f); glVertex2f(0.03f, 0.4f);
    glEnd();

    // Volume Bar Background
    float barY = -0.1f;
    float barHalfWidth = 0.4f;
    glColor3f(0.3f, 0.3f, 0.3f);
    glBegin(GL_QUADS);
    glVertex2f(-barHalfWidth, barY - 0.02f); glVertex2f(barHalfWidth, barY - 0.02f);
    glVertex2f(barHalfWidth, barY + 0.02f); glVertex2f(-barHalfWidth, barY + 0.02f);
    glEnd();

    // Volume Bar Fill
    float fillRight = -barHalfWidth + (g_currentVolume * (barHalfWidth * 2.0f));
    glColor3f(0.2f, 0.8f, 0.2f);
    glBegin(GL_QUADS);
    glVertex2f(-barHalfWidth, barY - 0.02f); glVertex2f(fillRight, barY - 0.02f);
    glVertex2f(fillRight, barY + 0.02f); glVertex2f(-barHalfWidth, barY + 0.02f);
    glEnd();

    // Circular Buttons with a small gap
    float buttonRadius = 0.05f;
    float gap = 0.08f;

    // Minus Button (Left)
    glColor3f(0.8f, 0.2f, 0.2f);
    float minusCenterX = -barHalfWidth - gap;
    glBegin(GL_POLYGON);
    for (int i = 0; i < 360; i += 20) 
    {
        float theta = i * 3.14159f / 180.0f;
        glVertex2f(minusCenterX + buttonRadius * cos(theta), barY + buttonRadius * sin(theta));
    }
    glEnd();

    // Plus Button (Right)
    glColor3f(0.2f, 0.8f, 0.2f);
    float plusCenterX = barHalfWidth + gap;
    glBegin(GL_POLYGON);
    for (int i = 0; i < 360; i += 20) 
    {
        float theta = i * 3.14159f / 180.0f;
        glVertex2f(plusCenterX + buttonRadius * cos(theta), barY + buttonRadius * sin(theta));
    }
    glEnd();

    // Quit Button (A red square with a white X)
    float quitY = -0.4f; // Positioned below the volume bar
    float quitSize = 0.06f;

    // Red Background
    glColor3f(0.8f, 0.2f, 0.2f);
    glBegin(GL_QUADS);
    glVertex2f(-quitSize, quitY - quitSize); glVertex2f(quitSize, quitY - quitSize);
    glVertex2f(quitSize, quitY + quitSize); glVertex2f(-quitSize, quitY + quitSize);
    glEnd();

    // White X
    glColor3f(1.0f, 1.0f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(-0.03f, quitY - 0.03f); glVertex2f(0.03f, quitY + 0.03f);
    glVertex2f(0.03f, quitY - 0.03f); glVertex2f(-0.03f, quitY + 0.03f);
    glEnd();
    glLineWidth(1.0f);
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
    char hostname[NI_MAXHOST] = {};
    gethostname(hostname, sizeof(hostname));

    addrinfo localHints{}, * res = nullptr;
    localHints.ai_family = AF_INET;
    localHints.ai_socktype = SOCK_STREAM;
    getaddrinfo(hostname, nullptr, &localHints, &res);

    std::string detectedIP = "127.0.0.1";
    bool found = false;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next)
    {
        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
        std::string currentIP(ipStr);
        if (currentIP != "127.0.0.1" && !found)
        {
            detectedIP = currentIP;
            found = true;
        }
    }
    freeaddrinfo(res);
    res = nullptr;

    std::string serverIPStr;
    std::cout << "Enter Server IPv4 [Default: " << detectedIP << "]: ";
    std::string input;
    std::getline(std::cin, input);
    serverIPStr = input.empty() ? detectedIP : input;

    std::string tcpPortStr = "27015";
    uint16_t serverUDPPort = 27016;

    // TCP for establishing link
    addrinfo tcpHints{}, * tcpInfo = nullptr;
    tcpHints.ai_family = AF_INET;
    tcpHints.ai_socktype = SOCK_STREAM;
    tcpHints.ai_protocol = IPPROTO_TCP;
    tcpHints.ai_flags = AI_PASSIVE;
    getaddrinfo(serverIPStr.c_str(), tcpPortStr.c_str(), &tcpHints, &tcpInfo);

#ifdef _DEBUG
    std::cout << "[Client] Attempting to connect to " << serverIPStr << "..." << std::endl;
#endif
    SOCKET g_tcpSocket = socket(tcpHints.ai_family, tcpHints.ai_socktype, tcpHints.ai_protocol);
    errorCode = connect(g_tcpSocket, tcpInfo->ai_addr, (int)tcpInfo->ai_addrlen);
    if (SOCKET_ERROR == errorCode)
    {
        std::cerr << "Failed to connect to server TCP.\n";
        closesocket(g_tcpSocket);
        WSACleanup();
        return RETURN_CODE_3;
    }
    freeaddrinfo(tcpInfo);
    std::cout << "[Client] Successfully connected to server " << serverIPStr << std::endl;

    // UDP
    g_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in udpBind{};
    udpBind.sin_family = AF_INET;
    udpBind.sin_addr.s_addr = INADDR_ANY;
    udpBind.sin_port = 0; // auto
    errorCode = bind(g_udpSocket, (sockaddr*)&udpBind, sizeof(udpBind));
    if (errorCode != NO_ERROR)
    {
        std::cerr << "bind() UDP failed: " << WSAGetLastError() << std::endl;
        closesocket(g_tcpSocket);
        closesocket(g_udpSocket);
        WSACleanup();
        return RETURN_CODE_2;
    }

    // find out which port auto gave
    sockaddr_in localAddr{};
    int localLen = sizeof(localAddr);
    getsockname(g_udpSocket, (sockaddr*)&localAddr, &localLen);
    uint16_t myUDPPort = localAddr.sin_port;

    std::vector<char> msg;
    msg.push_back(REQ_JOIN);

    uint32_t zeroIp = 0;
    msg.insert(msg.end(), reinterpret_cast<char*>(&zeroIp), reinterpret_cast<char*>(&zeroIp) + 4);
    msg.insert(msg.end(), reinterpret_cast<char*>(&myUDPPort), reinterpret_cast<char*>(&myUDPPort) + 2);
    sendAll(g_tcpSocket, msg.data(), (int)msg.size());

    // wait for id from server
    char idRsp;
    if (!recvAll(g_tcpSocket, &idRsp, 1) || idRsp == -1) 
    {
        std::cerr << "Server is full or rejected connection.\n";
        return 1;
    }
    uint32_t myPlayerID = static_cast<uint32_t>(idRsp);
    std::cout << "[Client] Joined as Player " << myPlayerID << "\n";

    // setup server UDP info
    sockaddr_in serverUdpAddr{};
    serverUdpAddr.sin_family = AF_INET;
    inet_pton(AF_INET, serverIPStr.c_str(), &serverUdpAddr.sin_addr);
    serverUdpAddr.sin_port = htons(serverUDPPort);

    g_audio = new AudioManager();
    g_audio->PlayBGM(ingame_BGM_audio);

    std::thread tUDP(udpReceiverThread);

    if (!glfwInit()) 
        return -1;

    std::string title = "Player " + std::to_string(myPlayerID);
    GLFWwindow* window = glfwCreateWindow(600, 600, title.c_str(), NULL, NULL);
    if (!window) 
    { 
        glfwTerminate(); 
        return -1; 
    }
    glfwMakeContextCurrent(window);

    uint32_t inputSeq = 0;

    while (!glfwWindowShouldClose(window) && g_running) 
    {
        bool isFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;

        // pause menu
        bool escPressed = isFocused && (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
        if (escPressed && !g_escWasPressed)
            g_isPaused = !g_isPaused;
        g_escWasPressed = escPressed;

        if (g_isPaused && isFocused) 
        {
            if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) 
            {
                g_currentVolume += 0.01f;
                if (g_currentVolume > 1.0f) g_currentVolume = 1.0f;
                if (g_audio) g_audio->SetMasterVolume(g_currentVolume);
            }
            if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) 
            {
                g_currentVolume -= 0.01f;
                if (g_currentVolume < 0.0f) g_currentVolume = 0.0f;
                if (g_audio) g_audio->SetMasterVolume(g_currentVolume);
            }
        }

        // hide cursor in game show in pause menu
        if (g_isPaused) 
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        else
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

        static bool mouseWasPressed = false;
        bool mousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (g_isPaused && mousePressed) 
        {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);

            int width, height;
            glfwGetWindowSize(window, &width, &height);
            float mouseX = (float)((xpos / width) * 2.0 - 1.0);
            float mouseY = (float)(1.0 - (ypos / height) * 2.0);

            float barY = -0.1f;
            double buttonRadius_sq = 0.05f * 0.05f;
            float gap = 0.08f;
            float barHalfWidth = 0.4f;

            // check for vol control
            float volumeChange = 0.001f;
            double distMinus_sq = pow(mouseX - (-barHalfWidth - gap), 2) + pow(mouseY - barY, 2);
            if (distMinus_sq <= buttonRadius_sq) 
            {
                g_currentVolume = fmaxf(0.0f, g_currentVolume - volumeChange);
                if (g_audio) 
                    g_audio->SetMasterVolume(g_currentVolume);
            }
            double distPlus_sq = pow(mouseX - (barHalfWidth + gap), 2) + pow(mouseY - barY, 2);
            if (distPlus_sq <= buttonRadius_sq) 
            {
                g_currentVolume = fminf(1.0f, g_currentVolume + volumeChange);
                if (g_audio) 
                    g_audio->SetMasterVolume(g_currentVolume);
            }

            // check for quit button
            if (!mouseWasPressed) 
            {
                float quitY = -0.4f;
                float quitSize = 0.06f;
                if (mouseX >= -quitSize && mouseX <= quitSize && mouseY >= quitY - quitSize && mouseY <= quitY + quitSize)
                    g_running = false;
            }
        }
        mouseWasPressed = mousePressed;

        // send input data pkt
        InputPacket inputPkt;
        inputPkt.sequenceNum = htonl(inputSeq++);
        inputPkt.playerID = htonl(myPlayerID);
        
        inputPkt.w_pressed = !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);
        inputPkt.a_pressed = !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
        inputPkt.s_pressed = !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
        inputPkt.d_pressed = !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS);
        inputPkt.space_pressed = isFocused && (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
        inputPkt.aimAngle = 0.0f;

        sendto(g_udpSocket, reinterpret_cast<const char*>(&inputPkt), sizeof(inputPkt), 0, (sockaddr*)&serverUdpAddr, sizeof(serverUdpAddr));

        // render
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        drawMap();

        std::map<uint32_t, ClientPlayer> playersToDraw;
        std::vector<ProjectileState> projsToDraw;
        {
            std::lock_guard<std::mutex> lock(g_stateMtx);
            playersToDraw = g_renderPlayers;
            projsToDraw = g_renderProjectiles;
        }

        for (const auto& pair : playersToDraw) 
        {
            uint32_t id = pair.first;
            float px = pair.second.x;
            float py = pair.second.y;
            float pAngle = pair.second.aimAngle;
            int pHP = pair.second.hp;
            int pShootCD = pair.second.shootCooldown;

            float r = 0.2f, g = 0.2f, b = 0.2f;
            if (id % 4 == 0) r = 0.8f;                      // Player 0: Red
            else if (id % 4 == 1) g = 0.8f;                 // Player 1: Green
            else if (id % 4 == 2) b = 0.8f;                 // Player 2: Blue
            else if (id % 4 == 3) { r = 0.8f; g = 0.8f; }   // Player 3: Yellow

            bool isMe = (id == myPlayerID);
            drawTank(px, py, r, g, b, pAngle, pHP, pShootCD, isMe);
        }

        for (const auto& proj : projsToDraw)
            drawProjectile(proj.x, proj.y);

        if (g_isPaused)
            drawPauseMenu();

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
    delete g_audio;
    return 0;
}