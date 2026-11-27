// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "regularexpressionpattern.h"
#include <memory>
#include <vector>

// Display-only matching: identify the positive conditions explaining a matching
// line, without changing the search engine or inventing spans for absent text.
class BooleanSearchHighlighter {
public:
    explicit BooleanSearchHighlighter( const RegularExpressionPattern& pattern );
    ~BooleanSearchHighlighter();

    const std::vector<RegularExpressionPattern>& patterns() const;
    std::vector<size_t> matchingPatternIndices( const QString& line ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
