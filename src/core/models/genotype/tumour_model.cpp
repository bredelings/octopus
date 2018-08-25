// Copyright (c) 2015-2018 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "tumour_model.hpp"

#include <algorithm>
#include <numeric>
#include <iterator>
#include <functional>
#include <cstddef>
#include <cmath>
#include <cassert>

#include "exceptions/unimplemented_feature_error.hpp"
#include "utils/maths.hpp"
#include "logging/logging.hpp"
#include "germline_likelihood_model.hpp"
#include "variational_bayes_mixture_model.hpp"

namespace octopus { namespace model {

TumourModel::TumourModel(std::vector<SampleName> samples, Priors priors)
: TumourModel {std::move(samples), std::move(priors), AlgorithmParameters {}}
{}

TumourModel::TumourModel(std::vector<SampleName> samples, Priors priors, AlgorithmParameters parameters)
: samples_ {std::move(samples)}
, priors_ {std::move(priors)}
, parameters_ {parameters}
{}

const TumourModel::Priors& TumourModel::priors() const noexcept
{
    return priors_;
}

namespace {

TumourModel::InferredLatents
run_variational_bayes(const std::vector<SampleName>& samples,
                      const std::vector<CancerGenotype<Haplotype>>& genotypes,
                      const TumourModel::Priors& priors,
                      const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                      const TumourModel::AlgorithmParameters& params);

TumourModel::InferredLatents
run_variational_bayes(const std::vector<SampleName>& samples,
                      const std::vector<CancerGenotype<Haplotype>>& genotypes,
                      const std::vector<CancerGenotypeIndex>& genotype_indices,
                      const TumourModel::Priors& priors,
                      const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                      const TumourModel::AlgorithmParameters& params);

} // namespace

TumourModel::InferredLatents
TumourModel::evaluate(const std::vector<CancerGenotype<Haplotype>>& genotypes,
                      const HaplotypeLikelihoodCache& haplotype_likelihoods) const
{
    assert(!genotypes.empty());
    return run_variational_bayes(samples_, genotypes, priors_, haplotype_likelihoods, parameters_);
}

TumourModel::InferredLatents
TumourModel::evaluate(const std::vector<CancerGenotype<Haplotype>>& genotypes,
                      const std::vector<CancerGenotypeIndex>& genotype_indices,
                      const HaplotypeLikelihoodCache& haplotype_likelihoods) const
{
    assert(!genotypes.empty());
    assert(genotypes.size() == genotype_indices.size());
    return run_variational_bayes(samples_, genotypes, genotype_indices, priors_, haplotype_likelihoods, parameters_);
}

namespace {

template <std::size_t K>
VBAlpha<K> flatten(const TumourModel::Priors::GenotypeMixturesDirichletAlphas& alpha)
{
    VBAlpha<K> result {};
    std::copy_n(std::cbegin(alpha), K, std::begin(result));
    return result;
}

template <std::size_t K>
VBAlphaVector<K> flatten(const TumourModel::Priors::GenotypeMixturesDirichletAlphaMap& alphas,
                         const std::vector<SampleName>& samples)
{
    VBAlphaVector<K> result(samples.size());
    std::transform(std::cbegin(samples), std::cend(samples), std::begin(result),
                   [&alphas] (const auto& sample) { return flatten<K>(alphas.at(sample)); });
    return result;
}

template <std::size_t K>
auto copy_cref(const Genotype<Haplotype>& genotype, const SampleName& sample,
               const HaplotypeLikelihoodCache& haplotype_likelihoods,
               typename VBGenotype<K>::iterator result_itr)
{
    return std::transform(std::cbegin(genotype), std::cend(genotype), result_itr,
                          [&sample, &haplotype_likelihoods] (const Haplotype& haplotype)
                          -> std::reference_wrapper<const VBReadLikelihoodArray::BaseType> {
                            return std::cref(haplotype_likelihoods(sample, haplotype));
                           });
}

template <std::size_t K>
VBGenotype<K>
flatten(const CancerGenotype<Haplotype>& genotype, const SampleName& sample,
        const HaplotypeLikelihoodCache& haplotype_likelihoods)
{
    VBGenotype<K> result {};
    assert(genotype.ploidy() == K);
    auto itr = copy_cref<K>(genotype.germline(), sample, haplotype_likelihoods, std::begin(result));
    copy_cref<K>(genotype.somatic(), sample, haplotype_likelihoods, itr);
    return result;
}

template <std::size_t K>
VBGenotypeVector<K>
flatten(const std::vector<CancerGenotype<Haplotype>>& genotypes, const SampleName& sample,
        const HaplotypeLikelihoodCache& haplotype_likelihoods)
{
    VBGenotypeVector<K> result(genotypes.size());
    std::transform(std::cbegin(genotypes), std::cend(genotypes), std::begin(result),
                   [&sample, &haplotype_likelihoods] (const auto& genotype) {
                       return flatten<K>(genotype, sample, haplotype_likelihoods);
                   });
    return result;
}

template <std::size_t K>
VBReadLikelihoodMatrix<K>
flatten(const std::vector<CancerGenotype<Haplotype>>& genotypes,
        const std::vector<SampleName>& samples,
        const HaplotypeLikelihoodCache& haplotype_likelihoods)
{
    VBReadLikelihoodMatrix<K> result {};
    result.reserve(samples.size());
    std::transform(std::cbegin(samples), std::cend(samples), std::back_inserter(result),
                   [&genotypes, &haplotype_likelihoods] (const auto& sample) {
                       return flatten<K>(genotypes, sample, haplotype_likelihoods);
                   });
    return result;
}

template <std::size_t K>
TumourModel::Latents::GenotypeMixturesDirichletAlphas expand(VBAlpha<K>& alpha)
{
    return TumourModel::Latents::GenotypeMixturesDirichletAlphas(std::begin(alpha), std::end(alpha));
}

template <std::size_t K>
TumourModel::Latents::GenotypeMixturesDirichletAlphaMap
expand(const std::vector<SampleName>& samples, VBAlphaVector<K>&& alphas)
{
    TumourModel::Latents::GenotypeMixturesDirichletAlphaMap result {};
    std::transform(std::cbegin(samples), std::cend(samples), std::begin(alphas),
                   std::inserter(result, std::begin(result)),
                   [] (const auto& sample, auto&& vb_alpha) {
                       return std::make_pair(sample, expand(vb_alpha));
                   });
    return result;
}

template <std::size_t K>
TumourModel::InferredLatents
expand(const std::vector<SampleName>& samples, VBLatents<K>&& inferred_latents,
       std::vector<double> genotype_log_priors, double evidence)
{
    TumourModel::Latents posterior_latents {std::move(inferred_latents.genotype_posteriors),
                                            expand(samples, std::move(inferred_latents.alphas))};
    return {std::move(posterior_latents), std::move(genotype_log_priors), evidence};
}

auto compute_germline_log_likelihoods(const SampleName& sample,
                                      const std::vector<CancerGenotype<Haplotype>>& genotypes,
                                      const HaplotypeLikelihoodCache& haplotype_log_likelihoods)
{
    haplotype_log_likelihoods.prime(sample);
    const GermlineLikelihoodModel likelihood_model {haplotype_log_likelihoods};
    std::vector<double> result(genotypes.size());
    std::transform(std::cbegin(genotypes), std::cend(genotypes), std::begin(result),
                   [&] (const auto& genotype) { return likelihood_model.evaluate(genotype.germline()); });
    return result;
}

auto compute_demoted_log_likelihoods(const SampleName& sample,
                                     const std::vector<CancerGenotype<Haplotype>>& genotypes,
                                     const HaplotypeLikelihoodCache& haplotype_log_likelihoods)
{
    assert(!genotypes.empty());
    haplotype_log_likelihoods.prime(sample);
    const GermlineLikelihoodModel likelihood_model {haplotype_log_likelihoods};
    std::vector<double> result(genotypes.size());
    std::transform(std::cbegin(genotypes), std::cend(genotypes), std::begin(result),
                   [&] (const auto& genotype) { return likelihood_model.evaluate(demote(genotype)); });
    return result;
}

auto compute_log_posteriors(const LogProbabilityVector& log_priors, const LogProbabilityVector& log_likelihoods)
{
    assert(log_priors.size() == log_likelihoods.size());
    LogProbabilityVector result(log_priors.size());
    std::transform(std::cbegin(log_priors), std::cend(log_priors), std::cbegin(log_likelihoods), std::begin(result),
                   [] (auto prior, auto likelihood) noexcept { return prior + likelihood; });
    maths::normalise_logs(result);
    return result;
}

LogProbabilityVector log_uniform_dist(const std::size_t n)
{
    return LogProbabilityVector(n, -std::log(static_cast<double>(n)));
}

auto make_point_seed(const std::size_t num_genotypes, const std::size_t n, const double p = 0.9999)
{
    LogProbabilityVector result(num_genotypes, num_genotypes > 1 ? std::log((1 - p) / (num_genotypes - 1)) : 0);
    if (num_genotypes > 1) result[n] = std::log(p);
    return result;
}

void make_point_seeds(const std::size_t num_genotypes, const std::vector<std::size_t>& ns,
                      std::vector<LogProbabilityVector>& result, const double p = 0.9999)
{
    result.reserve(result.size() + ns.size());
    std::transform(std::cbegin(ns), std::cend(ns), std::back_inserter(result),
                   [=] (auto n) { return make_point_seed(num_genotypes, n, p); });
}

auto make_range_seed(const std::size_t num_genotypes, const std::size_t begin, const std::size_t n, const double p = 0.9999)
{
    LogProbabilityVector result(num_genotypes, std::log((1 - p) / (num_genotypes - n)));
    std::fill_n(std::next(std::begin(result), begin), n, std::log(p / n));
    return result;
}

auto make_range_seed(const std::vector<CancerGenotype<Haplotype>>& genotypes, const Genotype<Haplotype>& germline, const double p = 0.9999)
{
    auto itr1 = std::find_if(std::cbegin(genotypes), std::cend(genotypes), [&] (const auto& g) { return g.germline() == germline; });
    auto itr2 = std::find_if_not(std::next(itr1), std::cend(genotypes), [&] (const auto& g) { return g.germline() == germline; });
    return make_range_seed(genotypes.size(), std::distance(std::cbegin(genotypes), itr1), std::distance(itr1, itr2));
}

namespace debug {

template <typename S>
void print_top(S&& stream, const std::vector<CancerGenotype<Haplotype>>& genotypes,
               const LogProbabilityVector& probs, std::size_t n)
{
    assert(probs.size() == genotypes.size());
    n = std::min(n, genotypes.size());
    std::vector<std::pair<CancerGenotype<Haplotype>, double> > pairs {};
    pairs.reserve(genotypes.size());
    std::transform(std::cbegin(genotypes), std::cend(genotypes), std::cbegin(probs), std::back_inserter(pairs),
                   [] (const auto& g, auto p) { return std::make_pair(g, p); });
    const auto mth = std::next(std::begin(pairs), n);
    std::partial_sort(std::begin(pairs), mth, std::end(pairs),
                      [] (const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
    std::for_each(std::begin(pairs), mth, [&] (const auto& p) {
        octopus::debug::print_variant_alleles(stream, p.first);
        stream << " " << p.second << '\n';
    });
}

void print_top(const std::vector<CancerGenotype<Haplotype>>& genotypes,
               const LogProbabilityVector& probs, std::size_t n = 10)
{
    print_top(std::cout, genotypes, probs, n);
}

} // namespace debug

bool is_somatic_expected(const SampleName& sample, const TumourModel::Priors& priors)
{
    const auto& alphas = priors.alphas.at(sample);
    auto e = maths::dirichlet_expectation(alphas.size() - 1, alphas);
    return e > 0.05;
}

void add_to(const LogProbabilityVector& other, LogProbabilityVector& result)
{
    std::transform(std::cbegin(other), std::cend(other), std::cbegin(result), std::begin(result),
                   [] (auto a, auto b) { return a + b; });
}

auto generate_exhaustive_seeds(const std::size_t n)
{
    std::vector<LogProbabilityVector> result {};
    result.reserve(n);
    for (unsigned i {0}; i < n; ++i) {
        result.push_back(make_point_seed(n, i));
    }
    return result;
}

auto num_weighted_seeds(const std::vector<SampleName>& samples, const std::vector<CancerGenotype<Haplotype>>& genotypes) noexcept
{
    return 1 + 4 * samples.size() + 2 * (samples.size() > 1);
}

auto generate_weighted_seeds(const std::vector<SampleName>& samples,
                             const std::vector<CancerGenotype<Haplotype>>& genotypes,
                             const LogProbabilityVector& genotype_log_priors,
                             const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                             const TumourModel::Priors& priors)
{
    std::vector<LogProbabilityVector> result {};
    result.reserve(num_weighted_seeds(samples, genotypes));
    result.push_back(genotype_log_priors);
    maths::normalise_logs(result.back());
    LogProbabilityVector combined_log_likelihoods(genotypes.size(), 0);
    for (const auto& sample : samples) {
        auto log_likelihoods = compute_germline_log_likelihoods(sample, genotypes, haplotype_log_likelihoods);
        auto demoted_log_likelihoods = compute_demoted_log_likelihoods(sample, genotypes, haplotype_log_likelihoods);
        if (is_somatic_expected(sample, priors)) {
            add_to(demoted_log_likelihoods, combined_log_likelihoods);
        } else {
            add_to(log_likelihoods, combined_log_likelihoods);
        }
        result.push_back(compute_log_posteriors(genotype_log_priors, log_likelihoods));
        maths::normalise_logs(log_likelihoods);
        result.push_back(std::move(log_likelihoods));
        result.push_back(compute_log_posteriors(genotype_log_priors, demoted_log_likelihoods));
        maths::normalise_logs(demoted_log_likelihoods);
        result.push_back(std::move(demoted_log_likelihoods));
    }
    if (samples.size() > 1) {
        auto combined_log_posteriors = combined_log_likelihoods;
        add_to(genotype_log_priors, combined_log_posteriors);
        maths::normalise_logs(combined_log_posteriors);
        result.push_back(std::move(combined_log_posteriors));
        maths::normalise_logs(combined_log_likelihoods);
        result.push_back(std::move(combined_log_likelihoods));
    }
    return result;
}

//auto estimate_haplotype_frequencies(const std::vector<Haplotype>& haplotypes, const std::vector<SampleName>& samples,
//                                    const HaplotypeLikelihoodCache& haplotype_log_likelihoods)
//{
//    std::vector<double> result(haplotypes.size());
//    const GermlineLikelihoodModel likelihood_model {haplotype_log_likelihoods};
//    std::transform(std::cbegin(haplotypes), std::cend(haplotypes), std::begin(result), [&] (const auto& haplotype) {
//        const Genotype<Haplotype> haploid_dummy {haplotype};
//        return std::accumulate(std::cbegin(samples), std::cend(samples), 0.0, [&] (auto curr, const auto& sample) {
//            return curr + likelihood_model.evaluate(haploid_dummy);
//        });
//    });
//    maths::normalise_exp(result);
//    return result;
//}
//
//auto order_by_estimated_frequency(const std::vector<Haplotype>& haplotypes, const std::vector<SampleName>& samples,
//                                  const HaplotypeLikelihoodCache& haplotype_log_likelihoods)
//{
//    const auto estimated_frequencies = estimate_haplotype_frequencies(haplotypes, )
//}

auto generate_targetted_seeds(const std::vector<CancerGenotype<Haplotype>>& genotypes,
                              const LogProbabilityVector& approx_log_likelihoods,
                              const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                              const TumourModel::Priors& priors,
                              std::size_t n, std::vector<LogProbabilityVector>& result)
{
    if (n == 0) return;
    if (n >= genotypes.size()) {
        for (std::size_t i {0}; i < genotypes.size(); ++i) {
            result.push_back(make_point_seed(genotypes.size(), i));
        }
    }
    std::vector<std::pair<double, std::size_t>> dummy_log_likelihoods {};
    dummy_log_likelihoods.reserve(genotypes.size());
    for (std::size_t i {0}; i < genotypes.size(); ++i) {
        dummy_log_likelihoods.push_back({approx_log_likelihoods[i], i});
    }
    std::sort(std::begin(dummy_log_likelihoods), std::end(dummy_log_likelihoods), std::greater<> {});
    std::vector<Genotype<Haplotype>> selected_germline_genotypes {};
    auto dummy_block_start_itr = std::begin(dummy_log_likelihoods);
    while (true) {
        auto dummy_block_end_itr = std::find_if_not(std::next(dummy_block_start_itr), std::end(dummy_log_likelihoods),
                                                    [=] (const auto& p) { return p.first == dummy_block_start_itr->first; });
        if (dummy_block_end_itr != std::next(dummy_block_start_itr)) {
            std::sort(dummy_block_start_itr, dummy_block_end_itr, [&] (const auto& lhs, const auto& rhs) {
                return priors.genotype_prior_model.evaluate(genotypes[lhs.second]) > priors.genotype_prior_model.evaluate(genotypes[rhs.second]);
            });
        }
        std::transform(dummy_block_start_itr, dummy_block_end_itr, std::back_inserter(selected_germline_genotypes),
                       [&] (const auto& p) { return genotypes[p.second].germline(); });
//        selected_germline_genotypes.push_back(genotypes[dummy_block_start_itr->second].germline());
        break; // TODO: how many germline genotypes to use?
        dummy_block_start_itr = dummy_block_end_itr;
    }
    
//    if (genotypes.size() == 8327) {
//        std::cout << "Top 200 genotype likelihoods:" << std::endl;
//        std::for_each(std::cbegin(dummy_log_likelihoods), std::next(std::cbegin(dummy_log_likelihoods), 200), [&] (const auto& p) {
//            std::cout << p.second << ": "; octopus::debug::print_variant_alleles(genotypes[p.second]); std::cout << " " << p.first << std::endl;
//        });
//    }
    
    for (const auto& germline : selected_germline_genotypes) {
        result.push_back(make_range_seed(genotypes, germline));
        --n;
        if (n == 0) break;
    }
    if (n > 0 && genotypes.front().somatic_ploidy() > 1) {
        std::vector<std::size_t> point_seeds {};
        point_seeds.reserve(n);
        // Genotype likelihoods will generally be dominated by a single haplotype and therefore appear in 'runs'
        std::vector<Haplotype> dominant_haplotypes {}, buffer {};
        for (const auto& p : dummy_log_likelihoods) {
            if (genotypes[p.second].germline() != selected_germline_genotypes.front()) {
                break;
            }
            const auto& somatics = genotypes[p.second].somatic();
            if (dominant_haplotypes.empty()) {
                dominant_haplotypes.assign(std::cbegin(somatics), std::cend(somatics));
            } else {
                std::set_difference(std::cbegin(dominant_haplotypes), std::cend(dominant_haplotypes),
                                    std::cbegin(somatics), std::cend(somatics), std::back_inserter(buffer));
                std::swap(buffer, dominant_haplotypes);
                buffer.clear();
                if (dominant_haplotypes.empty()) {
                    point_seeds.push_back(p.second);
                    --n;
                    if (n == 0) break;
                }
            }
        }
        if (n > 0) {
            for (const auto& p : dummy_log_likelihoods) {
                if (genotypes[p.second].germline() == selected_germline_genotypes.front()
                    && std::find(std::cbegin(point_seeds), std::cend(point_seeds), p.second) == std::cend(point_seeds)) {
                    point_seeds.push_back(p.second);
                    --n;
                    if (n == 0) break;
                }
            }
        }
        make_point_seeds(genotypes.size(), point_seeds, result);
    }
}

auto generate_seeds(const std::vector<SampleName>& samples,
                    const std::vector<CancerGenotype<Haplotype>>& genotypes,
                    const LogProbabilityVector& genotype_log_priors,
                    const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                    const TumourModel::Priors& priors,
                    const std::size_t max_seeds)
{
    if (genotypes.size() <= std::min(max_seeds, num_weighted_seeds(samples, genotypes))) {
        return generate_exhaustive_seeds(genotypes.size());
    } else {
        auto result = generate_weighted_seeds(samples, genotypes, genotype_log_priors, haplotype_log_likelihoods, priors);
        if (result.size() < max_seeds) {
            result.reserve(max_seeds);
            generate_targetted_seeds(genotypes, result.back(), haplotype_log_likelihoods, priors, max_seeds - result.size(), result);
        }
        return result;
    }
}

template <std::size_t K>
TumourModel::InferredLatents
run_variational_bayes(const std::vector<SampleName>& samples,
                      const std::vector<CancerGenotype<Haplotype>>& genotypes,
                      const TumourModel::Priors::GenotypeMixturesDirichletAlphaMap& prior_alphas,
                      std::vector<double> genotype_log_priors,
                      const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                      const VariationalBayesParameters& params,
                      std::vector<std::vector<double>>&& seeds)
{
    const auto vb_prior_alphas = flatten<K>(prior_alphas, samples);
    const auto log_likelihoods = flatten<K>(genotypes, samples, haplotype_log_likelihoods);
    auto p = run_variational_bayes(vb_prior_alphas, genotype_log_priors, log_likelihoods, params, std::move(seeds));
    return expand(samples, std::move(p.first), std::move(genotype_log_priors), p.second);
}

TumourModel::InferredLatents
run_variational_bayes_helper(const std::vector<SampleName>& samples,
                             const std::vector<CancerGenotype<Haplotype>>& genotypes,
                             const TumourModel::Priors::GenotypeMixturesDirichletAlphaMap& prior_alphas,
                             std::vector<double> genotype_log_priors,
                             const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                             const TumourModel::AlgorithmParameters& params,
                             std::vector<std::vector<double>>&& seeds)
{
    const VariationalBayesParameters vb_params {params.epsilon, params.max_iterations};
    using std::move;
    switch (genotypes.front().ploidy()) {
        case 2: return run_variational_bayes<2>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                haplotype_log_likelihoods, vb_params, move(seeds));
        case 3: return run_variational_bayes<3>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                haplotype_log_likelihoods, vb_params, move(seeds));
        case 4: return run_variational_bayes<4>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                haplotype_log_likelihoods, vb_params, move(seeds));
        case 5: return run_variational_bayes<5>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                haplotype_log_likelihoods, vb_params, move(seeds));
        case 6: return run_variational_bayes<6>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                haplotype_log_likelihoods, vb_params, move(seeds));
        case 7: return run_variational_bayes<7>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                haplotype_log_likelihoods, vb_params, move(seeds));
        case 8: return run_variational_bayes<8>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                haplotype_log_likelihoods, vb_params, move(seeds));
        default: throw UnimplementedFeatureError {"tumour model ploidies above 8", "TumourModel"};
    }
}

// Main entry point

TumourModel::InferredLatents
run_variational_bayes(const std::vector<SampleName>& samples,
                      const std::vector<CancerGenotype<Haplotype>>& genotypes,
                      const TumourModel::Priors& priors,
                      const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                      const TumourModel::AlgorithmParameters& params)
{
    auto genotype_log_priors = calculate_log_priors(genotypes, priors.genotype_prior_model);
    auto seeds = generate_seeds(samples, genotypes, genotype_log_priors, haplotype_log_likelihoods, priors, params.max_seeds);
    return run_variational_bayes_helper(samples, genotypes, priors.alphas, std::move(genotype_log_priors),
                                        haplotype_log_likelihoods, params, std::move(seeds));
}

TumourModel::InferredLatents
run_variational_bayes(const std::vector<SampleName>& samples,
                      const std::vector<CancerGenotype<Haplotype>>& genotypes,
                      const std::vector<CancerGenotypeIndex>& genotype_indices,
                      const TumourModel::Priors& priors,
                      const HaplotypeLikelihoodCache& haplotype_log_likelihoods,
                      const TumourModel::AlgorithmParameters& params)
{
    auto genotype_log_priors = calculate_log_priors(genotype_indices, priors.genotype_prior_model);
    auto seeds = generate_seeds(samples, genotypes, genotype_log_priors, haplotype_log_likelihoods, priors, params.max_seeds);
    return run_variational_bayes_helper(samples, genotypes, priors.alphas, std::move(genotype_log_priors),
                                        haplotype_log_likelihoods, params, std::move(seeds));
}

} // namespace

} // namespace model
} // namespace octopus
