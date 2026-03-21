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

#include "Windows.h"		// Entire Win32 API...
#include "winsock2.h"		// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()

// Tell the Visual Studio linker to include the following library in linking.
// Alternatively, we could add this file to the linker command-line parameters,
// but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")

#include <iostream>			// cout, cerr
#include <string>			// string
#include <vector>
#include <fstream>
#include <thread>
#include <chrono>
#include <map>
#include <queue>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <algorithm>

#include <GLFW/glfw3.h>

namespace fs = std::filesystem;

#define WINSOCK_VERSION     2
#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1024
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

// defaults if config file not found
static uint32_t UDP_CHUNK_SIZE = 60000;  // bytes per UDP data packet

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

SOCKET g_tcpSocket = INVALID_SOCKET;
SOCKET g_udpSocket = INVALID_SOCKET;
std::string g_downloadPath;
std::atomic<bool> g_running{ true };

std::string g_clientIPStr;
uint16_t g_clientUDPPort = 0;

uint16_t g_serverUDPPortHost = 0;
sockaddr_in g_serverUDPAddr{};
std::mutex g_serverUDPAddrMtx;

std::queue<std::string> g_pendingFilenames;
std::mutex g_pendingMtx;

struct DownloadSession {
    uint32_t sessionID;
    uint32_t fileSize;
    uint32_t expectedSeq;
    uint32_t bytesWritten;
    std::string filename;
    std::string savePath;
    std::string tmpPath;
    std::ofstream outFile;
    bool complete;
};

std::map<uint32_t, DownloadSession*> g_downloads;
std::mutex g_downloadsMtx;

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

// Load config.txt — same function as server so both share the same file.
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
        }
        catch (...)
        {
            std::cerr << "[Config] Bad value for " << key << ": " << val << "\n";
        }
    }
    std::cout << "[Config] Loaded from \"" << path << "\":\n" << "  UDP_CHUNK_SIZE = " << UDP_CHUNK_SIZE << " bytes\n";
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
* @brief trim crlf
* @param std::string& s
*/
static void trimRight(std::string& s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
        s.pop_back();
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

static std::vector<std::string> g_localIPs;

/**
* @brief UDP reciever thread
*/
static void udpReceiverThread(void)
{
    std::vector<char> buf(sizeof(UDPHeader) + UDP_CHUNK_SIZE + 128);
    sockaddr_in from{};
    int fromLen = sizeof(from);

    while (g_running)
    {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(g_udpSocket, &rs);

        timeval tv{ 1, 0 };
        int sel = select(0, &rs, nullptr, nullptr, &tv);
        if (sel == SOCKET_ERROR)
        {
            int e = WSAGetLastError();

            if (e == WSAENOTSOCK || e == WSAESHUTDOWN)
                break;
            std::cerr << "[UDP] select() transient error " << e << ", retrying (cable break?)" << std::endl;
            continue; // survive ethernet break
        }
        if (sel == 0)
            continue;

        int r = recvfrom(g_udpSocket, buf.data(), (int)buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (r == SOCKET_ERROR)
        {
            int e = WSAGetLastError();

            if (e == WSAENOTSOCK || e == WSAESHUTDOWN)
                break;
            std::cerr << "[UDP] recvfrom() transient error " << e << ", retrying (cable break?)" << std::endl;
            continue; // survive ethernet break
        }
        if (r < (int)sizeof(UDPHeader))
            continue;

        auto* hdr = reinterpret_cast<UDPHeader*>(buf.data());
        if (hdr->flags & 0x01)
            continue;

        uint32_t sessionID = ntohl(hdr->sessionID);
        uint32_t fileLength = ntohl(hdr->fileLength);
        uint32_t fileOffset = ntohl(hdr->fileOffset);
        uint32_t dataLength = ntohl(hdr->dataLength);
        uint32_t seqNum = ntohl(hdr->seqNum);

        {
            std::lock_guard<std::mutex> dlk(g_downloadsMtx);
            auto it = g_downloads.find(sessionID);
            if (it == g_downloads.end())
            {
                std::cout << "[UDP] Session " << sessionID << " not ready yet (seq=" << seqNum << "), server will retry." << std::endl;
                continue; // no ack sent will retrainsmit after timeout
            }

            DownloadSession* ds = it->second;
            if (ds->complete)
                continue;

            // sess known
            {
                UDPHeader ack{};
                ack.sessionID = hdr->sessionID;
                ack.fileLength = 0;
                ack.fileOffset = 0;
                ack.dataLength = 0;
                ack.flags = 0x01;
                ack.seqNum = 0;
                ack.ackNum = hdr->seqNum;

                std::lock_guard<std::mutex> lk(g_serverUDPAddrMtx);
                sendto(g_udpSocket,
                    reinterpret_cast<char*>(&ack), sizeof(ack), 0,
                    reinterpret_cast<sockaddr*>(&g_serverUDPAddr),
                    sizeof(g_serverUDPAddr)
                );
            }

            if (seqNum == ds->expectedSeq) {
                if (dataLength > 0)
                {
                    ds->outFile.write(buf.data() + sizeof(UDPHeader), dataLength);
                    ds->outFile.flush();
                    if (!ds->outFile.good())
                        std::cerr << "[DL " << sessionID << "] Write error seq=" << seqNum << std::endl;
                    ds->bytesWritten += dataLength;
                }
                ds->expectedSeq++;

                if (ds->fileSize > 0) // download prog perc
                {
                    uint32_t pct = static_cast<uint32_t>((static_cast<uint64_t>(ds->bytesWritten) * 100ULL) / static_cast<uint64_t>(ds->fileSize));
                    std::cout << "[DL " << sessionID << "] Progress: " << ds->bytesWritten << "/" << ds->fileSize << " bytes (" << pct << "%)" << std::endl;
                }
                else
                    std::cout << "[DL " << sessionID << "] Received empty file packet." << std::endl;

                // check if done
                bool done = (fileLength == 0) || (ds->bytesWritten >= ds->fileSize);
                if (done)
                {
                    ds->outFile.flush();
                    ds->outFile.close();
                    ds->complete = true;

                    BOOL moved = MoveFileExA(
                        ds->tmpPath.c_str(),
                        ds->savePath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
                    );

                    if (!moved)
                    {
                        DWORD err = GetLastError();
                        std::cerr << "\n[DL " << sessionID
                            << "] ERROR: Could not save \""
                            << ds->savePath << "\" (WinErr=" << err << ")\n"
                            << "  Temp file kept at: " << ds->tmpPath
                            << std::endl;
                    }
                    else
                        std::cout << "\n"
                        << "========================================\n"
                        << "[DL " << sessionID << "] DOWNLOAD COMPLETE\n"
                        << "  File : " << ds->savePath << "\n"
                        << "  Size : " << ds->fileSize << " bytes\n"
                        << "========================================\n";
                    delete ds;
                    g_downloads.erase(it);
                }
            }
            // else is dup seqnum so ignore
        }
    }
}

/**
* @brief TCP receiver thread
*/
static void tCommuThread(void)
{
    char cmdBuf[1];

    while (g_running)
    {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(g_tcpSocket, &rs);

        timeval tv{ 1, 0 };
        int sel = select(0, &rs, nullptr, nullptr, &tv);
        if (sel == SOCKET_ERROR)
            break;
        if (sel == 0)
            continue;

        int r = recv(g_tcpSocket, cmdBuf, 1, 0);
        if (r <= 0)
        {
            std::cout << "\n[Client] Server disconnected." << std::endl;
            g_running = false;
            break;
        }
        unsigned char cmd = static_cast<unsigned char>(cmdBuf[0]);

        if (cmd == RSP_LISTFILES)
        {
            uint16_t numFilesNet{};
            uint32_t listLenNet{};
            if (!recvAll(g_tcpSocket, &numFilesNet, 2))
            {
                g_running = false;
                break;
            }

            if (!recvAll(g_tcpSocket, &listLenNet, 4))
            {
                g_running = false;
                break;
            }

            uint16_t numFiles = ntohs(numFilesNet);
            std::cout << "\n=== Files available (" << numFiles << ") ===" << std::endl;

            bool ok = true;
            for (uint16_t i = 0; i < numFiles && ok; ++i)
            {
                uint32_t nl_net{};
                if (!recvAll(g_tcpSocket, &nl_net, 4))
                {
                    ok = false;
                    break;
                }
                uint32_t nl = ntohl(nl_net);

                std::vector<char> nameBuf(nl + 1, '\0');
                if (!recvAll(g_tcpSocket, nameBuf.data(), static_cast<int>(nl)))
                {
                    ok = false;
                    break;
                }
                std::cout << "  " << (i + 1) << ". " << nameBuf.data() << std::endl;
            }
            std::cout << "==============================" << std::endl;

            if (!ok)
            {
                g_running = false;
                break;
            }
        }
        else if (cmd == RSP_DOWNLOAD)
        {
            uint32_t serverIP_net{};
            uint16_t serverPort_net{};
            uint32_t sessionID_net{};
            uint32_t fileSize_net{};

            bool ok = recvAll(g_tcpSocket, &serverIP_net, 4)
                && recvAll(g_tcpSocket, &serverPort_net, 2)
                && recvAll(g_tcpSocket, &sessionID_net, 4)
                && recvAll(g_tcpSocket, &fileSize_net, 4);

            if (!ok)
            {
                g_running = false;
                break;
            }

            uint32_t sessionID = ntohl(sessionID_net);
            uint32_t fileSize = ntohl(fileSize_net);

            char srvIPStr[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &serverIP_net, srvIPStr, sizeof(srvIPStr));
            {
                std::lock_guard<std::mutex> lk(g_serverUDPAddrMtx);
                g_serverUDPAddr.sin_family = AF_INET;
                g_serverUDPAddr.sin_addr.s_addr = serverIP_net;
                g_serverUDPAddr.sin_port = serverPort_net;
            }
            std::cout << "[RSP_DOWNLOAD] Server UDP endpoint: "
                << srvIPStr << ":" << ntohs(serverPort_net)
                << "  Session=" << sessionID
                << "  Size=" << fileSize << " bytes" << std::endl;

            std::string filename;
            {
                std::lock_guard<std::mutex> lk(g_pendingMtx);
                if (!g_pendingFilenames.empty())
                {
                    std::string raw = g_pendingFilenames.front();
                    g_pendingFilenames.pop();
                    filename = fs::path(raw).filename().string();
                }
            }

            fs::path savePath = fs::path(g_downloadPath) / filename;
            fs::path tmpPath = savePath;
            tmpPath += ".downloading";

            std::cout << "\n[DL " << sessionID << "] Starting: \"" << filename << "\" (" << fileSize << " bytes)" << std::endl;

            // create sess
            auto* ds = new DownloadSession();
            ds->sessionID = sessionID;
            ds->fileSize = fileSize;
            ds->expectedSeq = 0;
            ds->bytesWritten = 0;
            ds->filename = filename;
            ds->savePath = savePath.string();
            ds->tmpPath = tmpPath.string();
            ds->complete = false;

            try
            {
                fs::create_directories(savePath.parent_path());
            }
            catch (...) {}

            ds->outFile.open(ds->tmpPath, std::ios::binary | std::ios::trunc);
            if (!ds->outFile.is_open())
            {
                std::cerr << "[Client] Cannot open temp file: " << ds->tmpPath << std::endl;
                delete ds;
                continue;
            }

            {
                std::lock_guard<std::mutex> lk(g_downloadsMtx);
                g_downloads[sessionID] = ds;
            }

            // if get 0byte file server send 1 empty udp packet and thread handles n closes file
        }
        else if (cmd == DOWNLOAD_ERROR)
        {
            std::cerr << "\n========================================\n"
                << "[Client] DOWNLOAD_ERROR from server.\n"
                << "  Either the file was not found, or the UDP transfer\n"
                << "  timed out (e.g. wrong IP/port was given in /d).\n"
                << "========================================\n";

            {
                std::lock_guard<std::mutex> lk(g_pendingMtx);
                if (!g_pendingFilenames.empty())
                    g_pendingFilenames.pop();
            }

            {
                std::lock_guard<std::mutex> lk(g_downloadsMtx);
                for (auto it = g_downloads.begin(); it != g_downloads.end(); )
                {
                    DownloadSession* ds = it->second;
                    if (ds->outFile.is_open())
                        ds->outFile.close();

                    std::error_code ec;
                    fs::remove(ds->tmpPath, ec);

                    std::cerr << "[Client] Removed incomplete temp file: " << ds->tmpPath << std::endl;
                    delete ds;
                    it = g_downloads.erase(it);
                }
            }
        }
        else
            std::cerr << "[Client] Unknown TCP command: 0x" << std::hex << static_cast<int>(cmd) << std::dec << std::endl;
    }
}

int main(int argc, char** argv)
{
    loadConfig("config.txt");

    std::string serverIPStr, tcpPortStr, serverUDPPortStr, clientUDPPortStr;
    std::vector<std::string> scriptCmds;

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
            serverIPStr = trim(line);
        if (std::getline(scriptFile, line))
            tcpPortStr = trim(line);
        if (std::getline(scriptFile, line))
            serverUDPPortStr = trim(line);
        if (std::getline(scriptFile, line))
            clientUDPPortStr = trim(line);
        if (std::getline(scriptFile, line))
            g_downloadPath = trim(line);
        while (std::getline(scriptFile, line))
        {
            std::string t = trim(line);
            if (!t.empty())
                scriptCmds.push_back(t);
        }
        scriptFile.close();

        std::cout << "Server IP Address: " << serverIPStr << "\n";
        std::cout << "Server TCP Port Number: " << tcpPortStr << "\n";
        std::cout << "Server UDP Port Number: " << serverUDPPortStr << "\n";
        std::cout << "Client UDP Port Number: " << clientUDPPortStr << "\n";
        std::cout << "Path: " << g_downloadPath << "\n";
    }
    else
    {
        std::cout << "Server IP Address: ";
        std::getline(std::cin, serverIPStr);
        std::cout << std::endl;

        std::cout << "Server TCP Port Number: ";
        std::getline(std::cin, tcpPortStr);
        std::cout << std::endl;

        std::cout << "Server UDP Port Number: ";
        std::getline(std::cin, serverUDPPortStr);
        std::cout << std::endl;

        std::cout << "Client UDP Port Number: ";
        std::getline(std::cin, clientUDPPortStr);
        std::cout << std::endl;

        std::cout << "Path: ";
        std::getline(std::cin, g_downloadPath);
        std::cout << std::endl;
    }

    trimRight(serverIPStr);
    trimRight(tcpPortStr);
    trimRight(serverUDPPortStr);
    trimRight(clientUDPPortStr);
    trimRight(g_downloadPath);

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
    errorCode = getaddrinfo(serverIPStr.c_str(), tcpPortStr.c_str(), &tcpHints, &tcpInfo);
    if (NO_ERROR != errorCode || !tcpInfo)
    {
        std::cerr << "getaddrinfo() failed for TCP." << std::endl;
        WSACleanup();
        return RETURN_CODE_1;
    }

    g_tcpSocket = socket(tcpHints.ai_family, tcpHints.ai_socktype, tcpHints.ai_protocol);
    if (INVALID_SOCKET == g_tcpSocket)
    {
        std::cerr << "socket() TCP failed." << std::endl;
        freeaddrinfo(tcpInfo);
        WSACleanup();
        return RETURN_CODE_2;
    }

    errorCode = connect(g_tcpSocket, tcpInfo->ai_addr, static_cast<int>(tcpInfo->ai_addrlen));
    freeaddrinfo(tcpInfo);
    if (SOCKET_ERROR == errorCode)
    {
        std::cerr << "connect() failed." << std::endl;
        closesocket(g_tcpSocket);
        WSACleanup();
        return RETURN_CODE_3;
    }
    std::cout << "Connected to server." << std::endl;

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

    g_udpSocket = socket(udpHints.ai_family, udpHints.ai_socktype, udpHints.ai_protocol);
    if (g_udpSocket == INVALID_SOCKET)
    {
        std::cerr << "socket() UDP failed." << std::endl;
        closesocket(g_tcpSocket);
        WSACleanup();
        return RETURN_CODE_2;
    }

    sockaddr_in udpBind{};
    udpBind.sin_family = AF_INET;
    udpBind.sin_addr.s_addr = INADDR_ANY;
    udpBind.sin_port = htons(static_cast<uint16_t>(std::stoi(clientUDPPortStr)));

    errorCode = bind(g_udpSocket, reinterpret_cast<sockaddr*>(&udpBind), sizeof(udpBind));
    if (errorCode != NO_ERROR)
    {
        std::cerr << "bind() UDP failed: " << WSAGetLastError() << std::endl;
        closesocket(g_tcpSocket);
        closesocket(g_udpSocket);
        WSACleanup();
        return RETURN_CODE_2;
    }

    int rcvBuf = 4 * 1024 * 1024;
    setsockopt(g_udpSocket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcvBuf), sizeof(rcvBuf));

    sockaddr_in localAddr{};
    int localLen = sizeof(localAddr);
    getsockname(g_tcpSocket, reinterpret_cast<sockaddr*>(&localAddr), &localLen);
    char clientIPBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &localAddr.sin_addr, clientIPBuf, sizeof(clientIPBuf));
    g_clientIPStr = clientIPBuf;
    g_clientUDPPort = static_cast<uint16_t>(std::stoi(clientUDPPortStr));
    g_serverUDPPortHost = static_cast<uint16_t>(std::stoi(serverUDPPortStr));

    g_localIPs = GetLocalIPv4Addresses();
    if (std::find(g_localIPs.begin(), g_localIPs.end(), g_clientIPStr) == g_localIPs.end())
        g_localIPs.push_back(g_clientIPStr);

    std::cout << "Client IP:       " << g_clientIPStr << std::endl;
    std::cout << "Client UDP Port: " << g_clientUDPPort << std::endl;
    std::cout << "\nCommands:\n"
        << "  /l                   - list files\n"
        << "  /d <filename>        - download file (auto fills ip n port)\n"
        << "  /d <IP>:<Port> <filename>  -- download file\n"
        << "  /q                   - quit\n\n";

    std::thread tCommu(tCommuThread);
    std::thread tUDP(udpReceiverThread);

    std::string input;
    size_t scriptIdx = 0;
    while (g_running)
    {
        if (scriptIdx < scriptCmds.size())
        {
            input = scriptCmds[scriptIdx++];
            std::cout << "> " << input << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        else if (!std::getline(std::cin, input))
        {
            g_running = false;
            break;
        }
        trimRight(input);
        if (input.empty())
            continue;

        if (input == "/l")
        {
            char c = static_cast<char>(REQ_LISTFILES);
            sendAll(g_tcpSocket, &c, 1);
        }
        else if (input.size() > 3 && input.substr(0, 3) == "/d ")
        {
            // /d <filename>
            // /d <IP:Port> <filename>

            std::string rest = input.substr(3);

            std::string dlIP = g_clientIPStr;
            uint16_t dlPort = g_clientUDPPort;
            std::string filename;

            size_t sp = rest.find(' ');
            size_t colon = (sp != std::string::npos) ? rest.substr(0, sp).rfind(':') : std::string::npos;

            if (sp != std::string::npos && colon != std::string::npos)
            {
                std::string addrStr = rest.substr(0, sp);
                filename = rest.substr(sp + 1);
                std::string portStr = addrStr.substr(colon + 1);
                dlIP = addrStr.substr(0, colon);

                try
                {
                    dlPort = static_cast<uint16_t>(std::stoi(portStr));
                }
                catch (...)
                {
                    std::cerr << "Invalid port." << std::endl;
                    continue;
                }
            }
            else // auto fill ver
            {
                filename = rest;
                std::cout << "[/d] Using client address " << dlIP << ":" << dlPort << " (auto-filled)" << std::endl;
            }

            if (filename.empty())
            {
                std::cerr << "Usage: /d <filename>  OR  /d <IP:Port> <filename>" << std::endl;
                continue;
            }

            if (dlPort == g_serverUDPPortHost)
            {
                std::cerr << "Error: port " << dlPort
                    << " is the server's UDP port, not your client port.\n"
                    << "Use your Client UDP Port (" << g_clientUDPPort
                    << ") instead.  Example:\n"
                    << "  /d " << g_clientIPStr << ":" << g_clientUDPPort
                    << " " << filename << std::endl;
                continue;
            }

            // check ip n port
            bool ipIsLocal = (std::find(g_localIPs.begin(), g_localIPs.end(), dlIP) != g_localIPs.end());
            if (!ipIsLocal || dlPort != g_clientUDPPort)
            {
                std::cerr << "Error: the IP:Port in /d must be your own address.\n"
                    << "  You entered : " << dlIP << ":" << dlPort << "\n"
                    << "  Your local IPs: ";

                for (size_t i = 0; i < g_localIPs.size(); ++i)
                {
                    if (i)
                        std::cerr << ", ";
                    std::cerr << g_localIPs[i];
                }
                std::cerr << "\n  Client UDP port: " << g_clientUDPPort << "\n"
                    << "Use: /d " << g_clientIPStr << ":" << g_clientUDPPort
                    << " " << filename << std::endl;
                continue;
            }

            // validate ip addr
            in_addr dlAddr{};
            if (inet_pton(AF_INET, dlIP.c_str(), &dlAddr) != 1)
            {
                std::cerr << "Invalid IP address: " << dlIP << std::endl;
                continue;
            }

            // build msg
            std::vector<char> msg;
            msg.push_back(static_cast<char>(REQ_DOWNLOAD));

            // client IP (4 byte, network order)
            msg.insert(msg.end(), reinterpret_cast<char*>(&dlAddr.s_addr), reinterpret_cast<char*>(&dlAddr.s_addr) + 4);

            // client UDP port (2 byte, network order)
            uint16_t portNet = htons(dlPort);
            msg.insert(msg.end(), reinterpret_cast<char*>(&portNet), reinterpret_cast<char*>(&portNet) + 2);

            // filenam length + filename
            uint32_t fnLen = static_cast<uint32_t>(filename.size());
            uint32_t fnLenNet = htonl(fnLen);
            msg.insert(msg.end(), reinterpret_cast<char*>(&fnLenNet), reinterpret_cast<char*>(&fnLenNet) + 4);
            msg.insert(msg.end(), filename.begin(), filename.end());

            // record pending filename b4 send T_Commu will eat it
            {
                std::lock_guard<std::mutex> lk(g_pendingMtx);
                g_pendingFilenames.push(filename);
            }

            sendAll(g_tcpSocket, msg.data(), (int)msg.size());
            std::cout << "Requesting download: " << filename << std::endl;
        }
        else if (input == "/q")
        {
            char c = static_cast<char>(REQ_QUIT);
            sendAll(g_tcpSocket, &c, 1);
            g_running = false;
        }
        else
            std::cerr << "Unknown command: " << input << std::endl;
    }

    g_running = false;

    // Clean up
    shutdown(g_tcpSocket, SD_BOTH);
    closesocket(g_tcpSocket);
    closesocket(g_udpSocket);

    if (tCommu.joinable()) tCommu.join();
    if (tUDP.joinable())   tUDP.join();

    // Release any incomplete download sessions
    {
        std::lock_guard<std::mutex> lk(g_downloadsMtx);
        for (auto& [id, ds] : g_downloads) {
            if (ds->outFile.is_open()) ds->outFile.close();
            delete ds;
        }
        g_downloads.clear();
    }

    WSACleanup();
    return 0;
}