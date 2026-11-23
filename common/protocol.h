// ============================================================================
// Application-layer protocol shared by the Crow server (Linux) and the
// Windows UDP client. Both programs include this one header, so the packet
// format and its validation rules can't drift apart.
//
// Wire layout
//   Request (Flag = 0): [Flag][Size = 0]                          2 bytes
//   Data    (Flag = 1): [Flag][Size][Name: Size bytes][IDNumber: 4 bytes]
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace protocol
{

enum Flag : unsigned char
{
    FLAG_REQUEST = 0, // ask for the last PersonData
    FLAG_DATA = 1     // carries a PersonData body
};

struct Header
{
    unsigned char Flag; // FLAG_REQUEST or FLAG_DATA
    unsigned char Size; // number of bytes of Name (0 for a request)
};

constexpr std::size_t HEADER_SIZE = sizeof(Header);
constexpr std::size_t ID_SIZE = sizeof(std::uint32_t);
constexpr std::size_t MAX_NAME_BYTES = 255; // Size is a single byte

// Owns its data in standard containers, so it is safe to copy and needs no
// manual memory management.
class AppPacket
{
    Header head{FLAG_REQUEST, 0};
    std::string name;          // not null-terminated on the wire
    std::uint32_t idNumber = 0;
    std::vector<char> txBuffer;
    bool valid = true;

public:
    AppPacket() = default;

    // Deserialise a received datagram. Anything that does not match the
    // protocol exactly is marked invalid instead of being trusted.
    AppPacket(const char *src, int len)
    {
        if (src == nullptr || len < static_cast<int>(HEADER_SIZE))
        {
            valid = false;
            return;
        }
        std::memcpy(&head, src, HEADER_SIZE);
        const auto length = static_cast<std::size_t>(len);

        if (head.Flag == FLAG_REQUEST)
        {
            // A request is exactly the 2-byte header with Size 0
            valid = (head.Size == 0 && length == HEADER_SIZE);
            return;
        }
        if (head.Flag != FLAG_DATA)
        {
            valid = false; // unknown operation
            return;
        }

        if (length != HEADER_SIZE + head.Size + ID_SIZE)
        {
            valid = false; // truncated or padded body
            return;
        }
        name.assign(src + HEADER_SIZE, head.Size);
        std::memcpy(&idNumber, src + HEADER_SIZE + head.Size, ID_SIZE);
    }

    // Returns false (and leaves the packet unchanged) if the name does not
    // fit in the one-byte Size field.
    bool SetData(const std::string &newName, std::uint32_t id)
    {
        if (newName.size() > MAX_NAME_BYTES)
            return false;
        head.Flag = FLAG_DATA;
        head.Size = static_cast<unsigned char>(newName.size());
        name = newName;
        idNumber = id;
        valid = true;
        return true;
    }

    void SetRequest()
    {
        head.Flag = FLAG_REQUEST;
        head.Size = 0;
        name.clear();
        idNumber = 0;
        valid = true;
    }

    // The returned pointer stays valid until the packet is changed.
    const char *SerializeData(int &totalSize)
    {
        txBuffer.assign(reinterpret_cast<const char *>(&head),
                        reinterpret_cast<const char *>(&head) + HEADER_SIZE);
        if (head.Flag == FLAG_DATA)
        {
            txBuffer.insert(txBuffer.end(), name.begin(), name.end());
            const auto *id = reinterpret_cast<const char *>(&idNumber);
            txBuffer.insert(txBuffer.end(), id, id + ID_SIZE);
        }
        totalSize = static_cast<int>(txBuffer.size());
        return txBuffer.data();
    }

    bool IsValid() const { return valid; }
    Header GetHead() const { return head; }
    std::string GetName() const { return name; }
    std::uint32_t GetIDNumber() const { return idNumber; }
};

} // namespace protocol
