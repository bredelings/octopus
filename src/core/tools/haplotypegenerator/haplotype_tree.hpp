//
//  haplotype_tree.hpp
//  Octopus
//
//  Created by Daniel Cooke on 22/03/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef __Octopus__haplotype_tree__
#define __Octopus__haplotype_tree__

#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <functional>
#include <iterator>
#include <algorithm>
#include <type_traits>

#include <boost/graph/adjacency_list.hpp>

#include <basics/genomic_region.hpp>
#include <core/types/allele.hpp>
#include <core/types/haplotype.hpp>
#include <core/types/variant.hpp>

namespace octopus {

class ReferenceGenome;

namespace coretools {

class HaplotypeTree
{
public:
    HaplotypeTree() = delete;
    
    HaplotypeTree(const GenomicRegion::ContigName& contig, const ReferenceGenome& reference);
    
    HaplotypeTree(const HaplotypeTree&);
    HaplotypeTree& operator=(HaplotypeTree);
    HaplotypeTree(HaplotypeTree&&)            = default;
    HaplotypeTree& operator=(HaplotypeTree&&) = default;
    
    ~HaplotypeTree() = default;
    
    bool is_empty() const noexcept;
    
    unsigned num_haplotypes() const noexcept;
    
    // uses Haplotype::operator== logic
    bool contains(const Haplotype& haplotype) const;
    
    // uses Haplotype::HaveSameAlleles logic
    bool includes(const Haplotype& haplotype) const;
    
    // using Haplotype::HaveSameAlleles logic
    bool is_unique(const Haplotype& haplotype) const;
    
    // Only extends existing leafs
    HaplotypeTree& extend(const ContigAllele& allele);
    HaplotypeTree& extend(const Allele& allele);
    
    // Splices into the tree wherever allele can be made a new leaf
    void splice(const ContigAllele& allele);
    void splice(const Allele& allele);
    
    GenomicRegion encompassing_region() const;
    
    std::vector<Haplotype> extract_haplotypes() const;
    std::vector<Haplotype> extract_haplotypes(const GenomicRegion& region) const;
    
    // Using Haplotype::operator== logic
    void prune_all(const Haplotype& haplotype);
    
    // Using Haplotype::HaveSameAlleles logic
    void prune_unique(const Haplotype& haplotype);
    
    void clear(const GenomicRegion& region);
    
    void clear() noexcept;
    
private:
    using Tree = boost::adjacency_list<
        boost::listS, boost::listS, boost::bidirectionalS, ContigAllele, boost::no_property
    >;
    
    using Vertex = boost::graph_traits<Tree>::vertex_descriptor;
    using Edge   = boost::graph_traits<Tree>::edge_descriptor;
    
    std::reference_wrapper<const ReferenceGenome> reference_;
    
    Tree tree_;
    
    Vertex root_;
    
    std::list<Vertex> haplotype_leafs_;
    
    GenomicRegion::ContigName contig_;
    
    mutable std::unordered_multimap<Haplotype, Vertex> haplotype_leaf_cache_;
    
    using LeafIterator  = decltype(haplotype_leafs_)::const_iterator;
    using CacheIterator = decltype(haplotype_leaf_cache_)::iterator;
    
    bool is_bifurcating(Vertex v) const;
    Vertex remove_forward(Vertex u);
    Vertex remove_backward(Vertex v);
    Vertex get_previous_allele(Vertex allele) const;
    Vertex find_allele_before(Vertex v, const ContigAllele& allele) const;
    bool allele_exists(Vertex leaf, const ContigAllele& allele) const;
    LeafIterator extend_haplotype(LeafIterator leaf, const ContigAllele& new_allele);
    Haplotype extract_haplotype(Vertex leaf, const GenomicRegion& region) const;
    bool define_same_haplotype(Vertex leaf1, Vertex leaf2) const;
    bool is_branch_exact_haplotype(Vertex branch_vertex, const Haplotype& haplotype) const;
    bool is_branch_equal_haplotype(Vertex branch_vertex, const Haplotype& haplotype) const;
    LeafIterator find_exact_haplotype_leaf(LeafIterator first, LeafIterator last,
                                           const Haplotype& haplotype) const;
    LeafIterator find_equal_haplotype_leaf(LeafIterator first, LeafIterator last,
                                           const Haplotype& haplotype) const;
    std::pair<Vertex, bool> clear(Vertex leaf, const ContigRegion& region);
    std::pair<Vertex, bool> clear_external(Vertex leaf, const ContigRegion& region);
    std::pair<Vertex, bool> clear_internal(Vertex leaf, const ContigRegion& region);
};

// non-member methods

namespace detail {
    template <typename InputIt, typename A>
    void extend_tree(InputIt first, InputIt last, HaplotypeTree& tree, A)
    {
        std::for_each(first, last, [&] (const auto& allele) {
            tree.extend(allele);
        });
    }
    
    template <typename InputIt>
    void extend_tree(InputIt first, InputIt last, HaplotypeTree& tree, Variant)
    {
        std::for_each(first, last, [&] (const auto& variant) {
            tree.extend(variant.ref_allele());
            tree.extend(variant.alt_allele());
        });
    }
    
    template <typename InputIt, typename A>
    InputIt extend_tree_until(InputIt first, InputIt last, HaplotypeTree& tree,
                              const unsigned max_haplotypes, A)
    {
        if (first == last) return last;
        const auto it = std::find_if(first, last,
                                     [&] (const auto& allele) {
                                         tree.extend(allele);
                                         return tree.num_haplotypes() >= max_haplotypes;
                                     });
        if (tree.num_haplotypes() == max_haplotypes) {
            return std::next(it);
        } else {
            return it;
        }
    }
    
    template <typename InputIt>
    InputIt extend_tree_until(InputIt first, InputIt last, HaplotypeTree& tree,
                              const unsigned max_haplotypes, Variant)
    {
        if (first == last) return last;
        const auto it = std::find_if(first, last,
                                     [&] (const auto& variant) {
                                         tree.extend(variant.ref_allele());
                                         tree.extend(variant.alt_allele());
                                         return tree.num_haplotypes() >= max_haplotypes;
                                     });
        if (tree.num_haplotypes() == max_haplotypes) {
            return std::next(it);
        } else {
            return it;
        }
    }
    
    template <typename T>
    constexpr bool is_variant_or_allele = std::is_same<T, ContigAllele>::value
                                            || std::is_same<T, Allele>::value
                                            || std::is_same<T, Variant>::value;
} // namespace detail

template <typename InputIt>
void extend_tree(InputIt first, InputIt last, HaplotypeTree& tree)
{
    using MappableType = std::decay_t<typename std::iterator_traits<InputIt>::value_type>;
    
    static_assert(detail::is_variant_or_allele<MappableType>, "not Allele or Variant");
    
    detail::extend_tree(first, last, tree, MappableType {});
}

template <typename Container>
void extend_tree(const Container& elements, HaplotypeTree& tree)
{
    extend_tree(std::cbegin(elements), std::cend(elements), tree);
}

template <typename InputIt>
InputIt extend_tree_until(InputIt first, InputIt last, HaplotypeTree& tree,
                          const unsigned max_haplotypes)
{
    using MappableType = std::decay_t<typename std::iterator_traits<InputIt>::value_type>;
    
    static_assert(detail::is_variant_or_allele<MappableType>, "not Allele or Variant");
    
    return detail::extend_tree_until(first, last, tree, max_haplotypes, MappableType {});
}

template <typename Container>
auto extend_tree_until(const Container& elements, HaplotypeTree& tree, const unsigned max_haplotypes)
{
    return extend_tree_until(std::cbegin(elements), std::cend(elements), tree, max_haplotypes);
}

template <typename Container>
void prune_all(const Container& haplotypes, HaplotypeTree& tree)
{
    for (const auto& haplotype : haplotypes) {
        tree.prune_all(haplotype);
    }
}

template <typename Container>
void prune_unique(const Container& haplotypes, HaplotypeTree& tree)
{
    for (const auto& haplotype : haplotypes) {
        tree.prune_unique(haplotype);
    }
}

template <typename Container>
void splice(const Container& alleles, HaplotypeTree& tree)
{
    for (const auto& allele : alleles) {
        tree.splice(allele);
    }
}

template <typename InputIt>
auto generate_all_haplotypes(InputIt first, InputIt last, const ReferenceGenome& reference)
{
    using MappableType = std::decay_t<typename std::iterator_traits<InputIt>::value_type>;
    
    static_assert(detail::is_variant_or_allele<MappableType>, "not Allele or Variant");
    
    if (first == last) return std::vector<Haplotype> {};
    
    HaplotypeTree tree {contig_name(*first), reference};
    
    extend_tree(first, last, tree);
    
    return tree.extract_haplotypes();
}

template <typename Container>
auto generate_all_haplotypes(const Container& elements, const ReferenceGenome& reference)
{
    return generate_all_haplotypes(std::cbegin(elements), std::cend(elements), reference);
}

} // namespace coretools
} // namespace octopus

#endif /* defined(__Octopus__haplotype_tree__) */