#include <mpso/preproc/bool_triple_generator.h>

#include <mpso/common/cuckoo_layout.h>
#include <mpso/preproc/preproc_common.h>

#include <volePSI/GMW/Gmw.h>

#include <string>

using mpso::block;
using mpso::Socket;
using mpso::sysRandomSeed;
using mpso::u32;
using mpso::u64;

namespace {

constexpr u64 kGmwTripleBatchSize = 1ull << 24;

u32 gmwPartyIndex(mpso::preproc::PairRole role)
{
    return role == mpso::preproc::PairRole::Receiver ? 1 : 0;
}

u32 keyBitLength(u32 num_parties, u32 num_bins)
{
    const u64 interaction_count = (static_cast<u64>(num_parties) * (num_parties - 1)) / 2;
    return mpso::kSsp + oc::log2ceil(static_cast<u64>(num_bins) * interaction_count);
}

void writeTripleMaterial(const std::string& path, volePSI::Gmw& gmw)
{
    auto out = mpso::preproc::openOutput(path);
    mpso::preproc::writeBinary(out, gmw.mA.data(), gmw.mA.size(), path);
    mpso::preproc::writeBinary(out, gmw.mB.data(), gmw.mB.size(), path);
    mpso::preproc::writeBinary(out, gmw.mC.data(), gmw.mC.size(), path);
    mpso::preproc::writeBinary(out, gmw.mD.data(), gmw.mD.size(), path);
}

void generatePairTriples(const mpso::preproc::PartyPair& pair, Socket& channel)
{
    auto circuit = volePSI::isZeroCircuit(keyBitLength(pair.parties, pair.bins));

    volePSI::Gmw gmw;
    gmw.init(
        pair.bins,
        circuit,
        pair.threads,
        gmwPartyIndex(pair.role),
        sysRandomSeed());

    coproto::sync_wait(gmw.generateTriple(kGmwTripleBatchSize, pair.threads, channel));
    const auto path = mpso::gmwTripleFile(mpso::pairTag(pair.parties, pair.elements, pair.self, pair.other));
    writeTripleMaterial(path, gmw);
    coproto::sync_wait(channel.flush());
}

}

namespace mpso::preproc {

void generateMpsuBoolTriples(
    u32 party_index,
    u32 num_parties,
    u32 log_num_elements,
    u32 num_threads)
{
    checkParty(party_index, num_parties);
    const auto num_elements = elementCount(log_num_elements);
    const auto num_bins = mpso::numCuckooBins(num_elements);

    runWithChannels(mpso::connectParties(party_index, num_parties), [&](auto& channels) {
        const auto pairs = allPartyPairs(party_index, num_parties, num_elements, num_bins, num_threads);
        runPartyPairs(channels, pairs, generatePairTriples);
    });
}

}
