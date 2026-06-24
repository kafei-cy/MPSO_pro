#include <mpso/preproc/beaver_triple_generator.h>

#include <mpso/common/cuckoo_layout.h>
#include <mpso/preproc/preproc_common.h>

#include <stdexcept>
#include <string>

using mpso::block;
using mpso::PRNG;
using mpso::Socket;
using mpso::sysRandomSeed;
using mpso::u32;
using mpso::u64;
using mpso::ZeroBlock;

namespace {

template<typename T>
struct FieldOps;

template<>
struct FieldOps<block> {
    static block zero()
    {
        return ZeroBlock;
    }

    static block multiply(const block& lhs, const block& rhs)
    {
        return lhs.gf128Mul(rhs);
    }
};

template<>
struct FieldOps<u64> {
    static u64 zero()
    {
        return 0;
    }

    static u64 multiply(u64 lhs, u64 rhs)
    {
        block x128 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&lhs));
        block y128 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&rhs));
        constexpr u64 gf64_modulus = (1 << 4) | (1 << 3) | (1 << 1) | 1;
        const block modulus = oc::toBlock(gf64_modulus);

        const block product = _mm_clmulepi64_si128(x128, y128, 0x00);
        const block folded = product.clmulepi64_si128<0x01>(modulus);
        const block reduced_high = folded.clmulepi64_si128<0x01>(modulus);
        const block reduced = reduced_high ^ folded ^ product;
        return reduced.get<u64>()[0];
    }
};

template<typename T>
void writeTripleShare(
    const mpso::preproc::BeaverTripleSpec& spec,
    u32 party_index,
    const std::vector<T>& a,
    const std::vector<T>& b,
    const std::vector<T>& c)
{
    const auto path = mpso::beaverTripleFile(
        spec.parties,
        spec.elements,
        party_index,
        spec.subgroup_size,
        spec.suffix);
    auto out = mpso::preproc::openOutput(path);
    mpso::preproc::writeBinary(out, a, path);
    mpso::preproc::writeBinary(out, b, path);
    mpso::preproc::writeBinary(out, c, path);
}

template<typename T>
void readTripleShare(
    const mpso::preproc::BeaverTripleSpec& spec,
    u32 party_index,
    u32 num_bins,
    std::vector<T>& a,
    std::vector<T>& b,
    std::vector<T>& c)
{
    a.resize(num_bins);
    b.resize(num_bins);
    c.resize(num_bins);

    const auto path = mpso::beaverTripleFile(
        spec.parties,
        spec.elements,
        party_index,
        spec.subgroup_size,
        spec.suffix);
    auto in = mpso::preproc::openInput(path);
    mpso::preproc::readBinary(in, a, path);
    mpso::preproc::readBinary(in, b, path);
    mpso::preproc::readBinary(in, c, path);
}

template<typename T>
std::vector<std::vector<T>> makeShareRows(u32 num_rows, u32 num_bins)
{
    return std::vector<std::vector<T>>(num_rows, std::vector<T>(num_bins, FieldOps<T>::zero()));
}

template<typename T>
void xorInto(std::vector<T>& accumulator, const std::vector<T>& row)
{
    for (u32 i = 0; i < accumulator.size(); ++i) {
        accumulator[i] ^= row[i];
    }
}

template<typename T>
void sampleRandomShares(
    PRNG& prng,
    std::vector<std::vector<T>>& a,
    std::vector<std::vector<T>>& b,
    std::vector<std::vector<T>>& c)
{
    for (u32 party = 0; party < a.size(); ++party) {
        prng.get(a[party].data(), a[party].size());
        prng.get(b[party].data(), b[party].size());
    }

    for (u32 party = 0; party + 1 < c.size(); ++party) {
        prng.get(c[party].data(), c[party].size());
    }
}

template<typename T>
void completeFinalProductShare(
    std::vector<std::vector<T>>& a,
    std::vector<std::vector<T>>& b,
    std::vector<std::vector<T>>& c)
{
    const auto subgroup_size = static_cast<u32>(a.size());
    const auto num_bins = static_cast<u32>(a.front().size());
    std::vector<T> sum_a(num_bins, FieldOps<T>::zero());
    std::vector<T> sum_b(num_bins, FieldOps<T>::zero());
    std::vector<T> sum_c(num_bins, FieldOps<T>::zero());

    for (u32 party = 0; party < subgroup_size; ++party) {
        xorInto(sum_a, a[party]);
        xorInto(sum_b, b[party]);
    }

    for (u32 party = 0; party + 1 < subgroup_size; ++party) {
        xorInto(sum_c, c[party]);
    }

    for (u32 bin = 0; bin < num_bins; ++bin) {
        c[subgroup_size - 1][bin] = FieldOps<T>::multiply(sum_a[bin], sum_b[bin]) ^ sum_c[bin];
    }
}

template<typename T>
void reconstructOpenedMask(
    u32 party_index,
    u32 subgroup_size,
    const std::vector<T>& local_mask,
    std::vector<Socket>& channels,
    std::vector<T>& opened_mask)
{
    const auto num_bins = static_cast<u32>(local_mask.size());
    opened_mask.resize(num_bins, FieldOps<T>::zero());

    if (party_index == 0) {
        std::vector<std::vector<T>> masks(subgroup_size - 1, std::vector<T>(num_bins, FieldOps<T>::zero()));
        for (u32 party = 0; party < subgroup_size - 1; ++party) {
            coproto::sync_wait(channels[party].recv(masks[party]));
        }

        opened_mask = local_mask;
        for (const auto& mask : masks) {
            xorInto(opened_mask, mask);
        }

        for (u32 party = 0; party < subgroup_size - 1; ++party) {
            coproto::sync_wait(channels[party].send(opened_mask));
        }
    } else if (party_index < subgroup_size) {
        coproto::sync_wait(channels[0].send(local_mask));
        coproto::sync_wait(channels[0].recv(opened_mask));
    }
}

void validateBeaverSpec(
    u32 party_index,
    u32 input_size,
    u32 channel_count,
    const mpso::preproc::BeaverTripleSpec& spec)
{
    if (spec.parties < 2 || spec.subgroup_size < 2 || spec.subgroup_size > spec.parties) {
        throw std::invalid_argument("invalid Beaver triple party counts");
    }
    if (party_index >= spec.parties) {
        throw std::invalid_argument("Beaver triple party index is out of range");
    }
    if (spec.elements == 0) {
        throw std::invalid_argument("Beaver triple element count is invalid");
    }

    const auto expected_bins = mpso::numCuckooBins(spec.elements);
    if (input_size != expected_bins) {
        throw std::invalid_argument("Beaver triple input size does not match generated triples");
    }
    if (channel_count + 1 < spec.parties) {
        throw std::invalid_argument("Beaver triple channel set is too small");
    }
}

}

namespace mpso::preproc {

void generateMpsuBeaverTriples(u32 parties, u32 log_num_elements)
{
    for (u32 subgroup_size = 2; subgroup_size <= parties; ++subgroup_size) {
        generateBeaverTriples<block>(parties, subgroup_size, log_num_elements);
    }
}

template<typename T>
void generateBeaverTriples(
    u32 parties,
    u32 subgroup_size,
    u32 log_num_elements,
    const std::string& suffix)
{
    if (subgroup_size < 2 || subgroup_size > parties) {
        throw std::invalid_argument("invalid Beaver triple subgroup size");
    }

    const auto elements = elementCount(log_num_elements);
    const auto bins = mpso::numCuckooBins(elements);
    const BeaverTripleSpec spec{parties, elements, subgroup_size, suffix};

    PRNG prng(sysRandomSeed());
    auto a = makeShareRows<T>(subgroup_size, bins);
    auto b = makeShareRows<T>(subgroup_size, bins);
    auto c = makeShareRows<T>(subgroup_size, bins);

    sampleRandomShares(prng, a, b, c);
    completeFinalProductShare(a, b, c);

    for (u32 party = 0; party < subgroup_size; ++party) {
        writeTripleShare(spec, party, a[party], b[party], c[party]);
    }
}

template<typename T>
void multiplyWithBeaverTriples(
    u32 party_index,
    const std::vector<T>& input,
    std::vector<Socket>& channels,
    std::vector<T>& output,
    const BeaverTripleSpec& spec)
{
    const auto num_bins = static_cast<u32>(input.size());
    validateBeaverSpec(party_index, num_bins, channels.size(), spec);

    std::vector<T> a;
    std::vector<T> b;
    std::vector<T> c;
    readTripleShare(spec, party_index, num_bins, a, b, c);

    std::vector<T> local_mask(num_bins, FieldOps<T>::zero());
    for (u32 bin = 0; bin < num_bins; ++bin) {
        local_mask[bin] = input[bin] ^ a[bin];
    }

    std::vector<T> opened_mask;
    reconstructOpenedMask(party_index, spec.subgroup_size, local_mask, channels, opened_mask);

    output.resize(num_bins);
    for (u32 bin = 0; bin < num_bins; ++bin) {
        output[bin] = FieldOps<T>::multiply(opened_mask[bin], b[bin]) ^ c[bin];
    }
}

template void generateBeaverTriples<block>(
    u32 parties,
    u32 subgroup_size,
    u32 log_num_elements,
    const std::string& suffix);

template void generateBeaverTriples<u64>(
    u32 parties,
    u32 subgroup_size,
    u32 log_num_elements,
    const std::string& suffix);

template void multiplyWithBeaverTriples<block>(
    u32 party_index,
    const std::vector<block>& input,
    std::vector<Socket>& channels,
    std::vector<block>& output,
    const BeaverTripleSpec& spec);

template void multiplyWithBeaverTriples<u64>(
    u32 party_index,
    const std::vector<u64>& input,
    std::vector<Socket>& channels,
    std::vector<u64>& output,
    const BeaverTripleSpec& spec);

}
