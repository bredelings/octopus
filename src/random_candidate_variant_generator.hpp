//
//  random_candidate_variant_generator.hpp
//  Octopus
//
//  Created by Daniel Cooke on 03/10/2015.
//  Copyright © 2015 Oxford University. All rights reserved.
//

#ifndef random_candidate_variant_generator_hpp
#define random_candidate_variant_generator_hpp

#include <vector>
#include <functional>

#include "i_candidate_variant_generator.hpp"
#include "aligned_read.hpp"

class ReferenceGenome;
class GenomicRegion;
class Variant;

namespace Octopus {
    
    class RandomCandidateVariantGenerator : public ICandidateVariantGenerator
    {
    public:
        using SizeType = AlignedRead::SizeType;
        
        RandomCandidateVariantGenerator() = delete;
        explicit RandomCandidateVariantGenerator(const ReferenceGenome& reference);
        ~RandomCandidateVariantGenerator() override = default;
        
        RandomCandidateVariantGenerator(const RandomCandidateVariantGenerator&)            = default;
        RandomCandidateVariantGenerator& operator=(const RandomCandidateVariantGenerator&) = default;
        RandomCandidateVariantGenerator(RandomCandidateVariantGenerator&&)                 = default;
        RandomCandidateVariantGenerator& operator=(RandomCandidateVariantGenerator&&)      = default;
        
        void add_reads(std::vector<AlignedRead>::const_iterator first, std::vector<AlignedRead>::const_iterator last) override;
        void add_reads(MappableFlatMultiSet<AlignedRead>::const_iterator first, MappableFlatMultiSet<AlignedRead>::const_iterator last) override;
        
        std::vector<Variant> generate_candidates(const GenomicRegion& region) override;
        
    private:
        std::reference_wrapper<const ReferenceGenome> reference_;
        SizeType max_read_size_ = 100;
    };
    
} // namespace Octopus

#endif /* random_candidate_variant_generator_hpp */