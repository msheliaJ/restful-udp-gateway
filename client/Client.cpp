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

// ----------------------------------------------------------------------------
// Application layer protocol (identical to the server)
// ----------------------------------------------------------------------------
struct Header
{
    unsigned char Flag; // 0 = request last PersonData, 1 = data transmission
    unsigned char Size; // number of bytes of Name (0 when Flag = 0)
};

struct PersonData
{
    char *Name;            // not null-terminated; length is Header.Size
    unsigned int IDNumber; // always 4 bytes
};

struct Pkt
{
    Header Head;     // always present
    PersonData Body; // only present when Flag = 1
};

// Owns the packet's dynamic memory and handles (de)serialization.
class AppPacket
{
    Pkt packet;
    char *TxBuffer;
    bool valid;

public:
    AppPacket() : TxBuffer(nullptr), valid(true)
    {
        memset(&packet.Head, 0, sizeof(packet.Head));
        packet.Body.Name = nullptr;
        packet.Body.IDNumber = 0;
    }

    // Deserialize a received datagram, bounds-checked against its length.
    AppPacket(const char *src, int len) : AppPacket()
    {
        if (len < (int)sizeof(Header)) { valid = false; return; }
        memcpy(&packet.Head, src, sizeof(Header));

        if (packet.Head.Flag == 1)
        {
            int needed = sizeof(Header) + packet.Head.Size + sizeof(unsigned int);
            if (len < needed) { valid = false; return; }

            packet.Body.Name = new char[packet.Head.Size];
            memcpy(packet.Body.Name, src + sizeof(Header), packet.Head.Size);
            memcpy(&packet.Body.IDNumber, src + sizeof(Header) + packet.Head.Size, sizeof(unsigned int));
        }
        else if (packet.Head.Flag != 0)
        {
            valid = false;
        }
    }

    // Owns raw pointers, so copying would double-free.
    AppPacket(const AppPacket &) = delete;
    AppPacket &operator=(const AppPacket &) = delete;

    void SetData(const char *name, int size, unsigned int id)
    {
        if (size > 255) size = 255; // Size field is one byte
        delete[] packet.Body.Name;
        packet.Head.Flag = 1;
        packet.Head.Size = (unsigned char)size;
        packet.Body.Name = new char[packet.Head.Size];
        memcpy(packet.Body.Name, name, packet.Head.Size);
        packet.Body.IDNumber = id;
    }

    void SetRequest()
    {
        delete[] packet.Body.Name;
        packet.Body.Name = nullptr;
        packet.Head.Flag = 0;
        packet.Head.Size = 0;
    }

    // Layout: [Header][Name bytes][IDNumber] for data, [Header] for requests.
    char *SerializeData(int &TotalSize)
    {
        delete[] TxBuffer;

        if (packet.Head.Flag == 1)
        {
            TotalSize = sizeof(Header) + packet.Head.Size + sizeof(unsigned int);
            TxBuffer = new char[TotalSize];
            memcpy(TxBuffer, &packet.Head, sizeof(Header));
            memcpy(TxBuffer + sizeof(Header), packet.Body.Name, packet.Head.Size);
            memcpy(TxBuffer + sizeof(Header) + packet.Head.Size, &packet.Body.IDNumber, sizeof(unsigned int));
        }
        else
        {
            TotalSize = sizeof(Header);
            TxBuffer = new char[TotalSize];
            memcpy(TxBuffer, &packet.Head, sizeof(Header));
        }
        return TxBuffer;
    }

    void Display() const
    {
        cout << "Name: " << GetName() << endl;
        cout << "ID: " << packet.Body.IDNumber << endl;
    }

    bool IsValid() const { return valid; }
    Header GetHead() const { return packet.Head; }
    string GetName() const
    {
        return packet.Body.Name ? string(packet.Body.Name, packet.Head.Size) : string();
    }
    unsigned int GetIDNumber() const { return packet.Body.IDNumber; }

    ~AppPacket()
    {
        delete[] packet.Body.Name;
        delete[] TxBuffer;
    }
};

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

        if (pkt.GetHead().Flag == 1)
        {
            // Store and print - no reply for data packets
            lastName = pkt.GetName();
            lastID = pkt.GetIDNumber();
            haveContact = true;

            cout << "Received Contact:" << endl;
            pkt.Display();
        }
        else
        {
            AppPacket response;
            if (haveContact)
            {
                cout << "SENDING THE FOLLOWING:" << endl;
                response.SetData(lastName.c_str(), (int)lastName.size(), lastID);
                response.Display();
            }
            else
            {
                // Reply anyway so the server isn't left waiting
                cout << "No contact received yet." << endl;
                response.SetData("None", 4, 0);
            }

            int txSize = 0;
            char *txBuf = response.SerializeData(txSize);
            sendto(ClientSocket, txBuf, txSize, 0, (sockaddr *)&fromAddr, fromLen);
        }
    }

    closesocket(ClientSocket);
    WSACleanup();
    return 0;
}
