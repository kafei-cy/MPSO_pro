#include <mpso/shuffle/online_shuffle.h>

#include <mpso/common/permutation.h>
#include <mpso/shuffle/shuffle_common.h>

#include <stdexcept>

using coproto::task;

namespace mpso::shuffle {

template<typename T, ShareMode Mode>
Online<T, Mode>::Online(u32 party_index, u32 num_parties, u32 num_elements)
    : party_index_(party_index),
      num_parties_(num_parties),
      num_elements_(num_elements)
{
    if (num_parties_ < 2) {
        throw std::invalid_argument("online shuffle requires at least two parties");
    }
    if (party_index_ >= num_parties_) {
        throw std::invalid_argument("online shuffle party index is out of range");
    }
    if (num_elements_ == 0) {
        throw std::invalid_argument("online shuffle requires at least one element");
    }

    permutation_.resize(num_elements_);

    if (!isFirstParty()) {
        a_.resize(num_elements_);
    }
    if (!isLastParty()) {
        a_prime_.resize(num_elements_);
        b_.resize(num_elements_);
    } else {
        delta_.resize(num_elements_);
    }
}

template<typename T, ShareMode Mode>
void Online<T, Mode>::load(const std::string& name)
{
    const auto path = filePath(name, num_parties_, num_elements_, party_index_);
    auto in = openInput(path);

    readBinary(in, permutation_, path);
    validatePermutation(permutation_, num_elements_);

    if (!isFirstParty()) {
        readBinary(in, a_, path);
    }
    if (!isLastParty()) {
        readBinary(in, a_prime_, path);
        readBinary(in, b_, path);
    } else {
        readBinary(in, delta_, path);
    }
}

template<typename T, ShareMode Mode>
Proto Online<T, Mode>::run(std::vector<Socket>& channels, std::vector<T>& data)
{
    MC_BEGIN(
        task<>, this, &channels, &data,
        i = u32{}, j = u32{},
        masked_shares = std::vector<std::vector<T>>{},
        forwarded = std::vector<T>{});

    if (channels.size() != num_parties_ - 1) {
        throw std::invalid_argument("online shuffle channel count is invalid");
    }
    if (data.size() != num_elements_) {
        throw std::invalid_argument("online shuffle input size is invalid");
    }

    if (isFirstParty()) {
        masked_shares.resize(num_parties_ - 1);
        for (i = 0; i < num_parties_ - 1; ++i) {
            masked_shares[i].resize(num_elements_);
            MC_AWAIT(channels[i].recv(masked_shares[i]));
        }

        forwarded.assign(num_elements_, identity<T>());
        for (i = 0; i < num_parties_ - 1; ++i) {
            for (j = 0; j < num_elements_; ++j) {
                add<T, Mode>(forwarded[j], masked_shares[i][j]);
            }
        }
        for (j = 0; j < num_elements_; ++j) {
            add<T, Mode>(forwarded[j], data[j]);
        }

        mpso::permute(permutation_, forwarded, PermuteSafety::Trust);
        for (j = 0; j < num_elements_; ++j) {
            add<T, Mode>(forwarded[j], a_prime_[j]);
        }
        MC_AWAIT(channels[nextChannel()].send(forwarded));

        data.resize(num_elements_);
        for (j = 0; j < num_elements_; ++j) {
            data[j] = inverse<T, Mode>(b_[j]);
        }
    } else {
        forwarded = a_;
        for (j = 0; j < num_elements_; ++j) {
            add<T, Mode>(forwarded[j], data[j]);
        }
        MC_AWAIT(channels[0].send(forwarded));

        forwarded.resize(num_elements_);
        MC_AWAIT(channels[prevChannel()].recv(forwarded));

        mpso::permute(permutation_, forwarded, PermuteSafety::Trust);
        if (isLastParty()) {
            for (j = 0; j < num_elements_; ++j) {
                sub<T, Mode>(forwarded[j], delta_[j]);
            }
            data = forwarded;
        } else {
            for (j = 0; j < num_elements_; ++j) {
                add<T, Mode>(forwarded[j], a_prime_[j]);
            }
            MC_AWAIT(channels[nextChannel()].send(forwarded));

            data.resize(num_elements_);
            for (j = 0; j < num_elements_; ++j) {
                data[j] = inverse<T, Mode>(b_[j]);
            }
        }
    }

    MC_END();
}

template class Online<block, ShareMode::Xor>;
template class Online<u64, ShareMode::Xor>;
template class Online<u64, ShareMode::Add>;

}
