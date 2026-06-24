#include <mpso/common/network.h>

#include <mpso/common/parameters.h>

#include <coproto/Socket/AsioSocket.h>

#include <stdexcept>
#include <string>

namespace {

using mpso::Socket;
using mpso::u32;
using mpso::u64;

void checkPartyFitsPortLayout(u32 party)
{
    if (party >= mpso::kMaxLocalParties) {
        throw std::invalid_argument("party index exceeds local port layout");
    }
}

u32 pairPort(u32 party_a, u32 party_b)
{
    checkPartyFitsPortLayout(party_a);
    checkPartyFitsPortLayout(party_b);

    const u32 hi = party_a > party_b ? party_a : party_b;
    const u32 lo = party_a > party_b ? party_b : party_a;
    return mpso::kPortBase + hi * mpso::kPortStride + lo;
}

bool actsAsServer(u32 self, u32 party)
{
    return self > party;
}

std::string pairEndpoint(u32 party_a, u32 party_b)
{
    return "localhost:" + std::to_string(pairPort(party_a, party_b));
}

}

namespace mpso {

Socket connectParty(u32 self, u32 party)
{
    if (self == party) {
        throw std::invalid_argument("cannot connect a party to itself");
    }

    return coproto::asioConnect(pairEndpoint(self, party), actsAsServer(self, party));
}

std::vector<Socket> connectParties(u32 self, u32 num_parties)
{
    if (num_parties < 2) {
        throw std::invalid_argument("at least two parties are required");
    }
    if (num_parties > kMaxLocalParties) {
        throw std::invalid_argument("number of parties exceeds local port layout");
    }
    if (self >= num_parties) {
        throw std::invalid_argument("local party index is out of range");
    }

    std::vector<Socket> channels;
    channels.reserve(num_parties - 1);

    for (u32 party = 0; party < num_parties; ++party) {
        if (party != self) {
            channels.emplace_back(connectParty(self, party));
        }
    }

    return channels;
}

u64 commBytes(std::vector<Socket>& channels)
{
    u64 bytes = 0;
    for (auto& channel : channels) {
        bytes += channel.bytesReceived() + channel.bytesSent();
    }
    return bytes;
}

double commMiB(std::vector<Socket>& channels)
{
    return static_cast<double>(commBytes(channels)) / 1024 / 1024;
}

void closeChannels(std::vector<Socket>& channels)
{
    for (auto& channel : channels) {
        coproto::sync_wait(channel.flush());
        coproto::sync_wait(channel.close());
    }
}

}
