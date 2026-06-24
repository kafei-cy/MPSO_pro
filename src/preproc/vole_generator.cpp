#include <mpso/preproc/vole_generator.h>

#include <mpso/common/cuckoo_layout.h>
#include <mpso/preproc/preproc_common.h>

#include <libOTe/Vole/Silent/SilentVoleReceiver.h>
#include <libOTe/Vole/Silent/SilentVoleSender.h>

#include <string>
#include <vector>

using mpso::block;
using mpso::PRNG;
using mpso::Socket;
using mpso::sysRandomSeed;
using mpso::u32;
using mpso::u64;

namespace {

constexpr auto kSilentVoleBaseType = oc::SilentBaseType::Base;

enum class VoleUse {
    Mpsu,
    Mpsi,
    MpsicsIndicator,
    MpsicsSum,
};

std::string voleSuffix(VoleUse use)
{
    switch (use) {
    case VoleUse::Mpsu:
        return "";
    case VoleUse::Mpsi:
        return "PSI";
    case VoleUse::MpsicsIndicator:
        return "PSICS1";
    case VoleUse::MpsicsSum:
        return "PSICS2";
    }

    throw std::invalid_argument("unknown VOLE use");
}

std::string volePath(const mpso::preproc::PartyPair& pair, VoleUse use)
{
    return mpso::voleFile(
        mpso::pairTag(pair.parties, pair.elements, pair.self, pair.other),
        voleSuffix(use));
}

void writeSenderMaterial(
    const std::string& path,
    const block& delta,
    const oc::AlignedUnVector<block>& b)
{
    auto out = mpso::preproc::openOutput(path);
    mpso::preproc::writeBinary(out, &delta, 1, path);
    mpso::preproc::writeBinary(out, b.data(), b.size(), path);
}

void writeReceiverMaterial(
    const std::string& path,
    const oc::AlignedUnVector<block>& a,
    const oc::AlignedUnVector<block>& c)
{
    auto out = mpso::preproc::openOutput(path);
    mpso::preproc::writeBinary(out, a.data(), a.size(), path);
    mpso::preproc::writeBinary(out, c.data(), c.size(), path);
}

void generateSenderMaterial(const mpso::preproc::PartyPair& pair, VoleUse use, Socket& channel, PRNG& prng)
{
    block delta = prng.get();

    oc::SilentVoleSender<block, block, oc::CoeffCtxGF128> sender;
    sender.mMalType = oc::SilentSecType::SemiHonest;
    sender.configure(pair.bins, kSilentVoleBaseType);

    oc::AlignedUnVector<block> b(pair.bins);
    coproto::sync_wait(sender.silentSend(delta, b, prng, channel));

    writeSenderMaterial(volePath(pair, use), delta, b);
}

void generateReceiverMaterial(const mpso::preproc::PartyPair& pair, VoleUse use, Socket& channel, PRNG& prng)
{
    oc::SilentVoleReceiver<block, block, oc::CoeffCtxGF128> receiver;
    receiver.mMalType = oc::SilentSecType::SemiHonest;
    receiver.configure(pair.bins, kSilentVoleBaseType);

    oc::AlignedUnVector<block> a(pair.bins);
    oc::AlignedUnVector<block> c(pair.bins);
    coproto::sync_wait(receiver.silentReceive(c, a, prng, channel));

    writeReceiverMaterial(volePath(pair, use), a, c);
}

void generateVole(const mpso::preproc::PartyPair& pair, VoleUse use, Socket& channel)
{
    PRNG prng(sysRandomSeed());

    if (pair.role == mpso::preproc::PairRole::Receiver) {
        generateReceiverMaterial(pair, use, channel, prng);
    } else {
        generateSenderMaterial(pair, use, channel, prng);
    }

    coproto::sync_wait(channel.flush());
}

std::vector<Socket> connectToCoordinator(u32 party_index, u32 num_parties)
{
    std::vector<Socket> channels;

    if (party_index == 0) {
        channels.reserve(num_parties - 1);
        for (u32 other = 1; other < num_parties; ++other) {
            channels.emplace_back(mpso::connectParty(party_index, other));
        }
    } else {
        channels.emplace_back(mpso::connectParty(party_index, 0));
    }

    return channels;
}

mpso::preproc::PartyPair participantPair(
    u32 party_index,
    u32 num_parties,
    u32 num_elements,
    u32 num_bins)
{
    return mpso::preproc::PartyPair{
        party_index,
        u32{0},
        num_parties,
        num_elements,
        num_bins,
        u32{1},
        mpso::preproc::PairRole::Sender,
    };
}

mpso::preproc::PartyPair coordinatorPair(
    u32 party_index,
    u32 other,
    u32 num_parties,
    u32 num_elements,
    u32 num_bins)
{
    return mpso::preproc::PartyPair{
        party_index,
        other,
        num_parties,
        num_elements,
        num_bins,
        u32{1},
        mpso::preproc::PairRole::Receiver,
    };
}

}

namespace mpso::preproc {

void generateMpsuVole(
    u32 party_index,
    u32 num_parties,
    u32 log_num_elements,
    u32 num_threads)
{
    (void)num_threads;

    checkParty(party_index, num_parties);
    const auto num_elements = elementCount(log_num_elements);
    const auto num_bins = mpso::numCuckooBins(num_elements);

    runWithChannels(mpso::connectParties(party_index, num_parties), [&](auto& channels) {
        const auto pairs = allPartyPairs(party_index, num_parties, num_elements, num_bins, 1);
        runPartyPairs(channels, pairs, [](const auto& pair, auto& channel) {
            generateVole(pair, VoleUse::Mpsu, channel);
        });
    });
}

void generateMpsiVole(
    u32 party_index,
    u32 num_parties,
    u32 log_num_elements,
    u32 num_threads)
{
    (void)num_threads;

    checkParty(party_index, num_parties);
    const auto num_elements = elementCount(log_num_elements);
    const auto num_bins = mpso::numCuckooBins(num_elements);

    runWithChannels(connectToCoordinator(party_index, num_parties), [&](auto& channels) {
        if (party_index == 0) {
            std::vector<PartyPair> pairs;
            pairs.reserve(num_parties - 1);
            for (u32 other = 1; other < num_parties; ++other) {
                pairs.push_back(coordinatorPair(party_index, other, num_parties, num_elements, num_bins));
            }
            runPartyPairs(channels, pairs, [](const auto& pair, auto& channel) {
                generateVole(pair, VoleUse::Mpsi, channel);
            });
        } else {
            const auto pair = participantPair(party_index, num_parties, num_elements, num_bins);
            generateVole(pair, VoleUse::Mpsi, channels[0]);
        }
    });
}

void generateMpsicsVole(
    u32 party_index,
    u32 num_parties,
    u32 log_num_elements,
    u32 num_threads)
{
    (void)num_threads;

    checkParty(party_index, num_parties);
    const auto num_elements = elementCount(log_num_elements);
    const auto num_bins = mpso::numCuckooBins(num_elements);

    runWithChannels(connectToCoordinator(party_index, num_parties), [&](auto& channels) {
        if (party_index == 0) {
            std::vector<PartyPair> pairs;
            pairs.reserve(num_parties - 1);
            for (u32 other = 1; other < num_parties; ++other) {
                pairs.push_back(coordinatorPair(party_index, other, num_parties, num_elements, num_bins));
            }
            runPartyPairs(channels, pairs, [](const auto& pair, auto& channel) {
                generateVole(pair, VoleUse::MpsicsIndicator, channel);
                generateVole(pair, VoleUse::MpsicsSum, channel);
            });
        } else {
            const auto pair = participantPair(party_index, num_parties, num_elements, num_bins);
            generateVole(pair, VoleUse::MpsicsIndicator, channels[0]);
            generateVole(pair, VoleUse::MpsicsSum, channels[0]);
        }
    });
}

}
