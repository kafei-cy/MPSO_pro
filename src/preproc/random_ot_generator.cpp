#include <mpso/preproc/random_ot_generator.h>

#include <mpso/common/cuckoo_layout.h>
#include <mpso/proto/random_ot.h>
#include <mpso/preproc/preproc_common.h>

#include <string>

using mpso::BitVector;
using mpso::PRNG;
using mpso::Socket;
using mpso::sysRandomSeed;
using mpso::u32;
using mpso::u64;

namespace {

void writeSenderMaterial(const std::string& path, const mpso::proto::OtSenderMessages& messages)
{
    auto out = mpso::preproc::openOutput(path);
    mpso::preproc::writeBinary(out, messages.data(), messages.size(), path);
}

void writeReceiverMaterial(
    const std::string& path,
    const mpso::proto::OtReceiverMessages& messages,
    const BitVector& choices)
{
    auto out = mpso::preproc::openOutput(path);
    mpso::preproc::writeBinary(out, messages.data(), messages.size(), path);
    mpso::preproc::writeBinary(out, choices.data(), choices.sizeBytes(), path);
}

void generateSenderMaterial(const mpso::preproc::PartyPair& pair, Socket& channel, PRNG& prng)
{
    mpso::proto::OtSenderMessages messages;
    mpso::proto::sendRandomOt(pair.bins, channel, prng, messages, pair.threads);
    const auto path = mpso::randomOtFile(mpso::pairTag(pair.parties, pair.elements, pair.self, pair.other));
    writeSenderMaterial(path, messages);
}

void generateReceiverMaterial(const mpso::preproc::PartyPair& pair, Socket& channel, PRNG& prng)
{
    BitVector choices(pair.bins);
    choices.randomize(prng);

    mpso::proto::OtReceiverMessages messages;
    mpso::proto::receiveRandomOt(choices, channel, prng, messages, pair.threads);
    const auto path = mpso::randomOtFile(mpso::pairTag(pair.parties, pair.elements, pair.self, pair.other));
    writeReceiverMaterial(path, messages, choices);
}

void generatePairMaterial(const mpso::preproc::PartyPair& pair, Socket& channel)
{
    PRNG prng(sysRandomSeed());

    if (pair.role == mpso::preproc::PairRole::Receiver) {
        generateReceiverMaterial(pair, channel, prng);
    } else {
        generateSenderMaterial(pair, channel, prng);
    }

    coproto::sync_wait(channel.flush());
}

}

namespace mpso::preproc {

void generateMpsuRandomOt(
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
        runPartyPairs(channels, pairs, generatePairMaterial);
    });
}

}
