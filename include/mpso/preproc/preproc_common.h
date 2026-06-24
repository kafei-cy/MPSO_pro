#pragma once

#include <mpso/common/binary_io.h>
#include <mpso/common/network.h>
#include <mpso/common/offline_paths.h>

#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>

namespace mpso::preproc {

enum class PairRole {
    Sender,
    Receiver,
};

struct PartyPair {
    u32 self = 0;
    u32 other = 0;
    u32 parties = 0;
    u32 elements = 0;
    u32 bins = 0;
    u32 threads = 1;
    PairRole role = PairRole::Sender;
};

inline u32 elementCount(u32 log_elements)
{
    if (log_elements >= 32) {
        throw std::invalid_argument("element count exceeds u32 range");
    }
    return u32{1} << log_elements;
}

inline void checkParty(u32 party, u32 parties)
{
    if (parties < 2) {
        throw std::invalid_argument("at least two parties are required");
    }
    if (party >= parties) {
        throw std::invalid_argument("party index is out of range");
    }
}

using mpso::openInput;
using mpso::openOutput;
using mpso::pairTag;
using mpso::readBinary;
using mpso::writeBinary;

template<typename Fn>
void runWithChannels(std::vector<Socket> channels, Fn&& fn)
{
    bool closed = false;

    try {
        fn(channels);
        closeChannels(channels);
        closed = true;
    } catch (...) {
        if (!closed) {
            try {
                closeChannels(channels);
            } catch (...) {
            }
        }
        throw;
    }
}

template<typename Fn>
void runPartyPairs(std::vector<Socket>& channels, const std::vector<PartyPair>& pairs, Fn&& fn)
{
    std::vector<std::thread> workers;
    std::vector<std::exception_ptr> errors(channels.size());
    workers.reserve(pairs.size());

    for (const auto& pair : pairs) {
        const auto channel_index = channelId(pair.self, pair.other);
        workers.emplace_back([&, channel_index, pair]() {
            try {
                fn(pair, channels[channel_index]);
            } catch (...) {
                errors[channel_index] = std::current_exception();
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    for (const auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

inline std::vector<PartyPair> allPartyPairs(
    u32 self,
    u32 parties,
    u32 elements,
    u32 bins,
    u32 threads)
{
    std::vector<PartyPair> pairs;
    pairs.reserve(parties - 1);

    for (u32 other = 0; other < parties; ++other) {
        if (other == self) {
            continue;
        }

        pairs.push_back(PartyPair{
            self,
            other,
            parties,
            elements,
            bins,
            threads,
            other < self ? PairRole::Receiver : PairRole::Sender,
        });
    }

    return pairs;
}

}
