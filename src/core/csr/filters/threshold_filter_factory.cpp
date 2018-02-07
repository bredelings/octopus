// Copyright (c) 2017 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "threshold_filter_factory.hpp"

#include <cassert>
#include <iterator>
#include <algorithm>
#include <unordered_map>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "core/csr/facets/facet_factory.hpp"
#include "core/csr/measures/measures_fwd.hpp"
#include "core/csr/measures/measure_factory.hpp"
#include "config/octopus_vcf.hpp"
#include "exceptions/user_error.hpp"
#include "utils/maths.hpp"

namespace octopus { namespace csr {

class BadVariantFilterCondition : public UserError
{
    std::string do_where() const override { return "ThresholdFilterFactory"; }
    std::string do_why() const override
    {
        return "The variant filter expression entered is not a valid Boolean expression";
    }
    std::string do_help() const override
    {
        return "Enter a valid Boolean expression";
    }
};

template <typename T>
auto make_threshold(const std::string& comparator, const T target)
{
    if (comparator == "==") {
        return make_wrapped_threshold<EqualThreshold<T>>(target);
    }
    if (comparator == "<") {
        return make_wrapped_threshold<LessThreshold<T>>(target);
    }
    if (comparator == "<=") {
        return make_wrapped_threshold<LessEqualThreshold<T>>(target);
    }
    if (comparator == ">") {
        return make_wrapped_threshold<GreaterThreshold<T>>(target);
    }
    if (comparator == ">=") {
        return make_wrapped_threshold<GreaterEqualThreshold<T>>(target);
    }
    throw BadVariantFilterCondition {};
}

using MeasureToFilterKeyMap = std::unordered_map<std::string, std::string>;

void init(MeasureToFilterKeyMap& filter_names)
{
    using namespace octopus::vcf::spec::filter;
    filter_names[name<AlleleFrequency>()]          = alleleBias;
    filter_names[name<Depth>()]                    = lowDepth;
    filter_names[name<MappingQualityDivergence>()] = highMappingQualityDivergence;
    filter_names[name<MappingQualityZeroCount>()]  = highMappingQualityZeroCount;
    filter_names[name<MeanMappingQuality>()]       = lowMappingQuality;
    filter_names[name<ModelPosterior>()]           = lowModelPosterior;
    filter_names[name<Quality>()]                  = lowQuality;
    filter_names[name<QualityByDepth>()]           = lowQualityByDepth;
    filter_names[name<MaxGenotypeQuality>()]       = lowGQ;
    filter_names[name<StrandBias>()]               = strandBias;
    filter_names[name<FilteredReadFraction>()]     = filteredReadFraction;
    filter_names[name<GCContent>()]                = highGCRegion;
    filter_names[name<ClippedReadFraction>()]      = highClippedReadFraction;
}

auto get_vcf_filter_name(const MeasureWrapper& measure, const std::string& comparator, const double threshold_target)
{
    using namespace octopus::vcf::spec::filter;
    // First look for special names
    if (measure.name() == Quality().name()) {
        if (maths::almost_equal(threshold_target, 10.0)) return std::string {q10};
        if (maths::almost_equal(threshold_target, 20.0)) return std::string {q20};
    }
    static MeasureToFilterKeyMap default_filter_names {};
    if (default_filter_names.empty()) {
        init(default_filter_names);
    }
    return default_filter_names[measure.name()];
}

template <typename T>
auto make_condition(const std::string& measure_name, const std::string& comparator, const T threshold_target)
{
    auto measure = make_measure(measure_name);
    if (measure_name == name<StrandBias>()) {
        measure = make_wrapped_measure<StrandBias>(threshold_target);
    }
    auto threshold = make_threshold(comparator, threshold_target);
    auto filter_name = get_vcf_filter_name(measure, comparator, threshold_target);
    return ThresholdVariantCallFilter::Condition {std::move(measure), std::move(threshold), std::move(filter_name)};
}

auto make_condition(const std::string& measure, const std::string& comparator, const std::string& threshold_target)
{
    try {
        return make_condition(measure, comparator, boost::lexical_cast<double>(threshold_target));
    } catch (const boost::bad_lexical_cast&) {
        throw BadVariantFilterCondition {};
    }
}

auto parse_conditions(std::string expression)
{
    expression.erase(std::remove(std::begin(expression), std::end(expression), ' '), std::end(expression));
    std::vector<ThresholdVariantCallFilter::Condition> result {};
    if (expression.empty()) return result;
    std::vector<std::string> conditions {};
    boost::split(conditions, expression, boost::is_any_of("|"));
    for (const auto& condition : conditions) {
        std::vector<std::string> tokens {};
        boost::split(tokens, condition, boost::is_any_of("<,>,<=,=>,=="));
        if (tokens.size() == 2) {
            const auto comparitor_pos = tokens.front().size();
            const auto comparitor_length = condition.size() - comparitor_pos - tokens.back().size();
            assert(comparitor_length == 1 || comparitor_length == 2);
            const auto comparator = condition.substr(comparitor_pos, comparitor_length);
            result.push_back(make_condition(tokens.front(), comparator, tokens.back()));
        } else {
            throw BadVariantFilterCondition {};
        }
    }
    return result;
}

ThresholdFilterFactory::ThresholdFilterFactory(std::string soft_expression)
: ThresholdFilterFactory {{}, std::move(soft_expression)}
{}

ThresholdFilterFactory::ThresholdFilterFactory(std::string hard_expression, std::string soft_expression)
: hard_conditions_ {parse_conditions(std::move(hard_expression))}
, soft_conditions_ {parse_conditions(std::move(soft_expression))}
{}

std::unique_ptr<VariantCallFilterFactory> ThresholdFilterFactory::do_clone() const
{
    return std::make_unique<ThresholdFilterFactory>(*this);
}

std::unique_ptr<VariantCallFilter> ThresholdFilterFactory::do_make(FacetFactory facet_factory,
                                                                   VariantCallFilter::OutputOptions output_config,
                                                                   boost::optional<ProgressMeter&> progress,
                                                                   VariantCallFilter::ConcurrencyPolicy threading) const
{
    return std::make_unique<ThresholdVariantCallFilter>(std::move(facet_factory), hard_conditions_, soft_conditions_,
                                                        output_config, threading, progress);
}

} // namespace csr
} // namespace octopus