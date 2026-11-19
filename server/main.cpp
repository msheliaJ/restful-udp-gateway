// ============================================================================
// RESTful Cloud Service - Crow web server (runs inside Docker)
//
// Acts as the middle tier between a web browser (HTTP) and a remote Windows
// console client (UDP). Contact data is sent to the client using a custom
// application-layer protocol (see README.md for the packet design).
//
// Routes
//   GET  /                                   -> serves public/index.html
//   PUT  /setup_connection/<ip>/<port>       -> sets UDP target for the client
//   POST /new_contact/?FirstName=&LastName=&IDNumber=
//                                            -> sends PersonData to the client
//   GET  /last_contact/                      -> asks the client for the last
//                                               PersonData it received
// ============================================================================
#define CROW_MAIN
#include "crow_all.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using namespace std;

// ----------------------------------------------------------------------------
// Application layer protocol
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

    // Deserialize a received buffer. Bounds-checked against the real
    // datagram length so a malformed packet can't cause an over-read.
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

// ----------------------------------------------------------------------------
// UDP connection settings (set by /setup_connection/)
// ----------------------------------------------------------------------------
static string UDPAddress;
static int UDPPort = 0;
static mutex UDPMutex; // Crow is multithreaded

static const int HTTP_PORT = 28001;
static const int UDP_REPLY_TIMEOUT_SEC = 3;

static bool GetTarget(sockaddr_in &addr)
{
    lock_guard<mutex> lock(UDPMutex);
    if (UDPAddress.empty() || UDPPort <= 0)
        return false;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDPPort);
    return inet_pton(AF_INET, UDPAddress.c_str(), &addr.sin_addr) == 1;
}

static void TextResponse(crow::response &res, int code, const string &body)
{
    res.code = code;
    res.set_header("Content-Type", "text/plain");
    res.write(body);
    res.end();
}

// Loads a file from disk and writes it to the HTTP response.
static void loadFile(const string &filename, const string &contentType, crow::response &res)
{
    ifstream in(filename, ifstream::in);
    if (!in)
    {
        TextResponse(res, 404, "Not Found");
        return;
    }
    ostringstream contents;
    contents << in.rdbuf();
    res.set_header("Content-Type", contentType);
    res.write(contents.str());
    res.end();
}

int main()
{
    crow::SimpleApp ProjectApp;

    // Root: the browser client UI
    CROW_ROUTE(ProjectApp, "/")
    ([](const crow::request &, crow::response &res) {
        loadFile("public/index.html", "text/html", res);
    });

    // Store the IP/port of the Windows client for UDP communication
    CROW_ROUTE(ProjectApp, "/setup_connection/<string>/<int>").methods(crow::HTTPMethod::Put)
    ([](const crow::request &, crow::response &res, string ip, int port) {
        in_addr tmp;
        if (inet_pton(AF_INET, ip.c_str(), &tmp) != 1 || port <= 0 || port > 65535)
        {
            TextResponse(res, 400, "Invalid IP address or port");
            return;
        }
        {
            lock_guard<mutex> lock(UDPMutex);
            UDPAddress = ip;
            UDPPort = port;
        }
        TextResponse(res, 200, "Connection set to " + ip + ":" + to_string(port));
    });

    // Send a new contact to the Windows client
    CROW_ROUTE(ProjectApp, "/new_contact/").methods(crow::HTTPMethod::Post)
    ([](const crow::request &req, crow::response &res) {
        const char *firstname = req.url_params.get("FirstName");
        const char *lastname = req.url_params.get("LastName");
        const char *idnumber = req.url_params.get("IDNumber");

        if (!firstname || !lastname || !idnumber)
        {
            TextResponse(res, 400, "FirstName, LastName and IDNumber are required");
            return;
        }

        char *end = nullptr;
        unsigned long id = strtoul(idnumber, &end, 10);
        if (*idnumber == '\0' || *end != '\0')
        {
            TextResponse(res, 400, "IDNumber must be a whole number");
            return;
        }

        sockaddr_in clientAddr;
        if (!GetTarget(clientAddr))
        {
            TextResponse(res, 409, "Set up the connection first");
            return;
        }

        string fullname = string(firstname) + " " + string(lastname);

        AppPacket pkt;
        pkt.SetData(fullname.c_str(), (int)fullname.length(), (unsigned int)id);
        int txSize = 0;
        char *txBuf = pkt.SerializeData(txSize);

        int udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSocket < 0)
        {
            TextResponse(res, 500, "Could not create UDP socket");
            return;
        }
        int sent = sendto(udpSocket, txBuf, txSize, 0, (sockaddr *)&clientAddr, sizeof(clientAddr));
        close(udpSocket);

        if (sent != txSize)
        {
            TextResponse(res, 502, "Failed to send contact to client");
            return;
        }
        TextResponse(res, 200, "Contact sent: " + fullname + " ID: " + to_string(id));
    });

    // Ask the Windows client for the last contact it received
    CROW_ROUTE(ProjectApp, "/last_contact/").methods(crow::HTTPMethod::Get)
    ([](const crow::request &, crow::response &res) {
        sockaddr_in clientAddr;
        if (!GetTarget(clientAddr))
        {
            TextResponse(res, 409, "Set up the connection first");
            return;
        }

        AppPacket request;
        request.SetRequest();
        int txSize = 0;
        char *txBuf = request.SerializeData(txSize);

        // Ephemeral port: the client replies to whatever address sent the request
        int udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSocket < 0)
        {
            TextResponse(res, 500, "Could not create UDP socket");
            return;
        }

        // Don't hang the HTTP request forever if the client isn't running
        timeval tv{UDP_REPLY_TIMEOUT_SEC, 0};
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sendto(udpSocket, txBuf, txSize, 0, (sockaddr *)&clientAddr, sizeof(clientAddr));

        char recvBuf[512];
        sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        int bytes = recvfrom(udpSocket, recvBuf, sizeof(recvBuf), 0, (sockaddr *)&fromAddr, &fromLen);
        close(udpSocket);

        if (bytes <= 0)
        {
            TextResponse(res, 504, "No response from client (is it running?)");
            return;
        }

        AppPacket response(recvBuf, bytes);
        if (!response.IsValid() || response.GetHead().Flag != 1)
        {
            TextResponse(res, 502, "Malformed response from client");
            return;
        }

        TextResponse(res, 200, "Last Contact: " + response.GetName() +
                                   " ID: " + to_string(response.GetIDNumber()));
    });

    ProjectApp.port(HTTP_PORT).multithreaded().run();
    return 0;
}
