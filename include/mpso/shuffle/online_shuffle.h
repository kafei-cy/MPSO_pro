#pragma once

#include <mpso/shuffle/shuffle_types.h>

#include <string>
#include <vector>

namespace mpso::shuffle {

template<typename T, ShareMode Mode>
class Online {
public:
    Online(u32 party_index, u32 num_parties, u32 num_elements);

    void load(const std::string& name);

    Proto run(std::vector<Socket>& channels, std::vector<T>& data);

    u32 partyIndex() const { return party_index_; }

    u32 numParties() const { return num_parties_; }

    u32 numElements() const { return num_elements_; }

private:
    bool isFirstParty() const { return party_index_ == 0; }

    bool isLastParty() const { return party_index_ == num_parties_ - 1; }

    u32 prevChannel() const { return party_index_ - 1; }

    u32 nextChannel() const { return party_index_; }

    u32 party_index_;
    u32 num_parties_;
    u32 num_elements_;
    std::vector<u32> permutation_;
    std::vector<T> a_;
    std::vector<T> a_prime_;
    std::vector<T> b_;
    std::vector<T> delta_;
};

extern template class Online<block, ShareMode::Xor>;
extern template class Online<u64, ShareMode::Xor>;
extern template class Online<u64, ShareMode::Add>;

}
