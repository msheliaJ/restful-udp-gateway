// ============================================================================
// Remote Windows Client - UDP console application
//
// Listens on UDP port 22150 and speaks the project's application-layer
// protocol (see README.md):
//   Flag = 1 (data)    -> store the PersonData and print it
//   Flag = 0 (request) -> print "SENDING THE FOLLOWING:" and reply with the
//                         last PersonData received
// ============================================================================
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#include <iostream>
#include <string>
#include <cstring>
using namespace std;

static const unsigned short CLIENT_PORT = 22150;

// Application layer protocol: shared with the server
#include "../common/protocol.h"
using protocol::AppPacket;
using protocol::FLAG_DATA;

static void Display(const AppPacket &pkt)
{
    cout << "Name: " << pkt.GetName() << endl;
    cout << "ID: " << pkt.GetIDNumber() << endl;
}

int main()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed" << endl;
        return 1;
    }

    SOCKET ClientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ClientSocket == INVALID_SOCKET)
    {
        cerr << "Could not create socket: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in clientAddr{};
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(CLIENT_PORT);
    clientAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ClientSocket, (sockaddr *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR)
    {
        cerr << "Could not bind to port " << CLIENT_PORT << ": " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }

    cout << "Client bound to port " << CLIENT_PORT << ". Waiting for packets..." << endl;

    // Last PersonData received
    bool haveContact = false;
    string lastName;
    unsigned int lastID = 0;

    char recvBuf[512];

    while (true)
    {
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int bytesReceived = recvfrom(ClientSocket, recvBuf, sizeof(recvBuf), 0,
                                     (sockaddr *)&fromAddr, &fromLen);

        if (bytesReceived == SOCKET_ERROR)
        {
            cerr << "Error receiving: " << WSAGetLastError() << endl;
            continue;
        }

        AppPacket pkt(recvBuf, bytesReceived);
        if (!pkt.IsValid())
        {
            cerr << "Ignored malformed packet (" << bytesReceived << " bytes)" << endl;
            continue;
        }

        if (pkt.GetHead().Flag == FLAG_DATA)
        {
            // Store and print - no reply for data packets
            lastName = pkt.GetName();
            lastID = pkt.GetIDNumber();
            haveContact = true;

            cout << "Received Contact:" << endl;
            Display(pkt);
        }
        else
        {
            AppPacket response;
            if (haveContact)
            {
                cout << "SENDING THE FOLLOWING:" << endl;
                response.SetData(lastName, lastID);
                Display(response);
            }
            else
            {
                // Reply anyway so the server isn't left waiting
                cout << "No contact received yet." << endl;
                response.SetData("None", 0);
            }

            int txSize = 0;
            const char *txBuf = response.SerializeData(txSize);
            sendto(ClientSocket, txBuf, txSize, 0, (sockaddr *)&fromAddr, fromLen);
        }
    }

    closesocket(ClientSocket);
    WSACleanup();
    return 0;
}
