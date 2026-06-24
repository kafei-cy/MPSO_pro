#pragma once

#include <mpso/common/runtime.h>

#include <array>
#include <cryptoTools/Common/Aligned.h>
#include <cryptoTools/Common/BitVector.h>

namespace mpso::proto {

using OtSenderMessages = oc::AlignedVector<std::array<block, 2>>;
using OtReceiverMessages = oc::AlignedVector<block>;

void sendRandomOt(
    u64 num_ots,
    Socket& channel,
    PRNG& prng,
    OtSenderMessages& messages,
    u64 num_threads = 1);

void receiveRandomOt(
    const BitVector& choices,
    Socket& channel,
    PRNG& prng,
    OtReceiverMessages& messages,
    u64 num_threads = 1);

}
