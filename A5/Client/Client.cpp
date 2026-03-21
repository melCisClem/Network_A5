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

#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1000
#define BUFFER_SIZE         4096
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

const unsigned char CMD_QUIT = 1;
const unsigned char CMD_ECHO = 2;

std::string trim(const std::string& str)
{
	size_t first = str.find_first_not_of("\t\r\n");
	if (std::string::npos == first)
		return "";
	size_t last = str.find_last_not_of("\t\r\n");
	return str.substr(first, (last - first + 1));
}

bool IsEmptyLine(const std::string& line)
{
	return trim(line).empty();
}

std::vector<unsigned char> HexStringToBytes(const std::string& hexStr)
{
	std::vector<unsigned char> bytes;
	std::string cleanHex;

	for (char c : hexStr) // remove spaces from hex string
		if (c != ' ' && c != '\t')
			cleanHex += c;

	for (size_t i = 0; i < cleanHex.length(); i += 2) // convert pairs of hex digits to bytes
	{
		if (i + 1 < cleanHex.length())
		{
			std::string byteString = cleanHex.substr(i, 2);
			unsigned char byte = static_cast<unsigned char>(std::stoi(byteString, nullptr, 16));
			bytes.push_back(byte);
		}
	}

	return bytes;
}

bool SendEchoMsg(SOCKET clientSocket, const std::string& msg)
{
	// command id + text length + text
	unsigned char commandID = CMD_ECHO;
	uint32_t textLength = static_cast<uint32_t>(msg.length());
	uint32_t textLengthNetwork = htonl(textLength);

	std::vector<char> buffer(1 + sizeof(uint32_t) + textLength);
	buffer[0] = commandID;
	memcpy(&buffer[1], &textLengthNetwork, sizeof(uint32_t));
	memcpy(&buffer[1 + sizeof(uint32_t)], msg.c_str(), textLength);

	int totalSent = 0;
	int totalToSend = static_cast<int>(buffer.size());

	while (totalSent < totalToSend)
	{
		int bytesSent = send(clientSocket, buffer.data() + totalSent, totalToSend - totalSent, 0);
		if (bytesSent == SOCKET_ERROR)
		{
			int error = WSAGetLastError();
			std::cerr << "Failed to send text data, WSA error: " << error << std::endl;
			return false;
		}
		totalSent += bytesSent;
	}
	return true;
}

bool ReceiveEchoResponse(SOCKET clientSocket)
{
	unsigned char commandID = 0;
	int bytesReceived = recv(clientSocket, reinterpret_cast<char*>(&commandID), 1, 0);
	if (bytesReceived <= 0)
		return false;

	uint32_t textLengthNetwork = 0;
	int totalReceived = 0;
	while (totalReceived < sizeof(uint32_t))
	{
		bytesReceived = recv(clientSocket, reinterpret_cast<char*>(&textLengthNetwork) + totalReceived, sizeof(uint32_t) - totalReceived, 0);
		if (bytesReceived <= 0)
			return false;
		totalReceived += bytesReceived;
	}

	uint32_t textLength = ntohl(textLengthNetwork);
	char* textBuffer = new char[textLength + 1];
	totalReceived = 0;

	while (totalReceived < static_cast<int>(textLength))
	{
		bytesReceived = recv(clientSocket, textBuffer + totalReceived, textLength - totalReceived, 0);
		if (bytesReceived <= 0)
		{
			delete[] textBuffer;
			return false;
		}
		totalReceived += bytesReceived;
	}

	textBuffer[textLength] = '\0'; // null terminate
	std::cout << textBuffer << std::endl; // echo

	delete[] textBuffer;
	return true;
}

bool SendQuitMsg(SOCKET clientSocket)
{
	unsigned char commandID = CMD_QUIT;
	if (send(clientSocket, reinterpret_cast<char*>(&commandID), 1, 0) == SOCKET_ERROR)
		return false;
	return true;
}

bool SendRawBytes(SOCKET clientSocket, const std::vector<unsigned char>& bytes)
{
	int totalSent = 0;
	while (totalSent < static_cast<int>(bytes.size()))
	{
		int bytesSent = send(clientSocket, reinterpret_cast<const char*>(bytes.data()) + totalSent, static_cast<int>(bytes.size()) - totalSent, 0);
		if (bytesSent == SOCKET_ERROR)
			return false;
		totalSent += bytesSent;
	}
	return true;
}

bool ProcessMessage(SOCKET clientSocket, const std::string& message)
{
	// process /q
	if (message == "/q")
	{
		SendQuitMsg(clientSocket);
		return false;
	}

	// process /t
	if (message.length() >= 2 && message.substr(0, 2) == "/t")
	{
		std::string hexPart = message.substr(2);
		std::vector<unsigned char> rawBytes = HexStringToBytes(hexPart);

		if (!SendRawBytes(clientSocket, rawBytes))
		{
			std::cerr << "Failed to send raw bytes" << std::endl;
			return false;
		}

		if (!ReceiveEchoResponse(clientSocket))
		{
			std::cerr << "Failed to receive echo after /t" << std::endl;
			return false;
		}

		return true;
	}

	// process normal msg
	if (!SendEchoMsg(clientSocket, message))
	{
		std::cerr << "Failed to send message" << std::endl;
		return false;
	}

	if (!ReceiveEchoResponse(clientSocket))
	{
		std::cerr << "Failed to receive echo" << std::endl;
		return false;
	}

	return true;
}

// This program requires one extra command-line parameter: a server hostname.
int main(int argc, char** argv)
{
	std::string host;
	std::string portString;
	std::vector<std::string> msgs;
	bool scriptMode = false;

	if (argc == 2) // script mode
	{
		scriptMode = true;
		std::ifstream scriptFile(argv[1]);
		if (!scriptFile.is_open())
		{
			std::cerr << "Failed to open script file: " << argv[1] << std::endl;
			return RETURN_CODE_1;
		}

		std::string line;

		if (std::getline(scriptFile, line))
			host = trim(line);

		if (std::getline(scriptFile, line))
			portString = trim(line);

		while (std::getline(scriptFile, line))
		{
			// remove window's trailing /r
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (!IsEmptyLine(line))
				msgs.push_back(line);
		}
		scriptFile.close();
	}
	else // manual
	{
		std::cout << "Server IP Address: ";
		std::getline(std::cin, host);

		std::cout << "Server Port Number: ";
		std::getline(std::cin, portString);
	}

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


	// -------------------------------------------------------------------------
	// Resolve a server host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 to autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP

	addrinfo* info = nullptr;
	errorCode = getaddrinfo(host.c_str(), portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}


	// -------------------------------------------------------------------------
	// Create a socket and attempt to connect to the first resolved address.
	//
	// socket()
	// connect()
	// -------------------------------------------------------------------------

	SOCKET clientSocket = socket(
		info->ai_family,
		info->ai_socktype,
		info->ai_protocol);
	if (INVALID_SOCKET == clientSocket)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return RETURN_CODE_2;
	}

	errorCode = connect(
		clientSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));

	freeaddrinfo(info);

	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "connect() failed." << std::endl;
		closesocket(clientSocket);
		WSACleanup();
		return RETURN_CODE_3;
	}

	if (scriptMode) // script
	{
		for (const auto& message : msgs)
		{
			if (!ProcessMessage(clientSocket, message))
				break; // /q or error occurred
		}
	}
	else // manual
	{
		while (true)
		{
			std::cout << "Enter message (or /q to quit): ";
			std::string message;
			std::getline(std::cin, message);

			// skip empty lines
			if (IsEmptyLine(message))
				continue;

			if (!ProcessMessage(clientSocket, message))
				break; // /q or error occurred
		}
	}


	shutdown(clientSocket, SD_SEND);
	closesocket(clientSocket);
	WSACleanup();
}
