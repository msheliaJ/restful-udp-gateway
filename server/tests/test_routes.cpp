// ============================================================================
// Route tests for the Crow server (server/main.cpp).
//
// main.cpp is included with its main() renamed, and the routes are driven
// in-process with crow::App::handle_full, so no HTTP port is opened. A local
// UDP socket stands in for the Windows client.
// ============================================================================
#include <gtest/gtest.h>

#define main gateway_server_main
#include "../main.cpp"
#undef main

#include <string>
#include <thread>

namespace
{

class RoutesTest : public ::testing::Test
{
protected:
    crow::SimpleApp app;

    void SetUp() override
    {
        RegisterRoutes(app);
        app.validate();
    }

    crow::response Call(crow::HTTPMethod method, const std::string &path,
                        const std::string &query = "")
    {
        crow::request req;
        req.method = method;
        req.url = path;
        req.raw_url = query.empty() ? path : path + "?" + query;
        req.url_params = crow::query_string(req.raw_url);
        crow::response res;
        app.handle_full(req, res);
        return res;
    }
};

// A UDP socket on 127.0.0.1 with an OS-chosen port: the fake client
class FakeClient
{
public:
    int fd = -1;
    int port = 0;

    FakeClient()
    {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        socklen_t len = sizeof(addr);
        getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len);
        port = ntohs(addr.sin_port);
        timeval tv{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    ~FakeClient() { close(fd); }

    // Waits for one datagram; optionally replies with `reply`
    std::string ReceiveAndReply(const std::string &reply = "")
    {
        char buf[512];
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        auto n = recvfrom(fd, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
        if (n <= 0)
            return "";
        if (!reply.empty())
            sendto(fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr *>(&from), fromLen);
        return std::string(buf, static_cast<std::size_t>(n));
    }
};

std::string DataPacket(const std::string &name, std::uint32_t id)
{
    protocol::AppPacket pkt;
    pkt.SetData(name, id);
    int size = 0;
    const char *buf = pkt.SerializeData(size);
    return std::string(buf, static_cast<std::size_t>(size));
}

} // namespace

// ---------------------------------------------------------------------------
// GET /
// ---------------------------------------------------------------------------
TEST_F(RoutesTest, RootServesTheBrowserUi)
{
    auto res = Call(crow::HTTPMethod::Get, "/");
    EXPECT_EQ(res.code, 200);
    EXPECT_NE(res.body.find("RESTful Cloud Service"), std::string::npos);
}

// ---------------------------------------------------------------------------
// PUT /setup_connection/<ip>/<port>
// ---------------------------------------------------------------------------
TEST_F(RoutesTest, SetupConnectionAcceptsValidTarget)
{
    auto res = Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/22150");
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "Connection set to 127.0.0.1:22150");
}

TEST_F(RoutesTest, SetupConnectionRejectsBadIp)
{
    EXPECT_EQ(Call(crow::HTTPMethod::Put, "/setup_connection/999.1.1.1/22150").code, 400);
}

TEST_F(RoutesTest, SetupConnectionRejectsBadPort)
{
    EXPECT_EQ(Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/70000").code, 400);
    EXPECT_EQ(Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/0").code, 400);
}

// ---------------------------------------------------------------------------
// POST /new_contact/
// ---------------------------------------------------------------------------
TEST_F(RoutesTest, NewContactRequiresAllParameters)
{
    auto res = Call(crow::HTTPMethod::Post, "/new_contact/", "FirstName=John&LastName=Doe");
    EXPECT_EQ(res.code, 400);
}

TEST_F(RoutesTest, NewContactRejectsOutOfRangeId)
{
    Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/22150");
    EXPECT_EQ(Call(crow::HTTPMethod::Post, "/new_contact/", "FirstName=John&LastName=Doe&IDNumber=-1").code, 400);
    EXPECT_EQ(Call(crow::HTTPMethod::Post, "/new_contact/", "FirstName=John&LastName=Doe&IDNumber=4294967296").code, 400);
}

TEST_F(RoutesTest, NewContactRejectsNameOver255Bytes)
{
    Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/22150");
    std::string longName(300, 'A');
    auto res = Call(crow::HTTPMethod::Post, "/new_contact/", "FirstName=" + longName + "&LastName=Doe&IDNumber=1");
    EXPECT_EQ(res.code, 400);
}

TEST_F(RoutesTest, NewContactBeforeSetupIsConflict)
{
    auto res = Call(crow::HTTPMethod::Post, "/new_contact/", "FirstName=John&LastName=Doe&IDNumber=1");
    EXPECT_EQ(res.code, 409);
}

TEST_F(RoutesTest, NewContactSendsTheSamePacketItConfirms)
{
    FakeClient client;
    Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/" + std::to_string(client.port));

    auto res = Call(crow::HTTPMethod::Post, "/new_contact/", "FirstName=John&LastName=Doe&IDNumber=12345");
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "Contact sent: John Doe ID: 12345");

    std::string wire = client.ReceiveAndReply();
    protocol::AppPacket received(wire.data(), static_cast<int>(wire.size()));
    ASSERT_TRUE(received.IsValid());
    EXPECT_EQ(received.GetName(), "John Doe");
    EXPECT_EQ(received.GetIDNumber(), 12345u);
}

// ---------------------------------------------------------------------------
// GET /last_contact/
// ---------------------------------------------------------------------------
TEST_F(RoutesTest, LastContactBeforeSetupIsConflict)
{
    EXPECT_EQ(Call(crow::HTTPMethod::Get, "/last_contact/").code, 409);
}

TEST_F(RoutesTest, LastContactReturnsClientReply)
{
    FakeClient client;
    Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/" + std::to_string(client.port));

    std::string request;
    std::thread fake([&] { request = client.ReceiveAndReply(DataPacket("Ada Lovelace", 1815)); });
    auto res = Call(crow::HTTPMethod::Get, "/last_contact/");
    fake.join();

    EXPECT_EQ(request, std::string("\0\0", 2)); // a 2-byte request packet
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "Last Contact: Ada Lovelace ID: 1815");
}

TEST_F(RoutesTest, LastContactRejectsMalformedReply)
{
    FakeClient client;
    Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/" + std::to_string(client.port));

    std::thread fake([&] { client.ReceiveAndReply(std::string("\x07\x00", 2)); });
    auto res = Call(crow::HTTPMethod::Get, "/last_contact/");
    fake.join();

    EXPECT_EQ(res.code, 502);
}

TEST_F(RoutesTest, LastContactTimesOutWhenClientIsSilent)
{
    FakeClient client; // receives the request but never replies
    Call(crow::HTTPMethod::Put, "/setup_connection/127.0.0.1/" + std::to_string(client.port));

    auto res = Call(crow::HTTPMethod::Get, "/last_contact/");
    EXPECT_EQ(res.code, 504);
}
