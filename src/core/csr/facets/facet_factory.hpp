// Copyright (c) 2015-2018 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef facet_factory_hpp
#define facet_factory_hpp

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include <boost/optional.hpp>

#include "config/common.hpp"
#include "basics/ploidy_map.hpp"
#include "io/variant/vcf_header.hpp"
#include "io/variant/vcf_record.hpp"
#include "io/reference/reference_genome.hpp"
#include "readpipe/buffered_read_pipe.hpp"
#include "utils/genotype_reader.hpp"
#include "utils/thread_pool.hpp"
#include "facet.hpp"

namespace octopus { namespace csr {

class FacetFactory
{
public:
    using CallBlock  = std::vector<VcfRecord>;
    using FacetBlock = std::vector<FacetWrapper>;
    
    FacetFactory() = delete;
    
    FacetFactory(VcfHeader input_header);
    FacetFactory(VcfHeader input_header, const ReferenceGenome& reference, BufferedReadPipe read_pipe, PloidyMap ploidies);
    
    FacetFactory(const FacetFactory&)            = delete;
    FacetFactory& operator=(const FacetFactory&) = delete;
    FacetFactory(FacetFactory&&);
    FacetFactory& operator=(FacetFactory&&);
    
    ~FacetFactory() = default;
    
    FacetWrapper make(const std::string& name, const CallBlock& block) const;
    FacetBlock make(const std::vector<std::string>& names, const CallBlock& block) const;
    std::vector<FacetBlock> make(const std::vector<std::string>& names, const std::vector<CallBlock>& blocks, ThreadPool& workers) const;

private:
    struct BlockData
    {
        boost::optional<GenomicRegion> region;
        boost::optional<ReadMap> reads;
        boost::optional<GenotypeMap> genotypes;
    };
    
    VcfHeader input_header_;
    std::vector<std::string> samples_;
    boost::optional<std::reference_wrapper<const ReferenceGenome>> reference_;
    boost::optional<BufferedReadPipe> read_pipe_;
    boost::optional<PloidyMap> ploidies_;
    
    std::unordered_map<std::string, std::function<FacetWrapper(const BlockData& data)>> facet_makers_;
    
    void setup_facet_makers();
    void check_requirements(const std::string& name) const;
    void check_requirements(const std::vector<std::string>& names) const;
    FacetWrapper make(const std::string& name, const BlockData& block) const;
    FacetBlock make(const std::vector<std::string>& names, const BlockData& block) const;
    BlockData make_block_data(const std::vector<std::string>& names, const CallBlock& block) const;
};

} // namespace csr
} // namespace octopus

#endif
