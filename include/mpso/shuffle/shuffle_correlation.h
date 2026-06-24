#pragma once

#include <mpso/shuffle/shuffle_types.h>

#include <string>
#include <vector>

namespace mpso::shuffle {

template<typename T, ShareMode Mode>
class Correlation {
public:
    Correlation(u32 num_parties, u32 num_elements);

    bool exists(const std::string& name) const;

    void generate();

    void generateWithPermutationsFrom(const std::string& source_name);

    void write(const std::string& name) const;

    void clear();

private:
    u32 num_parties_;
    u32 num_elements_;
    std::vector<std::vector<u32>> permutations_;
    std::vector<std::vector<T>> a_;
    std::vector<std::vector<T>> a_prime_;
    std::vector<std::vector<T>> b_;
    std::vector<T> delta_;
};

extern template class Correlation<block, ShareMode::Xor>;
extern template class Correlation<u64, ShareMode::Xor>;
extern template class Correlation<u64, ShareMode::Add>;

}
