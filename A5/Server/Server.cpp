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
#include "Windows.h"		// Entire Win32 API...
// #include "winsock2.h"	// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()

// Tell the Visual Studio linker to include the following library in linking.
// Alternatively, we could add this file to the linker command-line parameters,
// but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")

#include <iostream>			// cout, cerr
#include <string>			// string
#include <fstream>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <algorithm>
#include <chrono>

#include "taskqueue.h"

namespace fs = std::filesystem;

#define WINSOCK_VERSION     2
#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1024
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

static uint32_t RETRY_LOG_INTERVAL = 5;     // every X retry cout

// defaults if config file not found
static uint32_t UDP_CHUNK_SIZE = 60000;  // bytes per UDP data packet
static uint32_t UDP_TIMEOUT_MS = 1000;   // ACK waitin timeout ms
static uint32_t MAX_RETRIES = 10;     // max retransmits before fail

enum CMDID : unsigned char {
    UNKNOWN = (unsigned char)0x00,
    REQ_QUIT = (unsigned char)0x01,
    REQ_DOWNLOAD = (unsigned char)0x02,
    RSP_DOWNLOAD = (unsigned char)0x03,
    REQ_LISTFILES = (unsigned char)0x04,
    RSP_LISTFILES = (unsigned char)0x05,
    CMD_TEST = (unsigned char)0x20,
    DOWNLOAD_ERROR = (unsigned char)0x30
};

// UDP datagram (Stop n Wait) (big endian)
#pragma pack(push, 1)
struct UDPHeader {
    uint32_t sessionID;
    uint32_t fileLength;  // bytes
    uint32_t fileOffset;  // byte offset
    uint32_t dataLength;
    uint8_t  flags; // 1 is ACK, 0 is data
    uint32_t seqNum;
    uint32_t ackNum;
};
#pragma pack(pop)

std::string g_serverPath;
SOCKET g_serverUDPSocket = INVALID_SOCKET;
std::atomic<uint32_t> g_nextSessionID{ 1 };

uint32_t g_serverIPNet = 0;
uint16_t g_serverUDPPort = 0;

struct SessionData {
    std::mutex mtx;
    std::condition_variable cv;
    uint32_t lastAckSeq = UINT32_MAX;
    bool newAck = false;
};

std::map<uint32_t, SessionData*> g_sessions;
std::mutex g_sessionsMtx;

/**
* @brief trim reused from A3
* @param const std::string& str
* @return std::string
*/
static std::string trim(const std::string& str)
{
    size_t first = str.find_first_not_of("\t\r\n ");
    if (std::string::npos == first)
        return "";
    size_t last = str.find_last_not_of("\t\r\n ");
    return str.substr(first, (last - first + 1));
}

/**
* @brief reads config.txt
* @param const std::string& path
*/
static void loadConfig(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::cerr << "[Config] \"" << path << "\" not found, using defaults.\n";
        return;
    }

    std::string line;
    while (std::getline(f, line))
    {
        auto hash = line.find('#'); // remove comments 
        if (hash != std::string::npos)
            line = line.substr(0, hash);

        auto equalSign = line.find('=');
        if (equalSign == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, equalSign));
        std::string val = trim(line.substr(equalSign + 1));
        if (key.empty() || val.empty())
            continue;

        try
        {
            uint32_t v = static_cast<uint32_t>(std::stoul(val));
            if (key == "UDP_CHUNK_SIZE")
                UDP_CHUNK_SIZE = v;
            else if (key == "UDP_TIMEOUT_MS")
                UDP_TIMEOUT_MS = v;
            else if (key == "MAX_RETRIES")
                MAX_RETRIES = v;
            else
                std::cerr << "[Config] Unknown key: " << key << "\n";
        }
        catch (...)
        {
            std::cerr << "[Config] Bad value for " << key << ": " << val << "\n";
        }
    }
    std::cout << "[Config] Loaded from \"" << path << "\":\n" << "  UDP_CHUNK_SIZE = " << UDP_CHUNK_SIZE << " bytes\n"
        << "  UDP_TIMEOUT_MS = " << UDP_TIMEOUT_MS << " ms\n" << "  MAX_RETRIES    = " << MAX_RETRIES << "\n";
}

/**
* @brief send
* @param SOCKET s, const void* data, int len
* @return bool
*/
static bool sendAll(SOCKET s, const void* data, int len)
{
    const char* ptr = reinterpret_cast<const char*>(data);
    int rem = len;
    while (rem > 0)
    {
        int sent = send(s, ptr, rem, 0);
        if (sent == SOCKET_ERROR || sent == 0)
            return false;
        ptr += sent;
        rem -= sent;
    }
    return true;
}

/**
* @brief recieve
* @param SOCKET s, void* buf, int len
* @return bool
*/
static bool recvAll(SOCKET s, void* buf, int len)
{
    char* ptr = reinterpret_cast<char*>(buf);
    int rem = len;
    while (rem > 0)
    {
        int r = recv(s, ptr, rem, 0);
        if (r == SOCKET_ERROR || r == 0)
            return false;
        ptr += r;
        rem -= r;
    }
    return true;
}

/**
* @brief get ipv4 add reused from A3
* @return std::vector<std::string>
*/
static std::vector<std::string> GetLocalIPv4Addresses(void)
{
    std::vector<std::string> addresses;
    addresses.push_back("127.0.0.1");

    char hostname[256];

    if (gethostname(hostname, sizeof(hostname)) == 0)
    {
        addrinfo hints{};
        SecureZeroMemory(&hints, sizeof(hints)); // zero out struct like memset
        hints.ai_family = AF_INET; // ipv4 only
        hints.ai_socktype = SOCK_STREAM; // tcp

        addrinfo* result = nullptr;
        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) // get all ipv4s
        {
            for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) // loop thru link list of ipv4s
            {
                sockaddr_in* sockaddr_ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, ipStr, INET_ADDRSTRLEN);

                if (std::string(ipStr) != "127.0.0.1")
                    addresses.push_back(std::string(ipStr));
            }
            freeaddrinfo(result);
        }
    }
    return addresses;
}

bool execute(SOCKET clientSocket);
void disconnect(SOCKET& listenerSocket);

/**
* @brief get file list
* @return std::vector<std::string>
*/
static std::vector<std::string> getFileList(void)
{
    std::vector<std::string> files;
    try {
        for (auto& entry : fs::directory_iterator(g_serverPath))
            if (entry.is_regular_file())
                files.push_back(entry.path().filename().string());
        std::sort(files.begin(), files.end());
    }
    catch (const std::exception& e) {
        std::cerr << "[Server] Error listing files: " << e.what() << std::endl;
    }
    return files;
}

/**
* @brief UDP ACK reciever thread
*/
static void udpAckReceiverThread(void)
{
    std::vector<char> buf(sizeof(UDPHeader) + 64);
    sockaddr_in from{};
    int fromLen = sizeof(from);

    while (true)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_serverUDPSocket, &readfds);

        timeval tv{ 1, 0 };
        int sel = select(0, &readfds, nullptr, nullptr, &tv);
        if (sel == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAENOTSOCK)
                break;
            continue;
        }
        if (sel == 0)
            continue;

        int r = recvfrom(g_serverUDPSocket, buf.data(), (int)buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (r == SOCKET_ERROR)
        {
            int e = WSAGetLastError();
            if (e == WSAENOTSOCK || e == WSAESHUTDOWN)
                break;
            continue;
        }
        if (r < (int)sizeof(UDPHeader))
            continue;

        auto* hdr = reinterpret_cast<UDPHeader*>(buf.data());
        if (!(hdr->flags & 0x01)) // not ACK
            continue;

        uint32_t sid = ntohl(hdr->sessionID);
        uint32_t ackSeq = ntohl(hdr->ackNum);

        std::lock_guard<std::mutex> lk(g_sessionsMtx);
        auto it = g_sessions.find(sid);
        if (it != g_sessions.end())
        {
            SessionData* sd = it->second;
            {
                std::lock_guard<std::mutex> slk(sd->mtx);
                sd->lastAckSeq = ackSeq;
                sd->newAck = true;
            }
            sd->cv.notify_one();
        }
    }
}

/**
* @brief UDP sender thread is stop n wait so it sends 1 chunk then wait for ACK then advance
* @param uint32_t sessionID, SOCKET tcpSocket, sockaddr_in clientAddr, fs::path filePath, uint32_t fileSize
*/
static void udpSenderThread(uint32_t sessionID, SOCKET tcpSocket, sockaddr_in clientAddr, fs::path filePath, uint32_t fileSize)
{
    auto sendError = [&]() {
        char errCmd = static_cast<char>(DOWNLOAD_ERROR);
        if (!sendAll(tcpSocket, &errCmd, 1))
            std::cerr << "[Session " << sessionID << "] DOWNLOAD_ERROR not delivered (client already disconnected)\n";
        else
            std::cerr << "[Session " << sessionID << "] DOWNLOAD_ERROR sent to client.\n";
        };

    SessionData sd;
    {
        std::lock_guard<std::mutex> lk(g_sessionsMtx);
        g_sessions[sessionID] = &sd;
    }

    std::ifstream fin;
    if (fileSize > 0)
    {
        fin.open(filePath, std::ios::binary);
        if (!fin)
        {
            std::cerr << "[Session " << sessionID << "] Cannot open file for streaming: " << filePath << std::endl;
            sendError();

            std::lock_guard<std::mutex> lk(g_sessionsMtx);
            g_sessions.erase(sessionID);
            return;
        }
    }

    int addrLen = sizeof(clientAddr);
    uint32_t offset = 0;
    uint32_t seqNum = 0;
    bool allOK = true;

    // stop n wait loop
    do
    {
        uint32_t chunk = (fileSize == 0) ? 0u : std::min((uint32_t)UDP_CHUNK_SIZE, fileSize - offset);

        // build datagram
        std::vector<char> pkt(sizeof(UDPHeader) + chunk);
        auto* hdr = reinterpret_cast<UDPHeader*>(pkt.data());
        hdr->sessionID = htonl(sessionID);
        hdr->fileLength = htonl(fileSize);
        hdr->fileOffset = htonl(offset);
        hdr->dataLength = htonl(chunk);
        hdr->flags = 0x00;
        hdr->seqNum = htonl(seqNum);
        hdr->ackNum = 0;

        if (chunk > 0)
        {
            fin.read(pkt.data() + sizeof(UDPHeader), chunk);
            if (static_cast<uint32_t>(fin.gcount()) != chunk)
            {
                std::cerr << "[Session " << sessionID << "] Short read at offset " << offset << std::endl;
                sendError();
                allOK = false;
                break;
            }
        }

        // retransmit till get ACK or hit max reties 
        bool acked = false;
        for (int retry = 0; retry <= MAX_RETRIES; ++retry)
        {
            sendto(g_serverUDPSocket, pkt.data(), (int)pkt.size(), 0, reinterpret_cast<sockaddr*>(&clientAddr), addrLen);

            if (retry == 0)
                std::cout << "[Session " << sessionID << "] seq=" << seqNum << " off=" << offset << " sz=" << chunk << std::endl;
            else if (retry % RETRY_LOG_INTERVAL == 0)
                std::cout << "[Session " << sessionID << "] RETRY " << retry << "/" << MAX_RETRIES << " seq=" << seqNum << std::endl;

            std::unique_lock<std::mutex> ul(sd.mtx);
            bool got = sd.cv.wait_for(ul, std::chrono::milliseconds(UDP_TIMEOUT_MS), [&] {
                return sd.newAck && sd.lastAckSeq == seqNum;
                });

            sd.newAck = false;
            if (got)
            {
                acked = true;
                break;
            }
        }

        if (!acked)
        {
            std::cerr << "[Session " << sessionID << "] TIMED OUT after " << MAX_RETRIES << " retries at seq=" << seqNum << " sending DOWNLOAD_ERROR to client." << std::endl;
            sendError();
            allOK = false;
            break;
        }

        offset += chunk;
        ++seqNum;

    } while (offset < fileSize && fileSize > 0);

    if (allOK)
        std::cout << "[Sesion " << sessionID << "] Transfer complete (" << fileSize << " bytes)" << std::endl;

    {
        std::lock_guard<std::mutex> lk(g_sessionsMtx);
        g_sessions.erase(sessionID);
    }
}

/**
* @brief disconect reused from A3
* @param SOCKET& listenerSocket
*/
void disconnect(SOCKET& listenerSocket)
{
    if (listenerSocket != INVALID_SOCKET)
    {
        shutdown(listenerSocket, SD_BOTH);
        closesocket(listenerSocket);
        listenerSocket = INVALID_SOCKET;
    }
}

/**
* @brief execute modifyed from A3
* @param SOCKET clientSocket
* @return bool
*/
bool execute(SOCKET clientSocket)
{
    char cmdBuf[1];
    bool running = true;

    while (running)
    {
        if (!recvAll(clientSocket, cmdBuf, 1))
        {
            std::cout << "[Server] Client disconected." << std::endl;
            break;
        }
        unsigned char cmd = static_cast<unsigned char>(cmdBuf[0]);

        if (cmd == REQ_QUIT)
        {
            std::cout << "[Server] REQ_QUIT recieved." << std::endl;
            break;
        }
        else if (cmd == REQ_LISTFILES)
        {
            std::cout << "[Server] REQ_LISTFILES recieved." << std::endl;
            auto files = getFileList();

            uint32_t listLen = 0; // sum of 4byte (uint32) + filename bytes
            for (auto& f : files)
                listLen += 4 + (uint32_t)f.size();

            // consturct datagram
            std::vector<char> rsp;
            rsp.push_back(static_cast<char>(RSP_LISTFILES));

            uint16_t noOfFiles_net = htons(static_cast<uint16_t>(files.size()));
            rsp.insert(rsp.end(), reinterpret_cast<char*>(&noOfFiles_net), reinterpret_cast<char*>(&noOfFiles_net) + 2); // 2 cos uint16

            uint32_t listLen_net = htonl(listLen);
            rsp.insert(rsp.end(), reinterpret_cast<char*>(&listLen_net), reinterpret_cast<char*>(&listLen_net) + 4); // 4 cos uint32

            for (auto& f : files)
            {
                uint32_t nameLen_net = htonl(static_cast<uint32_t>(f.size()));
                rsp.insert(rsp.end(), reinterpret_cast<char*>(&nameLen_net), reinterpret_cast<char*>(&nameLen_net) + 4); // 4 cos uint32
                rsp.insert(rsp.end(), f.begin(), f.end());
            }

            sendAll(clientSocket, rsp.data(), (int)rsp.size());
        }
        else if (cmd == REQ_DOWNLOAD)
        {
            uint32_t clientIP_net{};
            uint16_t clientPort_net{};
            uint32_t filenameLen_net{};

            bool ok = recvAll(clientSocket, &clientIP_net, 4) && recvAll(clientSocket, &clientPort_net, 2) && recvAll(clientSocket, &filenameLen_net, 4);
            if (!ok)
            {
                running = false;
                break;
            }

            // 00000008test.jpg 00000006yo.mp3
            uint32_t filenameLen = ntohl(filenameLen_net);
            std::vector<char> filenameBuf(filenameLen + 1, '\0');
            if (!recvAll(clientSocket, filenameBuf.data(), static_cast<int>(filenameLen)))
            {
                running = false;
                break;
            }
            std::string filename(filenameBuf.data(), filenameLen);

            uint16_t clientUDPPort = ntohs(clientPort_net);
            std::cout << "[Server] REQ_DOWNLOAD \"" << filename << "\" | port " << clientUDPPort << std::endl;

            fs::path fullPath = fs::path(g_serverPath) / filename;

            if (!fs::exists(fullPath) || !fs::is_regular_file(fullPath))
            {
                char errCmd = static_cast<char>(DOWNLOAD_ERROR);
                sendAll(clientSocket, &errCmd, 1);
                std::cerr << "[Server] File not found: " << fullPath << std::endl;
                continue;
            }

            uint32_t fileSize = static_cast<uint32_t>(fs::file_size(fullPath));
            uint32_t sessionID = g_nextSessionID.fetch_add(1);

            if (fileSize > 0)
            {
                std::ifstream probe(fullPath, std::ios::binary);
                if (!probe)
                {
                    char errCmd = static_cast<char>(DOWNLOAD_ERROR);
                    sendAll(clientSocket, &errCmd, 1);
                    std::cerr << "[Server] Cannot open for reading: " << fullPath << std::endl;
                    continue;
                }
            }

            sockaddr_in localAddr{};
            int localAddrLen = sizeof(localAddr);
            uint32_t rspIP_net = g_serverIPNet;
            if (getsockname(clientSocket, reinterpret_cast<sockaddr*>(&localAddr), &localAddrLen) == 0 && localAddr.sin_addr.s_addr != 0)
                rspIP_net = localAddr.sin_addr.s_addr;

            // consturct datagram
            std::vector<char> rsp;
            rsp.push_back(static_cast<char>(RSP_DOWNLOAD));

            rsp.insert(rsp.end(), reinterpret_cast<char*>(&rspIP_net), reinterpret_cast<char*>(&rspIP_net) + 4);
            rsp.insert(rsp.end(), reinterpret_cast<char*>(&g_serverUDPPort), reinterpret_cast<char*>(&g_serverUDPPort) + 2);

            uint32_t sidNet = htonl(sessionID);
            rsp.insert(rsp.end(), reinterpret_cast<char*>(&sidNet), reinterpret_cast<char*>(&sidNet) + 4);

            uint32_t fsNet = htonl(fileSize);
            rsp.insert(rsp.end(), reinterpret_cast<char*>(&fsNet), reinterpret_cast<char*>(&fsNet) + 4);

            sendAll(clientSocket, rsp.data(), (int)rsp.size());

            sockaddr_in cAddr{};
            cAddr.sin_family = AF_INET;
            cAddr.sin_addr.s_addr = clientIP_net;
            cAddr.sin_port = clientPort_net;

            std::thread t(udpSenderThread, sessionID, clientSocket, cAddr, fullPath, fileSize);
            t.detach();
        }
        else
            std::cerr << "[Server] Unknown command 0x" << std::hex << static_cast<int>(cmd) << std::dec << std::endl;
    }


    closesocket(clientSocket);
    {
        std::lock_guard<std::mutex> lk(_stdoutMutex);
        std::cout << "[Server] Client handler exited." << std::endl;
    }
    return true;
}

int main(int argc, char** argv)
{
    loadConfig("config.txt");

    std::string tcpPortStr, udpPortStr;

    if (argc == 2) // script mode reused from A3
    {
        std::ifstream scriptFile(argv[1]);
        if (!scriptFile.is_open())
        {
            std::cerr << "Failed to open script file: " << argv[1] << std::endl;
            return RETURN_CODE_1;
        }
        std::string line;
        if (std::getline(scriptFile, line))
            tcpPortStr = trim(line);
        if (std::getline(scriptFile, line))
            udpPortStr = trim(line);
        if (std::getline(scriptFile, line))
            g_serverPath = trim(line);

        scriptFile.close();
    }
    else
    {
        std::cout << "Server TCP Port Number: ";
        std::getline(std::cin, tcpPortStr);
        std::cout << std::endl;

        std::cout << "Server UDP Port Number: ";
        std::getline(std::cin, udpPortStr);
        std::cout << std::endl;

        std::cout << "Path: ";
        std::getline(std::cin, g_serverPath);
        std::cout << std::endl;
    }

    // trim right
    while (!g_serverPath.empty() && (g_serverPath.back() == '\r' || g_serverPath.back() == '\n' || g_serverPath.back() == ' '))
        g_serverPath.pop_back();

    // below reused from tempalte 
        // This object holds the information about the version of Winsock that we
        // are using, which is not necessarily the version that we requested.
    WSADATA wsaData{};
    SecureZeroMemory(&wsaData, sizeof(wsaData));

    // Initialize Winsock. You must call WSACleanup when you are finished.
    // As this function uses a reference counter, for each call to WSAStartup,
    // you must call WSACleanup or suffer memory issues.
    int errorCode = WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData);
    if (NO_ERROR != errorCode)
    {
        std::cerr << "WSAStartup() failed." << std::endl;
        return errorCode;
    }

    // Object hints indicates which protocols to use to fill in the info.
    addrinfo tcpHints{};
    SecureZeroMemory(&tcpHints, sizeof(tcpHints));
    tcpHints.ai_family = AF_INET;			// IPv4
    // For UDP use SOCK_DGRAM instead of SOCK_STREAM.
    tcpHints.ai_socktype = SOCK_STREAM;	// Reliable delivery
    // Could be 0 for autodetect, but reliable delivery over IPv4 is always TCP.
    tcpHints.ai_protocol = IPPROTO_TCP;	// TCP
    // Create a passive socket that is suitable for bind() and listen().
    tcpHints.ai_flags = AI_PASSIVE;

    addrinfo* tcpInfo = nullptr;
    errorCode = getaddrinfo(nullptr, tcpPortStr.c_str(), &tcpHints, &tcpInfo);
    if (NO_ERROR != errorCode || !tcpInfo)
    {
        std::cerr << "getaddrinfo() failed for TCP." << std::endl;
        WSACleanup();
        return RETURN_CODE_1;
    }

    auto localIPs = GetLocalIPv4Addresses();
    std::string displayIP = "127.0.0.1"; // fix since A3 give me D .-.
    for (const auto& ip : localIPs)
        if (ip != "127.0.0.1")
        {
            displayIP = ip;
            break;
        }
    std::cout << "\nServer IP Address: " << displayIP << std::endl;
    std::cout << "Server TCP Port:    " << tcpPortStr << std::endl;
    std::cout << "Server UDP Port:    " << udpPortStr << std::endl;
    std::cout << "Files Path:         " << g_serverPath << std::endl;

    SOCKET listenerSocket = socket(tcpHints.ai_family, tcpHints.ai_socktype, tcpHints.ai_protocol);
    if (listenerSocket == INVALID_SOCKET)
    {
        std::cerr << "socket() TCP failed." << std::endl;
        freeaddrinfo(tcpInfo);
        WSACleanup();
        return RETURN_CODE_1;
    }

    int reuseAddr = 1;
    setsockopt(listenerSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuseAddr), sizeof(reuseAddr));

    errorCode = bind(listenerSocket, tcpInfo->ai_addr, static_cast<int>(tcpInfo->ai_addrlen));
    if (errorCode != NO_ERROR)
    {
        std::cerr << "bind() TCP failed: " << std::endl;
        closesocket(listenerSocket);
        WSACleanup();
        return RETURN_CODE_2;
    }

    freeaddrinfo(tcpInfo);

    errorCode = listen(listenerSocket, SOMAXCONN);
    if (errorCode != NO_ERROR)
    {
        std::cerr << "listen() TCP failed." << std::endl;
        closesocket(listenerSocket);
        WSACleanup();
        return RETURN_CODE_3;
    }

    // Object hints indicates which protocols to use to fill in the info.
    addrinfo udpHints{};
    SecureZeroMemory(&udpHints, sizeof(udpHints));
    udpHints.ai_family = AF_INET;			// IPv4
    // For UDP use SOCK_DGRAM instead of SOCK_STREAM.
    udpHints.ai_socktype = SOCK_DGRAM;	// Reliable delivery
    // Could be 0 for autodetect, but reliable delivery over IPv4 is always TCP.
    udpHints.ai_protocol = IPPROTO_UDP;	// UDP
    // Create a passive socket that is suitable for bind() and listen().
    udpHints.ai_flags = AI_PASSIVE;

    addrinfo* udpInfo = nullptr;
    errorCode = getaddrinfo(nullptr, udpPortStr.c_str(), &udpHints, &udpInfo);
    if (errorCode != NO_ERROR || !udpInfo)
    {
        std::cerr << "getaddrinfo() failed for UDP." << std::endl;
        closesocket(listenerSocket);
        WSACleanup();
        return RETURN_CODE_1;
    }

    g_serverUDPSocket = socket(udpHints.ai_family, udpHints.ai_socktype, udpHints.ai_protocol);
    if (g_serverUDPSocket == INVALID_SOCKET)
    {
        std::cerr << "socket() UDP failed." << std::endl;
        freeaddrinfo(udpInfo);
        closesocket(listenerSocket);
        WSACleanup();
        return RETURN_CODE_1;
    }

    g_serverUDPPort = reinterpret_cast<sockaddr_in*>(udpInfo->ai_addr)->sin_port;
    errorCode = bind(g_serverUDPSocket, udpInfo->ai_addr, static_cast<int>(udpInfo->ai_addrlen));
    if (errorCode != NO_ERROR)
    {
        std::cerr << "bind() UDP failed: " << WSAGetLastError() << std::endl;
        closesocket(g_serverUDPSocket);
        closesocket(listenerSocket);
        WSACleanup();
        return RETURN_CODE_2;
    }

    freeaddrinfo(udpInfo);

    int rcvBuf = 4 * 1024 * 1024;
    setsockopt(g_serverUDPSocket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcvBuf), sizeof(rcvBuf));
    std::cout << "\nServer ready. Waiting for connections..." << std::endl;

    std::thread ackThread(udpAckReceiverThread);
    ackThread.detach();

    // reused from A3
    {
        const auto onDisconnect = [&]() { disconnect(listenerSocket); };
        auto tq = TaskQueue<SOCKET, decltype(execute), decltype(onDisconnect)>{ 10, 20, execute, onDisconnect };
        while (listenerSocket != INVALID_SOCKET)
        {
            sockaddr  clientAddr{};
            SecureZeroMemory(&clientAddr, sizeof(clientAddr));
            int clientAddrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(
                listenerSocket,
                &clientAddr,
                &clientAddrLen);
            if (clientSocket == INVALID_SOCKET)
            {
                break;
            }

            char ipStr[INET_ADDRSTRLEN]{};
            char cPort[8]{};
            getnameinfo(&clientAddr, clientAddrLen, ipStr, sizeof(ipStr), cPort, sizeof(cPort), NI_NUMERICHOST | NI_NUMERICSERV);
            {
                std::lock_guard<std::mutex> lk(_stdoutMutex);
                std::cout << "\n[Server] New client: " << ipStr << ":" << cPort << std::endl;
            }

            tq.produce(clientSocket);
        }
    }

    closesocket(listenerSocket);
    closesocket(g_serverUDPSocket);
    WSACleanup();
    return 0;
}