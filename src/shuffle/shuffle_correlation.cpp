#include <mpso/shuffle/shuffle_correlation.h>

#include <mpso/common/permutation.h>
#include <mpso/shuffle/shuffle_common.h>

#include <fstream>
#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>

namespace mpso::shuffle {
namespace {

template<typename T>
void clearRows(std::vector<std::vector<T>>& rows)
{
    std::vector<std::vector<T>>().swap(rows);
}

template<typename T>
void clearVector(std::vector<T>& values)
{
    std::vector<T>().swap(values);
}

template<typename T>
void randomizeRows(PRNG& prng, std::vector<std::vector<T>>& rows)
{
    for (auto& row : rows) {
        prng.get<T>(row.data(), row.size());
    }
}

u32 drawBelow(PRNG& prng, u32 bound)
{
    if (bound == 0) {
        throw std::invalid_argument("random bound must be nonzero");
    }

    constexpr u64 range = u64{1} << std::numeric_limits<u32>::digits;
    const u64 limit = range - range % bound;

    u32 value = 0;
    do {
        value = prng.get<u32>();
    } while (static_cast<u64>(value) >= limit);

    return value % bound;
}

std::vector<u32> samplePermutation(u32 num_elements, PRNG& prng)
{
    std::vector<u32> permutation(num_elements);
    std::iota(permutation.begin(), permutation.end(), u32{0});

    for (u32 i = num_elements; i > 1; --i) {
        const auto j = drawBelow(prng, i);
        std::swap(permutation[i - 1], permutation[j]);
    }

    return permutation;
}

std::vector<std::vector<u32>> samplePermutations(u32 num_parties, u32 num_elements, PRNG& prng)
{
    std::vector<std::vector<u32>> permutations(num_parties);

    for (auto& permutation : permutations) {
        permutation = samplePermutation(num_elements, prng);
    }

    return permutations;
}

void validatePermutations(const std::vector<std::vector<u32>>& permutations, u32 num_elements)
{
    for (const auto& permutation : permutations) {
        validatePermutation(permutation, num_elements);
    }
}

template<typename T>
void resizeMasks(
    u32 num_parties,
    u32 num_elements,
    std::vector<std::vector<T>>& a,
    std::vector<std::vector<T>>& a_prime,
    std::vector<std::vector<T>>& b)
{
    a.assign(num_parties - 1, std::vector<T>(num_elements));
    a_prime.assign(num_parties - 1, std::vector<T>(num_elements));
    b.assign(num_parties - 1, std::vector<T>(num_elements));
}

template<typename T, ShareMode Mode>
void computeDelta(
    const std::vector<std::vector<u32>>& permutations,
    const std::vector<std::vector<T>>& a,
    const std::vector<std::vector<T>>& a_prime,
    const std::vector<std::vector<T>>& b,
    std::vector<T>& delta)
{
    const auto num_parties = static_cast<u32>(permutations.size());
    const auto num_elements = static_cast<u32>(permutations.front().size());

    delta.assign(num_elements, identity<T>());
    for (u32 party = 0; party < num_parties - 1; ++party) {
        for (u32 i = 0; i < num_elements; ++i) {
            add<T, Mode>(delta[i], a[party][i]);
        }
    }

    for (u32 party = 0; party < num_parties - 1; ++party) {
        mpso::permute(permutations[party], delta, PermuteSafety::Trust);
        for (u32 i = 0; i < num_elements; ++i) {
            add<T, Mode>(delta[i], a_prime[party][i]);
        }
    }

    mpso::permute(permutations[num_parties - 1], delta, PermuteSafety::Trust);
    for (u32 party = 0; party < num_parties - 1; ++party) {
        for (u32 i = 0; i < num_elements; ++i) {
            sub<T, Mode>(delta[i], b[party][i]);
        }
    }
}

template<typename T>
void writeFiles(
    const std::string& name,
    u32 num_parties,
    u32 num_elements,
    const std::vector<std::vector<u32>>& permutations,
    const std::vector<std::vector<T>>& a,
    const std::vector<std::vector<T>>& a_prime,
    const std::vector<std::vector<T>>& b,
    const std::vector<T>& delta)
{
    std::vector<std::ofstream> outputs(num_parties);
    std::vector<std::string> paths(num_parties);

    for (u32 party = 0; party < num_parties; ++party) {
        paths[party] = filePath(name, num_parties, num_elements, party);
        outputs[party] = openOutput(paths[party]);
    }

    for (u32 party = 0; party < num_parties; ++party) {
        writeBinary(outputs[party], permutations[party], paths[party]);
    }
    for (u32 party = 0; party < num_parties - 1; ++party) {
        writeBinary(outputs[party + 1], a[party], paths[party + 1]);
    }
    for (u32 party = 0; party < num_parties - 1; ++party) {
        writeBinary(outputs[party], a_prime[party], paths[party]);
        writeBinary(outputs[party], b[party], paths[party]);
    }
    writeBinary(outputs[num_parties - 1], delta, paths[num_parties - 1]);
}

void readPermutation(
    const std::string& name,
    u32 num_parties,
    u32 num_elements,
    u32 party_index,
    std::vector<u32>& permutation)
{
    const auto path = filePath(name, num_parties, num_elements, party_index);
    auto in = openInput(path);

    permutation.resize(num_elements);
    readBinary(in, permutation, path);
    validatePermutation(permutation, num_elements);
}

void readPermutations(
    const std::string& name,
    u32 num_parties,
    u32 num_elements,
    std::vector<std::vector<u32>>& permutations)
{
    permutations.resize(num_parties);
    for (u32 party = 0; party < num_parties; ++party) {
        readPermutation(name, num_parties, num_elements, party, permutations[party]);
    }
}

} // namespace

template<typename T, ShareMode Mode>
Correlation<T, Mode>::Correlation(u32 num_parties, u32 num_elements)
    : num_parties_(num_parties),
      num_elements_(num_elements)
{
    if (num_parties_ < 2) {
        throw std::invalid_argument("shuffle correlation requires at least two parties");
    }
    if (num_elements_ == 0) {
        throw std::invalid_argument("shuffle correlation requires at least one element");
    }
}

template<typename T, ShareMode Mode>
bool Correlation<T, Mode>::exists(const std::string& name) const
{
    for (u32 party = 0; party < num_parties_; ++party) {
        if (!fileExists(filePath(name, num_parties_, num_elements_, party))) {
            return false;
        }
    }
    return true;
}

template<typename T, ShareMode Mode>
void Correlation<T, Mode>::generate()
{
    if constexpr (Mode == ShareMode::Add) {
        throw std::logic_error("additive shuffle correlations must reuse existing permutations");
    }

    PRNG prng(sysRandomSeed());
    permutations_ = samplePermutations(num_parties_, num_elements_, prng);
    validatePermutations(permutations_, num_elements_);
    resizeMasks(num_parties_, num_elements_, a_, a_prime_, b_);
    randomizeRows(prng, a_);
    randomizeRows(prng, a_prime_);
    randomizeRows(prng, b_);
    computeDelta<T, Mode>(permutations_, a_, a_prime_, b_, delta_);
}

template<typename T, ShareMode Mode>
void Correlation<T, Mode>::generateWithPermutationsFrom(const std::string& source_name)
{
    readPermutations(source_name, num_parties_, num_elements_, permutations_);

    PRNG prng(sysRandomSeed());
    resizeMasks(num_parties_, num_elements_, a_, a_prime_, b_);
    randomizeRows(prng, a_);
    randomizeRows(prng, a_prime_);
    randomizeRows(prng, b_);
    computeDelta<T, Mode>(permutations_, a_, a_prime_, b_, delta_);
}

template<typename T, ShareMode Mode>
void Correlation<T, Mode>::write(const std::string& name) const
{
    writeFiles(name, num_parties_, num_elements_, permutations_, a_, a_prime_, b_, delta_);
}

template<typename T, ShareMode Mode>
void Correlation<T, Mode>::clear()
{
    clearRows(permutations_);
    clearRows(a_);
    clearRows(a_prime_);
    clearRows(b_);
    clearVector(delta_);
}

template class Correlation<block, ShareMode::Xor>;
template class Correlation<u64, ShareMode::Xor>;
template class Correlation<u64, ShareMode::Add>;

}
