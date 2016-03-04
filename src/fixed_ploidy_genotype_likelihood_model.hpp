//
//  fixed_ploidy_genotype_likelihood_model.hpp
//  Octopus
//
//  Created by Daniel Cooke on 01/04/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef __Octopus__fixed_ploidy_genotype_likelihood_model__
#define __Octopus__fixed_ploidy_genotype_likelihood_model__

#include <cstddef>
#include <functional>
#include <iterator>
#include <vector>

#include "haplotype.hpp"
#include "genotype.hpp"
#include "aligned_read.hpp"
#include "haplotype_likelihood_cache.hpp"
#include "probability_matrix.hpp"

namespace Octopus
{
namespace GenotypeModel
{
    class FixedPloidyGenotypeLikelihoodModel
    {
    public:
        FixedPloidyGenotypeLikelihoodModel()  = delete;
        explicit FixedPloidyGenotypeLikelihoodModel(unsigned ploidy, const HaplotypeLikelihoodCache& haplotype_likelihoods);
        ~FixedPloidyGenotypeLikelihoodModel() = default;
        
        FixedPloidyGenotypeLikelihoodModel(const FixedPloidyGenotypeLikelihoodModel&)            = default;
        FixedPloidyGenotypeLikelihoodModel& operator=(const FixedPloidyGenotypeLikelihoodModel&) = default;
        FixedPloidyGenotypeLikelihoodModel(FixedPloidyGenotypeLikelihoodModel&&)                 = default;
        FixedPloidyGenotypeLikelihoodModel& operator=(FixedPloidyGenotypeLikelihoodModel&&)      = default;
        
        double log_likelihood(const SampleIdType& sample, const Genotype<Haplotype>& genotype) const;
        
    private:
        std::reference_wrapper<const HaplotypeLikelihoodCache> haplotype_likelihoods_;
        
        unsigned ploidy_;
        double ln_ploidy_;
        
        // These are just for optimisation
        double log_likelihood_haploid(const SampleIdType& sample, const Genotype<Haplotype>& genotype) const;
        double log_likelihood_diploid(const SampleIdType& sample, const Genotype<Haplotype>& genotype) const;
        double log_likelihood_triploid(const SampleIdType& sample, const Genotype<Haplotype>& genotype) const;
        double log_likelihood_polyploid(const SampleIdType& sample, const Genotype<Haplotype>& genotype) const;
    };
    
    namespace debug
    {
//        void print_genotype_log_likelihoods(const std::vector<Genotype<Haplotype>>& genotypes,
//                                            const FixedPloidyGenotypeLikelihoodModel& read_model,
//                                            size_t n = 5);
//        
//        void print_read_genotype_liklihoods(const std::vector<Genotype<Haplotype>>& genotypes,
//                                            const ReadMap& reads,
//                                            const FixedPloidyGenotypeLikelihoodModel& read_model,
//                                            size_t n = 3);
    } // namespace debug
} // namespace GenotypeModel
} // namespace Octopus

#endif /* defined(__Octopus__fixed_ploidy_genotype_likelihood_model__) */