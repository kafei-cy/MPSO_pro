#pragma once

#include <mpso/common/cuckoo_layout.h>
#include <mpso/common/network.h>

#include <cryptoTools/Common/CuckooIndex.h>
#include <volePSI/SimpleIndex.h>

#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mpso::proto {

inline u32 checkedU32(std::size_t value, const std::string& label)
{
    if (value > std::numeric_limits<u32>::max()) {
        throw std::overflow_error(label + " exceeds u32 range");
    }
    return static_cast<u32>(value);
}

inline void joinThreads(
    std::vector<std::thread>& threads,
    const std::vector<std::exception_ptr>& errors)
{
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    for (const auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

inline std::vector<std::vector<block>> buildSimpleHashTable(
    std::vector<block>& set,
    u32 num_bins,
    u32 num_elements,
    block cuckoo_seed)
{
    volePSI::SimpleIndex simple_index;
    simple_index.init(num_bins, num_elements, mpso::kSsp, mpso::kCuckooHashes);
    simple_index.insertItems(set, cuckoo_seed);

    std::vector<std::vector<block>> simple_hash_table(num_bins);
    for (u32 bin_index = 0; bin_index < num_bins; ++bin_index) {
        const auto bin = simple_index.mBins[bin_index];
        const auto bin_size = simple_index.mBinSizes[bin_index];
        simple_hash_table[bin_index].resize(bin_size);
        for (u32 item_index = 0; item_index < bin_size; ++item_index) {
            const auto hash_index = bin[item_index].hashIdx();
            const auto set_index = bin[item_index].idx();
            const u64 item_value = set[set_index].mData[0];
            simple_hash_table[bin_index][item_index] = block(item_value, hash_index);
        }
    }

    return simple_hash_table;
}

inline std::vector<block> buildCuckooHashTable(
    std::vector<block>& set,
    u32 num_bins,
    u32 num_elements,
    block cuckoo_seed)
{
    oc::CuckooIndex cuckoo;
    cuckoo.init(num_elements, mpso::kSsp, mpso::kCuckooStashSize, mpso::kCuckooHashes);
    cuckoo.insert(set, cuckoo_seed);

    std::vector<block> cuckoo_hash_table(num_bins);
    for (u32 bin_index = 0; bin_index < num_bins; ++bin_index) {
        const auto bin = cuckoo.mBins[bin_index];
        if (!bin.isEmpty()) {
            const auto hash_index = bin.hashIdx();
            const auto set_index = bin.idx();
            const u64 item_value = set[set_index].mData[0];
            cuckoo_hash_table[bin_index] = block(item_value, hash_index);
        }
    }

    return cuckoo_hash_table;
}

template<typename T>
void receiveAndXorShares(
    std::vector<Socket>& channels,
    std::vector<T>& share_sum,
    u32 num_parties)
{
    std::vector<std::vector<T>> received_shares(num_parties - 1);
    std::vector<std::thread> receive_threads(num_parties - 1);
    std::vector<std::exception_ptr> receive_errors(num_parties - 1);

    for (u32 party_offset = 0; party_offset < num_parties - 1; ++party_offset) {
        receive_threads[party_offset] = std::thread([&, party_offset]() {
            try {
                received_shares[party_offset].resize(share_sum.size());
                coproto::sync_wait(channels[party_offset].recv(received_shares[party_offset]));
            } catch (...) {
                receive_errors[party_offset] = std::current_exception();
            }
        });
    }

    joinThreads(receive_threads, receive_errors);

    for (const auto& received_share : received_shares) {
        for (u32 value_index = 0; value_index < share_sum.size(); ++value_index) {
            share_sum[value_index] ^= received_share[value_index];
        }
    }
}

}
