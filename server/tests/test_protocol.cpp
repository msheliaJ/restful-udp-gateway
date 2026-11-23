// ============================================================================
// Unit tests for the application-layer protocol (AppPacket) in main.cpp.
//
// The baseline server keeps everything in main.cpp, so this file includes it
// directly and renames its main() to keep it out of the way. This lets the
// tests run against the untouched baseline code.
// ============================================================================
#include <gtest/gtest.h>

#define main gateway_server_main
#include "../main.cpp"
#undef main

#include <climits>
#include <cstring>
#include <string>
#include <vector>

namespace
{
std::vector<char> Serialize(AppPacket &pkt)
{
    int size = 0;
    char *buf = pkt.SerializeData(size);
    return std::vector<char>(buf, buf + size);
}
} // namespace

// ---------------------------------------------------------------------------
// Happy path: normal serialisation and deserialisation
// ---------------------------------------------------------------------------
TEST(AppPacket, DataPacketHasHeaderNameAndId)
{
    AppPacket pkt;
    pkt.SetData("John Doe", 8, 12345);
    auto wire = Serialize(pkt);

    ASSERT_EQ(wire.size(), 2u + 8u + 4u);
    EXPECT_EQ(wire[0], 1); // Flag = data
    EXPECT_EQ(wire[1], 8); // Size = name length
    EXPECT_EQ(std::string(wire.data() + 2, 8), "John Doe");
}

TEST(AppPacket, RequestPacketIsHeaderOnly)
{
    AppPacket pkt;
    pkt.SetRequest();
    auto wire = Serialize(pkt);

    ASSERT_EQ(wire.size(), 2u);
    EXPECT_EQ(wire[0], 0);
    EXPECT_EQ(wire[1], 0);
}

TEST(AppPacket, RoundTripPreservesNameAndId)
{
    AppPacket out;
    out.SetData("Ada Lovelace", 12, 1815);
    auto wire = Serialize(out);

    AppPacket in(wire.data(), static_cast<int>(wire.size()));
    ASSERT_TRUE(in.IsValid());
    EXPECT_EQ(in.GetHead().Flag, 1);
    EXPECT_EQ(in.GetName(), "Ada Lovelace");
    EXPECT_EQ(in.GetIDNumber(), 1815u);
}

TEST(AppPacket, SetDataReplacesPreviousName)
{
    AppPacket pkt;
    pkt.SetData("First", 5, 1);
    pkt.SetData("Second", 6, 2);
    EXPECT_EQ(pkt.GetName(), "Second");
    EXPECT_EQ(pkt.GetIDNumber(), 2u);
}

TEST(AppPacket, SetRequestClearsBody)
{
    AppPacket pkt;
    pkt.SetData("John", 4, 7);
    pkt.SetRequest();
    EXPECT_EQ(pkt.GetHead().Flag, 0);
    EXPECT_EQ(pkt.GetName(), "");
}

TEST(AppPacket, LongNameIsCappedAt255Bytes)
{
    std::string longName(300, 'x');
    AppPacket pkt;
    pkt.SetData(longName.c_str(), static_cast<int>(longName.size()), 1);
    EXPECT_EQ(pkt.GetHead().Size, 255);
    EXPECT_EQ(pkt.GetName().size(), 255u);
}

// ---------------------------------------------------------------------------
// Sad path: malformed datagrams from the network
// ---------------------------------------------------------------------------
TEST(AppPacket, EmptyDatagramIsRejected)
{
    AppPacket in("", 0);
    EXPECT_FALSE(in.IsValid());
}

TEST(AppPacket, OneByteDatagramIsRejected)
{
    const char buf[] = {1};
    AppPacket in(buf, 1);
    EXPECT_FALSE(in.IsValid());
}

TEST(AppPacket, TruncatedBodyIsRejected)
{
    // Header claims an 8-byte name but only 3 bytes follow
    const char buf[] = {1, 8, 'J', 'o', 'h'};
    AppPacket in(buf, sizeof(buf));
    EXPECT_FALSE(in.IsValid());
}

TEST(AppPacket, UnknownFlagIsRejected)
{
    // The protocol only defines Flag 0 (request) and Flag 1 (data).
    // Any other value is not a valid packet.
    const char buf[] = {7, 0};
    AppPacket in(buf, sizeof(buf));
    EXPECT_FALSE(in.IsValid());
}

TEST(AppPacket, RequestWithNonZeroSizeIsRejected)
{
    // A request (Flag 0) must carry Size 0 and no body.
    const char buf[] = {0, 5};
    AppPacket in(buf, sizeof(buf));
    EXPECT_FALSE(in.IsValid());
}
