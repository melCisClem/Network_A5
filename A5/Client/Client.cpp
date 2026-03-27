/* Start Header
*****************************************************************/
/*!
\file Client.cpp
\authors
\   Lai Jun Jie Clement (junjieclement.lai@digipen.edu)
\   Aryan bin Mohamed Isran (aryan.b@digipen.edu)
\   Lee Hwee Min (l.hweemin@digipen.edu)
\date 25/03/2026
\brief
\   the client file
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
#include <algorithm>

#include "../NetworkData.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../Audiomanager.h"

#include "../utils.h"

#define REQ_JOIN (unsigned char)0x06
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

AudioManager* g_audio = nullptr;
constexpr const char* ingame_BGM_audio = "bgm.wav";
constexpr const char* shooting_audio = "shoot.mp3";
constexpr const char* explosion_audio = "explode.mp3";
constexpr const char* mainmenu_BGM_audio = "mm_bgm.wav";

std::atomic<bool> g_isTyping{ false };
std::atomic<bool> g_isConnected{ false };
std::thread g_tUDP;
std::thread g_tTCP;
std::string g_currentChatInput = "";
std::vector<std::string> g_chatMessages;
std::mutex g_chatMtx;
std::atomic<float> g_chatTimer{ 0.0f };
bool g_enterWasPressed = false;
bool g_backspaceWasPressed = false;


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

void charCallback(GLFWwindow* window, unsigned int codepoint)
{
    if (g_isTyping && codepoint < 128)
        g_currentChatInput += (char)codepoint;
}

void tcpReceiverThread()
{
    char cmdBuf[1];
    while (g_running && g_isConnected)
    {
        if (recv(g_tcpSocket, cmdBuf, 1, 0) > 0)
        {
            if (cmdBuf[0] == REQ_CHAT)
            {
                uint16_t fullLenNet;
                if (recvAll(g_tcpSocket, &fullLenNet, 2))
                {
                    uint16_t fullLen = ntohs(fullLenNet);
                    std::vector<char> fullMsgBuf(fullLen + 1, '\0');
                    if (recvAll(g_tcpSocket, fullMsgBuf.data(), fullLen))
                    {
                        std::lock_guard<std::mutex> lock(g_chatMtx);
                        g_chatMessages.push_back(fullMsgBuf.data());
                        if (g_chatMessages.size() > 5)
                            g_chatMessages.erase(g_chatMessages.begin());
                        
                        g_chatTimer = 10.0f; // Reset timer on new message
                    }
                }
            }
            else if (cmdBuf[0] == REQ_LEADERBOARD)
            {
                uint32_t countNet;
                if (recvAll(g_tcpSocket, &countNet, 4))
                {
                    uint32_t count = ntohl(countNet);
                    std::vector<std::pair<std::string, int>> newLB;
                    for (uint32_t i = 0; i < count; i++)
                    {
                        LeaderboardEntry entry;
                        if (recvAll(g_tcpSocket, &entry, sizeof(entry))) 
                        {
                            entry.name[15] = '\0';
                            newLB.push_back({ std::string(entry.name), (int)ntohl(entry.totalKills) });
                        }
                    }
                    std::lock_guard<std::mutex> lock(g_lbMtx);
                    g_globalLeaderboard = newLB;
                }
            }

        }
        else
        {
            // If we are still supposed to be connected, but recv failed, server closed connection
            if (g_isConnected)
            {
                g_isConnected = false;
                g_appState = AppState::MAIN_MENU;
            }
            break;
        }
    }
}

void drawChat(int winW, int winH)
{
    if (g_chatTimer > 0.0f || g_isTyping)
    {
        float startY = winH - 250.0f;
        {
            std::lock_guard<std::mutex> lock(g_chatMtx);
            for (const auto& msg : g_chatMessages)
            {
                drawTextScreen(20.0f, startY, msg, 1.0f, 1.0f, 1.0f, g_fontTiny, g_dataTiny);
                startY += 20.0f;
            }
        }
    }

    if (g_isTyping)
    {
        std::string typingText = "CHAT: " + g_currentChatInput + "_";
        drawTextScreen(20.0f, winH - 100.0f, typingText, 1.0f, 1.0f, 0.0f, g_fontTiny, g_dataTiny);
    }
}

void DisconnectFromServer()
{
    // Use g_isConnected as a flag to ensure we only run this once effectively
    // But we still want to join threads even if g_isConnected was set to false by a thread
    bool wasConnected = g_isConnected.exchange(false);
    
    // Shutdown and close sockets to unblock recv calls
    if (g_tcpSocket != INVALID_SOCKET) 
    {
        shutdown(g_tcpSocket, SD_BOTH);
        closesocket(g_tcpSocket);
        g_tcpSocket = INVALID_SOCKET;
    }
    if (g_udpSocket != INVALID_SOCKET)
    {
        closesocket(g_udpSocket);
        g_udpSocket = INVALID_SOCKET;
    }

    // Join threads (but NOT if we are currently IN one of those threads)
    auto myId = std::this_thread::get_id();
    if (g_tTCP.joinable()) 
    {
        if (myId != g_tTCP.get_id()) 
            g_tTCP.join();
        else
            g_tTCP.detach();
    }
    if (g_tUDP.joinable()) 
    {
        if (myId != g_tUDP.get_id()) 
            g_tUDP.join();
        else 
            g_tUDP.detach();
    }

    // Reset game state
    {
        std::lock_guard<std::mutex> lock(g_stateMtx);
        g_renderPlayers.clear();
        g_renderProjectiles.clear();
        g_lastSeq = 0;
        g_matchState = 0;
        g_winnerID = -1;
    }
    {
        std::lock_guard<std::mutex> lock(g_chatMtx);
        g_chatMessages.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_lbMtx);
        g_globalLeaderboard.clear();
    }
    g_isPaused = false;
    g_isTabbed = false;
    g_isTyping = false;

    if (wasConnected)
        std::cout << "[Client] Disconnected from server.\n";
}

void udpReceiverThread() 
{
    std::vector<char> buf(UDPPACKET_BUFFER_SIZE);
    sockaddr_in from{};
    int fromLen = sizeof(from);
    bool isFirstPacket = true;

    while (g_running && g_isConnected) 
    {
        fromLen = sizeof(from);
        int r = recvfrom(g_udpSocket, buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fromLen);

        if (r > 0 && r >= (int)sizeof(GameStateHeader)) 
        {
            auto* header = reinterpret_cast<GameStateHeader*>(buf.data());
            uint32_t seq = ntohl(header->sequenceNum);
            uint32_t matchState = ntohl(header->matchState);
            uint32_t numPlayers = ntohl(header->numPlayers);
            uint32_t numProjs = ntohl(header->numProjectiles);

            g_matchState = matchState;
            g_winnerID = (int32_t)ntohl(header->winnerID);

            if (matchState == 1 && g_appState == AppState::WAITING_ROOM)
                g_appState = AppState::IN_GAME;
            else if (matchState == 0 && g_appState == AppState::IN_GAME) 
            {
                g_appState = AppState::MAIN_MENU;
                g_isPaused = false;
                g_isTabbed = false;
            }

            int expectedSize = sizeof(GameStateHeader) + (numPlayers * sizeof(PlayerState)) + (numProjs * sizeof(ProjectileState));
            if (r >= expectedSize) 
            {
                std::lock_guard<std::mutex> lock(g_stateMtx);
                if (isFirstPacket || seq > g_lastSeq) 
                {
                    isFirstPacket = false;
                    g_lastSeq = seq;

                    char* payloadPtr = buf.data() + sizeof(GameStateHeader);

                    // players
                    std::map<uint32_t, ClientPlayer> activePlayersThisTick;
                    for (uint32_t i = 0; i < numPlayers; i++) 
                    {
                        auto* ps = reinterpret_cast<PlayerState*>(payloadPtr);
                        uint32_t pID = ntohl(ps->playerID);

                        activePlayersThisTick[pID].name = std::string(ps->name);
                        activePlayersThisTick[pID].x = ps->x;
                        activePlayersThisTick[pID].y = ps->y;
                        activePlayersThisTick[pID].aimAngle = ps->aimAngle;
                        activePlayersThisTick[pID].hp = (int)ntohl(ps->hp);
                        activePlayersThisTick[pID].kills = (int)ntohl(ps->kills);
                        activePlayersThisTick[pID].shootCooldown = (int)ntohl(ps->shootCooldown);
                        activePlayersThisTick[pID].isReady = ps->isReady;
                        activePlayersThisTick[pID].hasUpgradedGun = ps->hasUpgradedGun;

                        if (pID == g_myPlayerID)
                        {
                            g_totalKills = (int)ntohl(ps->totalKills);
                            g_hasUpgradedGun = ps->hasUpgradedGun;
                        }
                        if(g_audio)
                        {
                            if (ps->justShot) 
                                g_audio->PlaySFX(shooting_audio);
                            if (ps->justHit) 
                                g_audio->PlaySFX(explosion_audio);
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

bool ConnectToServer(const std::string& serverIPStr)
{
    if (g_isConnected) 
        DisconnectFromServer();

    std::string tcpPortStr = "27015";
    uint16_t serverUDPPort = 27015;

    // TCP for establishing link
    addrinfo tcpHints{}, * tcpInfo = nullptr;
    tcpHints.ai_family = AF_INET;
    tcpHints.ai_socktype = SOCK_STREAM;
    tcpHints.ai_protocol = IPPROTO_TCP;
    tcpHints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(serverIPStr.c_str(), tcpPortStr.c_str(), &tcpHints, &tcpInfo) != 0) 
        return false;

    g_tcpSocket = socket(tcpHints.ai_family, tcpHints.ai_socktype, tcpHints.ai_protocol);
    int errorCode = connect(g_tcpSocket, tcpInfo->ai_addr, (int)tcpInfo->ai_addrlen);
    freeaddrinfo(tcpInfo);

    if (SOCKET_ERROR == errorCode)
    {
        std::cerr << "Failed to connect to server TCP.\n";
        closesocket(g_tcpSocket);
        g_tcpSocket = INVALID_SOCKET;
        return false;
    }

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
        closesocket(g_tcpSocket); g_tcpSocket = INVALID_SOCKET;
        closesocket(g_udpSocket); g_udpSocket = INVALID_SOCKET;
        return false;
    }

    // find out which port auto gave
    sockaddr_in localAddr{};
    int localLen = sizeof(localAddr);
    getsockname(g_udpSocket, (sockaddr*)&localAddr, &localLen);
    uint16_t myUDPPort = localAddr.sin_port;

    // setup server UDP info and connect() it for better NAT traversal
    sockaddr_in serverUdpAddr{};
    serverUdpAddr.sin_family = AF_INET;
    inet_pton(AF_INET, serverIPStr.c_str(), &serverUdpAddr.sin_addr);
    serverUdpAddr.sin_port = htons(serverUDPPort);
    connect(g_udpSocket, (sockaddr*)&serverUdpAddr, sizeof(serverUdpAddr));

    std::vector<char> msg;
    msg.push_back(REQ_JOIN);

    char nameBuf[16] = { 0 };
    strncpy_s(nameBuf, g_playerName.c_str(), _TRUNCATE);
    msg.insert(msg.end(), nameBuf, nameBuf + 16);

    uint32_t zeroIp = 0;
    msg.insert(msg.end(), reinterpret_cast<char*>(&zeroIp), reinterpret_cast<char*>(&zeroIp) + 4);
    msg.insert(msg.end(), reinterpret_cast<char*>(&myUDPPort), reinterpret_cast<char*>(&myUDPPort) + 2);
    sendAll(g_tcpSocket, msg.data(), (int)msg.size());

    // wait for response from server
    JoinResponse jr;
    if (!recvAll(g_tcpSocket, &jr, sizeof(jr)) || (int32_t)ntohl(jr.playerID) == -1) 
    {
        std::cerr << "Server is full or rejected connection.\n";
        closesocket(g_tcpSocket); g_tcpSocket = INVALID_SOCKET;
        closesocket(g_udpSocket); g_udpSocket = INVALID_SOCKET;
        return false;
    }
    g_myPlayerID = ntohl(jr.playerID);
    g_totalKills = ntohl(jr.totalKills);
    g_hasUpgradedGun = jr.hasUpgradedGun;

    std::cout << "[Client] Connected as Player " << g_myPlayerID << " | Kills: " << g_totalKills << "\n";
    
    g_isConnected = true;
    g_tUDP = std::thread(udpReceiverThread);
    g_tTCP = std::thread(tcpReceiverThread);

    return true;
}

void drawWaitingRoom(int winW, int winH, float mouseX, float mouseY, bool mousePressed, bool& mouseWasPressed)
{
    glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float centerX = winW * 0.5f;
    drawTextScreen(centerX - 180.0f, winH * 0.1f, "WAITING ROOM", 1.0f, 0.8f, 0.0f, g_fontMainMenuSmall, g_dataMainMenuSmall);

    float currentY = 0.3f;
    int connectedCount = 0;
    bool amIReady = false;

    {
        std::lock_guard<std::mutex> lock(g_stateMtx);
        for (const auto& pair : g_renderPlayers)
        {
            connectedCount++;
            uint32_t id = pair.first;
            bool isReady = pair.second.isReady;

            if (id == g_myPlayerID) 
                amIReady = isReady;

            float pixelY = (1.0f - (currentY + 1.0f) * 0.5f) * winH;

            std::string playerTxt = pair.second.name;
            if (id == g_myPlayerID) 
                playerTxt += " (YOU)";

            drawTextScreen(centerX - 150.0f, pixelY, playerTxt, 1.0f, 1.0f, 1.0f, g_fontScoreboard, g_dataScoreboard);

            if (isReady)
                drawTextScreen(centerX + 50.0f, pixelY, "READY", 0.2f, 1.0f, 0.2f, g_fontScoreboard, g_dataScoreboard);
            else
                drawTextScreen(centerX + 50.0f, pixelY, "WAITING", 0.6f, 0.6f, 0.6f, g_fontScoreboard, g_dataScoreboard);

            currentY -= 0.15f;
        }
    }

    if (connectedCount == 0) 
    {
        float pixelY = (1.0f - (currentY + 1.0f) * 0.5f) * winH;
        drawTextScreen(centerX - 100.0f, pixelY, "Connecting...", 0.5f, 0.5f, 0.5f, g_fontScoreboard, g_dataScoreboard);
    }

    // READY Button
    float btnY = -0.6f;
    bool isHovered = (mouseX >= -0.3f && mouseX <= 0.3f && mouseY >= btnY - 0.1f && mouseY <= btnY + 0.1f);

    if (amIReady)
        glColor3f(0.1f, 0.6f, 0.1f);
    else if (isHovered)
        glColor3f(0.5f, 0.5f, 0.5f);
    else 
        glColor3f(0.3f, 0.3f, 0.3f);

    glBegin(GL_QUADS);
    glVertex2f(-0.3f, btnY - 0.1f); glVertex2f(0.3f, btnY - 0.1f);
    glVertex2f(0.3f, btnY + 0.1f);  glVertex2f(-0.3f, btnY + 0.1f);
    glEnd();

    float btnPixelY = (1.0f - (btnY + 1.0f) * 0.5f) * winH;
    drawTextScreen(centerX - 50.0f, btnPixelY + 10.0f, amIReady ? "UNREADY" : "READY", 1.0f, 1.0f, 1.0f, g_fontScoreboard, g_dataScoreboard);

    if (isHovered && mousePressed && !mouseWasPressed)
    {
        char req = REQ_TOGGLE_READY;
        sendAll(g_tcpSocket, &req, 1);
    }

    // exit back to mainmenu button
    float backY = -0.85f;
    bool backHovered = (mouseX >= -0.2f && mouseX <= 0.2f && mouseY >= backY - 0.08f && mouseY <= backY + 0.08f);

    glColor3f(backHovered ? 0.8f : 0.6f, 0.2f, 0.2f); // Reddish color for EXIT
    glBegin(GL_QUADS);
    glVertex2f(-0.2f, backY - 0.08f); glVertex2f(0.2f, backY - 0.08f);
    glVertex2f(0.2f, backY + 0.08f);  glVertex2f(-0.2f, backY + 0.08f);
    glEnd();

    float backPixelY = (1.0f - (backY + 1.0f) * 0.5f) * winH;
    drawTextScreen(centerX - 40.0f, backPixelY + 10.0f, "BACK", 1.0f, 1.0f, 1.0f, g_fontScoreboard, g_dataScoreboard);

    if (backHovered && mousePressed && !mouseWasPressed)
    {
        if (amIReady)
        {
            char req = REQ_TOGGLE_READY;
            sendAll(g_tcpSocket, &req, 1);
        }

        g_appState = AppState::MAIN_MENU;
    }
}

void drawMainMenu(int winW, int winH, float mouseX, float mouseY, bool mousePressed, bool& mouseWasPressed)
{
    glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float centerX = winW * 0.5f;

    drawTextScreen(centerX - 210.f, winH * 0.2f, "TANK WARS", 1.f, 0.6f, 0.1f, g_fontMainMenuLarge, g_dataMainMenuLarge);

    float btnWidth = 0.4f;
    float btnHeight = 0.1f;

    float playY = 0.1f;
    float progY = -0.2f;
    float quitY = -0.5f;

    struct Button { float y; std::string text; int action; };
    Button buttons[] = {
        { playY, "PLAY", 1 },
        { progY, "EXP", 2 },
        { quitY, "QUIT", 3 }
    };

    for (const auto& btn : buttons)
    {
        bool isHovered = (mouseX >= -btnWidth && mouseX <= btnWidth && mouseY >= btn.y - btnHeight && mouseY <= btn.y + btnHeight);

        glColor3f(isHovered ? 0.4f : 0.2f, isHovered ? 0.4f : 0.2f, isHovered ? 0.4f : 0.2f);
        glBegin(GL_QUADS);
        glVertex2f(-btnWidth, btn.y - btnHeight);
        glVertex2f(btnWidth, btn.y - btnHeight);
        glVertex2f(btnWidth, btn.y + btnHeight);
        glVertex2f(-btnWidth, btn.y + btnHeight);
        glEnd();

        float textPixelY = (1.f - (btn.y + 1.f) * 0.5f) * winH;
        drawTextScreen(centerX - 55.f, textPixelY + 20.f, btn.text, 1.f, 1.f, 1.f, g_fontMainMenuSmall, g_dataMainMenuSmall);

        if (isHovered && mousePressed && !mouseWasPressed)
        {
            if (btn.action == 1) g_appState = AppState::WAITING_ROOM;
            if (btn.action == 2) g_appState = AppState::EXP_SCREEN;
            if (btn.action == 3) g_running = false;
        }
    }
}

void drawEXP(int winW, int winH, float mouseX, float mouseY, bool mousePressed, bool& mouseWasPressed)
{
    glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float centerX = winW * 0.5f;

    drawTextScreen(centerX - 220.f, winH * 0.15f, "EXP SCREEN", 1.f, 0.8f, 0.2f, g_fontMainMenuLarge, g_dataMainMenuLarge);

    std::string killsTxt = "Total Kills: " + std::to_string(g_totalKills);
    drawTextScreen(centerX - 120.f, winH * 0.35f, killsTxt, 1.f, 1.f, 1.f, g_fontMainMenuSmall, g_dataMainMenuSmall);

    // Buy Button
    float btnY = -0.1f;
    float btnWidth = 0.45f;
    float btnHeight = 0.1f;
    bool isHovered = (mouseX >= -btnWidth && mouseX <= btnWidth && mouseY >= btnY - btnHeight && mouseY <= btnY + btnHeight);

    if (g_hasUpgradedGun) 
        glColor3f(0.2f, 0.6f, 0.2f);
    else if (isHovered && g_totalKills >= 10) 
        glColor3f(0.5f, 0.5f, 0.1f);
    else if (isHovered) 
        glColor3f(0.4f, 0.4f, 0.4f);
    else 
        glColor3f(0.2f, 0.2f, 0.2f);

    glBegin(GL_QUADS);
    glVertex2f(-btnWidth, btnY - btnHeight);
    glVertex2f(btnWidth, btnY - btnHeight);
    glVertex2f(btnWidth, btnY + btnHeight);
    glVertex2f(-btnWidth, btnY + btnHeight);
    glEnd();

    std::string btnTxt = g_hasUpgradedGun ? "GUN UPGRADED!" : "UPGRADE GUN (10 KILLS)";
    float textPixelY = (1.f - (btnY + 1.f) * 0.5f) * winH;
    drawTextScreen(centerX - 125.f, textPixelY + 10.f, btnTxt, 1.f, 1.f, 1.f, g_fontEXP, g_dataEXP);

    if (isHovered && mousePressed && !mouseWasPressed && !g_hasUpgradedGun && g_totalKills >= 10)
    {
        g_totalKills -= 10;
        g_hasUpgradedGun = true;
        unsigned char req = REQ_BUY_UPGRADE;
        sendAll(g_tcpSocket, &req, 1);
    }

    // BACK Button
    float backY = -0.6f;
    bool backHovered = (mouseX >= -0.2f && mouseX <= 0.2f && mouseY >= backY - 0.08f && mouseY <= backY + 0.08f);
    glColor3f(backHovered ? 0.4f : 0.2f, backHovered ? 0.4f : 0.2f, backHovered ? 0.4f : 0.2f);
    glBegin(GL_QUADS);
    glVertex2f(-0.2f, backY - 0.08f); glVertex2f(0.2f, backY - 0.08f);
    glVertex2f(0.2f, backY + 0.08f);  glVertex2f(-0.2f, backY + 0.08f);
    glEnd();
    float backTextY = (1.f - (backY + 1.f) * 0.5f) * winH;
    drawTextScreen(centerX - 35.f, backTextY + 10.f, "BACK", 1.f, 1.f, 1.f, g_fontEXP, g_dataEXP);

    if (backHovered && mousePressed && !mouseWasPressed)
    {
        g_appState = AppState::MAIN_MENU;
    }
}

int main() 
{
    WSADATA wsaData;
    SecureZeroMemory(&wsaData, sizeof(wsaData));

    int errorCode = WSAStartup(MAKEWORD(2, 2), &wsaData);
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
    std::cout << "Enter Server IPv4 [Press Enter auto fill]: ";
    std::string input;
    std::getline(std::cin, input);
    serverIPStr = input.empty() ? detectedIP : input;

    std::cout << "Enter Player Name (15 char max): ";
    std::string nameInput;
    std::getline(std::cin, nameInput);
    if (!nameInput.empty())
        g_playerName = nameInput;

    g_audio = new AudioManager();
    g_audio->PlayBGM(mainmenu_BGM_audio);

    if (!glfwInit()) 
        return -1;

    std::string title = "TANK WARS";
    GLFWwindow* window = glfwCreateWindow(600, 600, title.c_str(), NULL, NULL);
    if (!window) 
    { 
        glfwTerminate(); 
        return -1; 
    }
    glfwMakeContextCurrent(window);
    glfwSetCharCallback(window, charCallback);

    g_fontPlayerName = loadFont("arial.ttf", 10.f, g_dataPlayerName);
    g_fontScoreboard = loadFont("arial.ttf", 20.f, g_dataScoreboard);
    g_fontScoreboardTitle = loadFont("arial.ttf", 60.f, g_dataScoreboardTitle);
    g_fontMainMenuLarge = loadFont("arial.ttf", 80.f, g_dataMainMenuLarge);
    g_fontMainMenuSmall = loadFont("arial.ttf", 50.f, g_dataMainMenuSmall);
    g_fontTiny = loadFont("arial.ttf", 14.f, g_dataTiny);
    g_fontEXP = loadFont("arial.ttf", 22.f, g_dataEXP);

    uint32_t inputSeq = 0;
    float lastTime = (float)glfwGetTime(); 
    
    std::string activeBGM = mainmenu_BGM_audio;

    while (!glfwWindowShouldClose(window) && g_running) 
    {
        float currentTime = (float)glfwGetTime();
        float deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        std::string targetBGM = (g_appState == AppState::IN_GAME) ? ingame_BGM_audio : mainmenu_BGM_audio;
        if (activeBGM != targetBGM)
        {
            activeBGM = targetBGM;
            g_audio->PlayBGM(activeBGM);
        }

        if (g_chatTimer > 0.0f)
            g_chatTimer = g_chatTimer - deltaTime;

        bool isFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;

        int fbW, fbH, winW, winH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glfwGetWindowSize(window, &winW, &winH);
        glViewport(0, 0, fbW, fbH);

        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        float mouseX = (float)((xpos / winW) * 2.0 - 1.0);
        float mouseY = (float)(1.0 - (ypos / winH) * 2.0);

        static bool mouseWasPressed = false;
        bool mousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        bool tabPressed = isFocused && (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS);
        if (tabPressed && !g_tabWasPressed)
        {
            if (g_appState == AppState::IN_GAME)
            {
                if (g_isPaused) 
                { 
                    g_isPaused = false; 
                    g_isTabbed = true; 
                }
                else 
                    g_isTabbed = !g_isTabbed;
            }
            else if (g_appState == AppState::WAITING_ROOM)
            {
                g_isTabbed = !g_isTabbed;

                if (g_isTabbed && g_isConnected) 
                {
                    char req = REQ_LEADERBOARD;
                    sendAll(g_tcpSocket, &req, 1);
                }
            }
        }
        g_tabWasPressed = tabPressed;

        if (g_appState == AppState::MAIN_MENU)
        {
            if (g_isConnected) 
                DisconnectFromServer();

            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            drawMainMenu(winW, winH, mouseX, mouseY, mousePressed, mouseWasPressed);
        }
        else if (g_appState == AppState::EXP_SCREEN)
        {
            if (!g_isConnected && !ConnectToServer(serverIPStr))
                g_appState = AppState::MAIN_MENU;

            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            drawEXP(winW, winH, mouseX, mouseY, mousePressed, mouseWasPressed);
        }
        else if (g_appState == AppState::WAITING_ROOM)
        {
            if (!g_isConnected && !ConnectToServer(serverIPStr))
                g_appState = AppState::MAIN_MENU;

            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            drawWaitingRoom(winW, winH, mouseX, mouseY, mousePressed, mouseWasPressed);

            if (g_isTabbed) 
                drawGlobalLeaderboard(winW, winH);

            // send heartbeat
            if (g_isConnected)
            {
                InputPacket heartbeatPkt;
                heartbeatPkt.sequenceNum = htonl(inputSeq++);
                heartbeatPkt.playerID = htonl(g_myPlayerID);
                heartbeatPkt.w_pressed = heartbeatPkt.a_pressed = heartbeatPkt.s_pressed = heartbeatPkt.d_pressed = heartbeatPkt.space_pressed = false;
                heartbeatPkt.aimAngle = 0.0f;
                heartbeatPkt.hasUpgradedGun = g_hasUpgradedGun;
                send(g_udpSocket, reinterpret_cast<const char*>(&heartbeatPkt), sizeof(heartbeatPkt), 0);
            }
        }
        else if (g_appState == AppState::IN_GAME)
        {
            if (!g_isConnected)
                g_appState = AppState::MAIN_MENU;
            else 
            {
                // chat detection enter
                bool enterPressed = isFocused && (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS);
                if (enterPressed && !g_enterWasPressed)
                {
                    if (!g_isTyping)
                    {
                        g_isTyping = true;
                        g_currentChatInput = "";
                    }
                    else
                    {
                        if (!g_currentChatInput.empty())
                        {
                            uint16_t msgLen = (uint16_t)g_currentChatInput.length();
                            uint16_t msgLenNet = htons(msgLen);

                            std::vector<char> chatPkt;
                            chatPkt.push_back(REQ_CHAT);
                            chatPkt.insert(chatPkt.end(), reinterpret_cast<char*>(&msgLenNet), reinterpret_cast<char*>(&msgLenNet) + 2);
                            chatPkt.insert(chatPkt.end(), g_currentChatInput.begin(), g_currentChatInput.end());

                            sendAll(g_tcpSocket, chatPkt.data(), (int)chatPkt.size());
                        }
                        g_isTyping = false;
                    }
                }
                g_enterWasPressed = enterPressed;

                if (g_isTyping)
                {
                    bool backspacePressed = isFocused && (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS);
                    if (backspacePressed && !g_backspaceWasPressed)
                    {
                        if (!g_currentChatInput.empty())
                            g_currentChatInput.pop_back();
                    }
                    g_backspaceWasPressed = backspacePressed;
                }

                // pause menu detection esc
                bool escPressed = isFocused && (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
                if (escPressed && !g_escWasPressed)
                {
                    if (g_isTabbed)
                    {
                        g_isTabbed = false;
                        g_isPaused = true;
                    }
                    else
                        g_isPaused = !g_isPaused;
                }
                g_escWasPressed = escPressed;

                static bool PWasPressed = false;
                bool PPressed = isFocused && (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS);
                if (!g_isTyping && PPressed && !PWasPressed)
                {
                    char req = REQ_CHEAT_WIN;
                    sendAll(g_tcpSocket, &req, 1);
                }
                PWasPressed = PPressed;

                // hide cursor in game show in pause menu
                if (g_isPaused || g_isTabbed)
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                else
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

                // Pause Menu Interactions Volume n Quit
                if (g_isPaused && isFocused)
                {
                    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
                    {
                        g_currentVolume += 0.01f;
                        if (g_currentVolume > 1.0f) 
                            g_currentVolume = 1.0f;

                        if (g_audio) 
                            g_audio->SetMasterVolume(g_currentVolume);
                    }
                    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
                    {
                        g_currentVolume -= 0.01f;
                        if (g_currentVolume < 0.0f) 
                            g_currentVolume = 0.0f;

                        if (g_audio) 
                            g_audio->SetMasterVolume(g_currentVolume);
                    }

                    if (mousePressed) {
                        using namespace UI;
                        barY = -0.1f; gap = 0.08f; barHalfWidth = 0.4f;
                        double buttonRadius_sq = buttonRadius * buttonRadius;

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

                        // check for quit button to go back to main menu
                        if (!mouseWasPressed)
                        {
                            quitY = -0.4f;
                            quitSize = 0.06f;
                            if (mouseX >= -quitSize && mouseX <= quitSize && mouseY >= quitY - quitSize && mouseY <= quitY + quitSize)
                            {
                                g_appState = AppState::MAIN_MENU;
                                g_isPaused = false;
                                DisconnectFromServer();
                            }
                        }
                    }
                }

                // send input data pkt if not typing
                if (!g_isTyping)
                {
                    InputPacket inputPkt;
                    inputPkt.sequenceNum = htonl(inputSeq++);
                    inputPkt.playerID = htonl(g_myPlayerID);

                    inputPkt.w_pressed = !g_isTabbed && !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);
                    inputPkt.a_pressed = !g_isTabbed && !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
                    inputPkt.s_pressed = !g_isTabbed && !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
                    inputPkt.d_pressed = !g_isTabbed && !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS);
                    inputPkt.space_pressed = !g_isTabbed && !g_isPaused && isFocused && (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
                    inputPkt.aimAngle = 0.0f;
                    inputPkt.hasUpgradedGun = g_hasUpgradedGun;

                    send(g_udpSocket, reinterpret_cast<const char*>(&inputPkt), sizeof(inputPkt), 0);
                }
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

                // draw tanks
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

                    bool isMe = (id == g_myPlayerID);
                    drawTank(px, py, r, g, b, pAngle, pHP, pShootCD, isMe, pair.second.hasUpgradedGun);
                }

                // draw proj
                for (const auto& proj : projsToDraw)
                    drawProjectile(proj.x, proj.y, proj.isUpgraded != 0);

                // draw floating name
                for (const auto& pair : playersToDraw)
                {
                    uint32_t id = pair.first;
                    std::string playerName = pair.second.name;
                    float px = pair.second.x;
                    float py = pair.second.y;

                    int winW, winH;
                    glfwGetWindowSize(window, &winW, &winH);
                    float screenX = (px + 1.0f) * 0.5f * winW;
                    float screenY = (1.0f - (py + 1.0f) * 0.5f) * winH;

                    drawTextScreen(screenX - 15, screenY - 25, playerName, 1.0f, 1.0f, 1.0f, g_fontPlayerName, g_dataPlayerName);
                }

                // game over 
                if (g_matchState == 2 && g_winnerID != -1)
                {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glColor4f(0.0f, 0.0f, 0.0f, 0.85f);
                    glBegin(GL_QUADS);
                    glVertex2f(-1.0f, -1.0f); glVertex2f(1.0f, -1.0f);
                    glVertex2f(1.0f, 1.0f); glVertex2f(-1.0f, 1.0f);
                    glEnd();
                    glDisable(GL_BLEND);

                    std::string winText;
                    if (g_winnerID == g_myPlayerID) 
                    {
                        winText = "VICTORY!";
                        drawTextScreen((winW * 0.5f) - 198.f, winH * 0.5f, winText, 0.2f, 1.0f, 0.2f, g_fontMainMenuLarge, g_dataMainMenuLarge);
                    }
                    else 
                    {
                        std::string winnerName = "Player";
                        if (playersToDraw.count(g_winnerID)) winnerName = playersToDraw[g_winnerID].name;

                        winText = winnerName + " WON!";
                        drawTextScreen((winW * 0.5f) - 198.f, winH * 0.5f, winText, 1.0f, 0.8f, 0.0f, g_fontMainMenuLarge, g_dataMainMenuLarge);
                    }
                    drawTextScreen((winW * 0.5f) - 190.0f, (winH * 0.5f) + 80.0f, "Returning to Main Menu...", 0.6f, 0.6f, 0.6f, g_fontTiny, g_dataTiny);
                }

                // (Keep your g_isPaused and g_isTabbed checks down here!)

                if (g_isPaused)
                    drawPauseMenu(g_currentVolume);

                if (g_isTabbed)
                    drawScoreboard(winW, winH);

                drawChat(winW, winH);
            }
        }
        mouseWasPressed = mousePressed;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    g_running = false;
    DisconnectFromServer();
    glfwTerminate();
    WSACleanup();
    delete g_audio;
    return 0;
}