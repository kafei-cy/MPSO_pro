#pragma once

#include <mpso/common/runtime.h>

#include <stdexcept>
#include <vector>

namespace mpso {

inline u32 channelId(u32 self, u32 party)
{
    if (self == party) {
        throw std::invalid_argument("channel id requires two distinct parties");
    }

    return party < self ? party : party - 1;
}

inline Socket& channelAt(std::vector<Socket>& channels, u32 self, u32 party)
{
    const auto id = channelId(self, party);
    if (id >= channels.size()) {
        throw std::out_of_range("channel id is out of range");
    }

    return channels[id];
}

Socket connectParty(u32 self, u32 party);

std::vector<Socket> connectParties(u32 self, u32 num_parties);

u64 commBytes(std::vector<Socket>& channels);

double commMiB(std::vector<Socket>& channels);

void closeChannels(std::vector<Socket>& channels);

}
