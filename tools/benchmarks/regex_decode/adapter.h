#pragma once

#include "containers.h"
#include "regularexpressionpattern.h"
#include <memory>
#include <string_view>

// Identical dispatch boundary for both versions; no changes to production APIs.
class BenchmarkMatcher {
  public:
    virtual ~BenchmarkMatcher() = default;
    virtual std::string match( std::string_view input ) const = 0;
};

std::unique_ptr<BenchmarkMatcher> makeBaseline( const klogg::vector<RegularExpressionPattern>& );
std::unique_ptr<BenchmarkMatcher> makeCurrent( const klogg::vector<RegularExpressionPattern>& );
