#include <mpso/preproc/offline_generator.h>

#include <mpso/common/cuckoo_layout.h>
#include <mpso/shuffle/shuffle_correlation.h>
#include <mpso/preproc/beaver_triple_generator.h>
#include <mpso/preproc/bool_triple_generator.h>
#include <mpso/preproc/preproc_common.h>
#include <mpso/preproc/random_ot_generator.h>
#include <mpso/preproc/vole_generator.h>

#include <iostream>
#include <stdexcept>
#include <string>

using mpso::u32;
using mpso::u64;

namespace {

constexpr const char* kPsiBeaverSuffix = "PSI64";
constexpr const char* kMpsicShuffleName = "psic";
constexpr const char* kMpsicsXorShuffleName = "psics1";
constexpr const char* kMpsicsAddShuffleName = "psics2";
constexpr const char* kMpsuShuffleName = "psu";

void validateConfig(const mpso::preproc::OfflineConfig& config)
{
    if (config.num_parties < 2) {
        throw std::invalid_argument("offline generation requires at least two parties");
    }
    if (config.party_index >= config.num_parties) {
        throw std::invalid_argument("offline generation party index is out of range");
    }
    if (config.log_num_elements >= 32) {
        throw std::invalid_argument("offline generation element count exceeds u32 range");
    }
    if (config.num_threads == 0) {
        throw std::invalid_argument("offline generation requires at least one thread");
    }
}

bool isCoordinator(const mpso::preproc::OfflineConfig& config)
{
    return config.party_index == 0;
}

u32 numBins(const mpso::preproc::OfflineConfig& config)
{
    return mpso::numCuckooBins(mpso::preproc::elementCount(config.log_num_elements));
}

void logCoordinatorStep(const mpso::preproc::OfflineConfig& config, const std::string& message)
{
    if (isCoordinator(config)) {
        std::cout << message << std::endl;
    }
}

void generatePsiBeaverLayer(const mpso::preproc::OfflineConfig& config)
{
    if (!isCoordinator(config)) {
        return;
    }

    mpso::preproc::generateBeaverTriples<u64>(
        config.num_parties,
        config.num_parties,
        config.log_num_elements,
        kPsiBeaverSuffix);
    logCoordinatorStep(config, "PSI Beaver triples generated");
}

void generateMpsicShuffleCorrelation(const mpso::preproc::OfflineConfig& config)
{
    if (!isCoordinator(config)) {
        return;
    }

    mpso::shuffle::XorCorrelation correlation(config.num_parties, numBins(config));
    correlation.generate();
    correlation.write(kMpsicShuffleName);
    correlation.clear();
    logCoordinatorStep(config, "MPSIC shuffle correlation generated");
}

void generateMpsicsShuffleCorrelations(const mpso::preproc::OfflineConfig& config)
{
    if (!isCoordinator(config)) {
        return;
    }

    const auto bins = numBins(config);

    mpso::shuffle::XorCorrelation xor_correlation(config.num_parties, bins);
    xor_correlation.generate();
    xor_correlation.write(kMpsicsXorShuffleName);
    xor_correlation.clear();

    mpso::shuffle::AddCorrelation add_correlation(config.num_parties, bins);
    add_correlation.generateWithPermutationsFrom(kMpsicsXorShuffleName);
    add_correlation.write(kMpsicsAddShuffleName);
    add_correlation.clear();

    logCoordinatorStep(config, "MPSICS shuffle correlations generated");
}

void generateMpsuShuffleCorrelation(const mpso::preproc::OfflineConfig& config)
{
    if (!isCoordinator(config)) {
        return;
    }

    mpso::shuffle::BlockCorrelation correlation(config.num_parties, (config.num_parties - 1) * numBins(config));
    correlation.generate();
    correlation.write(kMpsuShuffleName);
    correlation.clear();
    logCoordinatorStep(config, "MPSU shuffle correlation generated");
}

void generateMpsuArithmeticTriples(const mpso::preproc::OfflineConfig& config)
{
    if (!isCoordinator(config)) {
        return;
    }

    mpso::preproc::generateMpsuBeaverTriples(config.num_parties, config.log_num_elements);
    logCoordinatorStep(config, "MPSU Beaver triples generated");
}

}

namespace mpso::preproc {

void generateMpsiOffline(const OfflineConfig& config)
{
    validateConfig(config);
    generateMpsiVole(
        config.party_index,
        config.num_parties,
        config.log_num_elements,
        config.num_threads);
    generatePsiBeaverLayer(config);
}

void generateMpsicOffline(const OfflineConfig& config)
{
    validateConfig(config);
    generateMpsiVole(
        config.party_index,
        config.num_parties,
        config.log_num_elements,
        config.num_threads);
    generatePsiBeaverLayer(config);
    generateMpsicShuffleCorrelation(config);
}

void generateMpsicsOffline(const OfflineConfig& config)
{
    validateConfig(config);
    generateMpsicsVole(
        config.party_index,
        config.num_parties,
        config.log_num_elements,
        config.num_threads);
    generatePsiBeaverLayer(config);
    generateMpsicsShuffleCorrelations(config);
}

void generateMpsuOffline(const OfflineConfig& config)
{
    validateConfig(config);
    generateMpsuVole(
        config.party_index,
        config.num_parties,
        config.log_num_elements,
        config.num_threads);
    generateMpsuRandomOt(
        config.party_index,
        config.num_parties,
        config.log_num_elements,
        config.num_threads);
    generateMpsuBoolTriples(
        config.party_index,
        config.num_parties,
        config.log_num_elements,
        config.num_threads);
    generateMpsuShuffleCorrelation(config);
    generateMpsuArithmeticTriples(config);
}

}
