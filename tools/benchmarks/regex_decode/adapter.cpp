#include "adapter.h"
#include "hsregularexpression.h"

namespace {
class Adapter final : public BenchmarkMatcher {
  public:
    explicit Adapter( const klogg::vector<RegularExpressionPattern>& patterns ) : matcher_( patterns ) {}
    std::string match( std::string_view input ) const override { return matcher_.match( input ); }

  private:
    DefaultRegularExpressionMatcher matcher_;
};
} // namespace

std::unique_ptr<BenchmarkMatcher> BENCHMARK_FACTORY( const klogg::vector<RegularExpressionPattern>& patterns )
{
    return std::make_unique<Adapter>( patterns );
}
