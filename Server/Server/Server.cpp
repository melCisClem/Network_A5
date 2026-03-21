/* Start Header
*****************************************************************/
/*!
\file server.cpp
\author 
\par
\date 
\brief

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
#include <cstdio>
#include <iostream>			   // cout, cerr
#include <string>			     // string
#include <fstream>
#include <vector>

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

std::vector<std::string> GetLocalIPv4Addresses(void)
{
	std::vector<std::string> addresses;
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
				inet_ntop(AF_INET, &(sockaddr_ipv4->sin_addr), ipStr, INET_ADDRSTRLEN);
				addresses.push_back(std::string(ipStr));
			}
			freeaddrinfo(result);
		}
	}

	return addresses;
}

bool HandleClient(SOCKET clientSocket)
{
	while (true)
	{
		unsigned char commandID = 0;
		int bytesReceived = recv(clientSocket, reinterpret_cast<char*>(&commandID), 1, 0);
		if (bytesReceived == 0)
		{
			// connection closed by client
			return false;
		}
		if (bytesReceived < 0)
		{
			// error occurred
			int error = WSAGetLastError();
			return false;
		}
		if (commandID == CMD_QUIT)
			return false; // client want to quit
		else if (commandID == CMD_ECHO)
		{
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

			int bytesSent = send(clientSocket, reinterpret_cast<char*>(&commandID), 1, 0);
			if (bytesSent == SOCKET_ERROR)
			{
				delete[] textBuffer;
				return false;
			}

			bytesSent = send(clientSocket, reinterpret_cast<char*>(&textLengthNetwork), sizeof(uint32_t), 0);
			if (bytesSent == SOCKET_ERROR)
			{
				delete[] textBuffer;
				return false;
			}

			int totalSent = 0;
			while (totalSent < static_cast<int>(textLength))
			{
				bytesSent = send(clientSocket, textBuffer + totalSent, textLength - totalSent, 0);
				if (bytesSent == SOCKET_ERROR)
				{
					delete[] textBuffer;
					return false;
				}
				totalSent += bytesSent;
			}
			delete[] textBuffer;
		}
		else
		{
			std::cerr << "Error invalid command" << std::endl;
			return false;
		}
	}
}

int main(int argc, char** argv)
{
	std::string portString;
	std::string bindAddress = "0.0.0.0"; // default

	if (argc == 2) // script mode
	{
		std::ifstream scriptFile(argv[1]);
		if (!scriptFile.is_open())
		{
			std::cerr << "Failed to open script file: " << argv[1] << std::endl;
			return RETURN_CODE_1;
		}

		std::string line;
		if (std::getline(scriptFile, line)) // get port number
			portString = line;

		if (std::getline(scriptFile, line)) // get bind address if it exists
		{
			std::string trimmedLine = trim(line);
			if (!trimmedLine.empty())
				bindAddress = trimmedLine;
		}

		scriptFile.close();
	}
	else // manual
	{
		std::cout << "Server Port Number: ";
		std::getline(std::cin, portString);
	}

	WSADATA wsaData{};
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
	// Resolve own host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------
	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	// For UDP use SOCK_DGRAM instead of SOCK_STREAM.
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 for autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP
	// Create a passive socket that is suitable for bind() and listen().
	hints.ai_flags = AI_PASSIVE;

	addrinfo* info = nullptr;
	errorCode = getaddrinfo(bindAddress.c_str(), portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	// -------------------------------------------------------------------------
	// Create a socket and bind it to own network interface controller.
	//
	// socket()
	// bind()
	// -------------------------------------------------------------------------
	SOCKET listenerSocket = socket(
		hints.ai_family,
		hints.ai_socktype,
		hints.ai_protocol);
	if (INVALID_SOCKET == listenerSocket)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return RETURN_CODE_1;
	}

	errorCode = bind(
		listenerSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));

	freeaddrinfo(info);

	if (NO_ERROR != errorCode)
	{
		std::cerr << "bind() failed." << std::endl;
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}
	if (INVALID_SOCKET == listenerSocket)
	{
		std::cerr << "bind() failed." << std::endl;
		WSACleanup();
		return RETURN_CODE_2;
	}


	// -------------------------------------------------------------------------
	// Set a socket in a listening mode and accept 1 incoming client.
	//
	// listen()
	// accept()
	// -------------------------------------------------------------------------

	errorCode = listen(listenerSocket, SOMAXCONN);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "listen() failed." << std::endl;
		closesocket(listenerSocket);
		WSACleanup();
		return RETURN_CODE_3;
	}

	std::vector<std::string> ipAddresses = GetLocalIPv4Addresses();
	std::cout << "Server is listening on:" << std::endl;
	for (auto const& ip : ipAddresses)
		std::cout << "  " << ip << ":" << portString << std::endl;

	while (true)
	{
		sockaddr clientAddress{};
		SecureZeroMemory(&clientAddress, sizeof(clientAddress));
		int clientAddressSize = sizeof(clientAddress);
		SOCKET clientSocket = accept(
			listenerSocket,
			&clientAddress,
			&clientAddressSize);
		if (INVALID_SOCKET == clientSocket)
		{
			std::cerr << "accept() failed." << std::endl;
			closesocket(listenerSocket);
			WSACleanup();
			return RETURN_CODE_4;
		}

		/* PRINT CLIENT IP ADDRESS AND PORT NUMBER */
		char clientIPAddr[MAX_STR_LEN];
		char clientPort[MAX_STR_LEN];
		getpeername(clientSocket, &clientAddress, &clientAddressSize);
		getnameinfo(&clientAddress, clientAddressSize, clientIPAddr, sizeof(clientIPAddr), clientPort, sizeof(clientPort), NI_NUMERICHOST);

		std::cout << "\nClient connected from " << clientIPAddr << ":" << clientPort << std::endl;

		HandleClient(clientSocket);

		closesocket(clientSocket);
		std::cout << "Client disconnected from " << clientIPAddr << ":" << clientPort << std::endl;
	}

	closesocket(listenerSocket);
	WSACleanup();
}
