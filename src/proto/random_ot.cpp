#include <mpso/proto/random_ot.h>

#include <mpso/common/parameters.h>

#include <libOTe/Base/BaseOT.h>
#include <libOTe/TwoChooseOne/SoftSpokenOT/SoftSpokenShOtExt.h>

namespace mpso::proto {

void sendRandomOt(
    u64 num_ots,
    Socket& channel,
    PRNG& prng,
    OtSenderMessages& messages,
    u64 num_threads)
{
    oc::SoftSpokenShOtSender<> sender;
    sender.init(kSsOtFieldBits, true, num_threads);

    PRNG ot_prng(prng.get<block>());
    oc::AlignedVector<block> base_messages(sender.baseOtCount());
    BitVector base_choices(sender.baseOtCount());
    base_choices.randomize(ot_prng);

    // The extension sender acts as the receiver in the base OT setup.
    oc::DefaultBaseOT base_ot;
    coproto::sync_wait(base_ot.receive(base_choices, base_messages, ot_prng, channel));

    sender.setBaseOts(base_messages, base_choices);

    messages.resize(num_ots);
    coproto::sync_wait(sender.send(messages, ot_prng, channel));
}

void receiveRandomOt(
    const BitVector& choices,
    Socket& channel,
    PRNG& prng,
    OtReceiverMessages& messages,
    u64 num_threads)
{
    oc::SoftSpokenShOtReceiver<> receiver;
    receiver.init(kSsOtFieldBits, true, num_threads);

    PRNG ot_prng(prng.get<block>());
    oc::AlignedVector<std::array<block, 2>> base_messages(receiver.baseOtCount());

    // The extension receiver acts as the sender in the base OT setup.
    oc::DefaultBaseOT base_ot;
    coproto::sync_wait(base_ot.send(base_messages, ot_prng, channel));

    receiver.setBaseOts(base_messages);

    messages.resize(choices.size());
    coproto::sync_wait(receiver.receive(choices, messages, ot_prng, channel));
}

}
