// SPDX-License-Identifier: GPL-3.0-or-later
#include "booleansearchhighlighter.h"
#include "regularexpression.h"
#include <QHash>

namespace {
enum class Op { Leaf, Not, And, Nand, Or, Nor, Xor, Xnor, Open };
struct Node {
    Op op;
    size_t left = 0, right = 0, pattern = 0;
};
int precedence( Op op )
{
    if ( op == Op::Not )
        return 3;
    if ( op == Op::And || op == Op::Nand )
        return 2;
    return op == Op::Open ? 0 : 1;
}
} // namespace

struct BooleanSearchHighlighter::Impl {
    std::vector<RegularExpressionPattern> patterns;
    std::vector<QRegularExpression> regexes;
    std::vector<Node> nodes;
    bool valid = false;

    bool parse( const QString& expression )
    {
        QHash<QString, size_t> identifiers;
        for ( size_t i = 0; i < patterns.size(); ++i )
            identifiers.insert( QString::fromStdString( patterns[ i ].id() ), i );
        const QHash<QString, Op> operators{ { "not", Op::Not },   { "!", Op::Not },
                                            { "and", Op::And },   { "&", Op::And },
                                            { "nand", Op::Nand }, { "or", Op::Or },
                                            { "|", Op::Or },      { "nor", Op::Nor },
                                            { "xor", Op::Xor },   { "xnor", Op::Xnor } };
        std::vector<Op> pending;
        std::vector<size_t> operands;
        const auto reduce = [ & ]() {
            const auto op = pending.back();
            pending.pop_back();
            if ( op == Op::Open || operands.size() < ( op == Op::Not ? 1u : 2u ) )
                return false;
            const auto right = operands.back();
            operands.pop_back();
            auto left = right;
            if ( op != Op::Not ) {
                left = operands.back();
                operands.pop_back();
            }
            nodes.push_back( { op, left, right, 0 } );
            operands.push_back( nodes.size() - 1 );
            return true;
        };
        // Search validation and quoted-pattern extraction have already run.
        // Use the same precedence as ExprTk: and/nand above or/nor/xor/xnor,
        // with left-associative binary operators and prefix not/!.
        qsizetype position = 0;
        while ( position < expression.size() ) {
            if ( expression[ position ].isSpace() ) {
                ++position;
                continue;
            }
            const auto start = position++;
            if ( expression[ start ].isLetter() ) {
                while ( position < expression.size()
                        && ( expression[ position ].isLetterOrNumber()
                             || expression[ position ] == '_' ) )
                    ++position;
            }
            const auto token = expression.mid( start, position - start );
            if ( identifiers.contains( token ) ) {
                nodes.push_back( { Op::Leaf, 0, 0, identifiers.value( token ) } );
                operands.push_back( nodes.size() - 1 );
            }
            else if ( token == "(" )
                pending.push_back( Op::Open );
            else if ( token == ")" ) {
                while ( !pending.empty() && pending.back() != Op::Open )
                    if ( !reduce() )
                        return false;
                if ( pending.empty() )
                    return false;
                pending.pop_back();
            }
            else if ( operators.contains( token ) ) {
                const auto op = operators.value( token );
                if ( op != Op::Not ) {
                    while ( !pending.empty() && pending.back() != Op::Open
                            && precedence( pending.back() ) >= precedence( op ) )
                        if ( !reduce() )
                            return false;
                }
                pending.push_back( op );
            }
            else
                return false;
        }
        while ( !pending.empty() )
            if ( !reduce() )
                return false;
        return operands.size() == 1 && operands.back() == nodes.size() - 1;
    }
};

BooleanSearchHighlighter::BooleanSearchHighlighter( const RegularExpressionPattern& pattern )
    : impl_( std::make_unique<Impl>() )
{
    if ( !pattern.isBoolean || pattern.isExclude || pattern.pattern.isEmpty() )
        return;
    const RegularExpression expression{ pattern };
    if ( !expression.isValid() )
        return;
    impl_->patterns.assign( expression.subPatterns_.begin(), expression.subPatterns_.end() );
    for ( const auto& term : impl_->patterns )
        impl_->regexes.push_back( static_cast<QRegularExpression>( term ) );
    impl_->valid = impl_->parse( expression.expression_ );
}

BooleanSearchHighlighter::~BooleanSearchHighlighter() = default;

const std::vector<RegularExpressionPattern>& BooleanSearchHighlighter::patterns() const
{
    return impl_->patterns;
}

std::vector<size_t> BooleanSearchHighlighter::matchingPatternIndices( const QString& line ) const
{
    if ( !impl_->valid )
        return {};
    const auto& nodes = impl_->nodes;
    // Nodes are in postorder. Iteration also handles long expressions without
    // recursively walking the tree on the painting stack.
    std::vector<bool> values( nodes.size() );
    for ( size_t i = 0; i < nodes.size(); ++i ) {
        const auto& node = nodes[ i ];
        const bool left = values[ node.left ], right = values[ node.right ];
        switch ( node.op ) {
        case Op::Leaf:
            values[ i ] = impl_->regexes[ node.pattern ].match( line ).hasMatch();
            break;
        case Op::Not:
            values[ i ] = !left;
            break;
        case Op::And:
            values[ i ] = left && right;
            break;
        case Op::Nand:
            values[ i ] = !( left && right );
            break;
        case Op::Or:
            values[ i ] = left || right;
            break;
        case Op::Nor:
            values[ i ] = !( left || right );
            break;
        case Op::Xor:
            values[ i ] = left != right;
            break;
        case Op::Xnor:
            values[ i ] = left == right;
            break;
        case Op::Open:
            return {};
        }
    }
    if ( !values.back() )
        return {};
    std::vector<size_t> result;
    std::vector<size_t> pending{ nodes.size() - 1 };
    while ( !pending.empty() ) {
        const auto index = pending.back();
        pending.pop_back();
        const auto& node = nodes[ index ];
        if ( node.op == Op::Leaf ) {
            if ( values[ index ] )
                result.push_back( node.pattern );
        }
        else if ( node.op == Op::Not )
            pending.push_back( node.left );
        else {
            const bool left = values[ node.left ], right = values[ node.right ];
            bool visitLeft = true, visitRight = true;
            if ( ( node.op == Op::And || node.op == Op::Nand ) && !( left && right ) ) {
                visitLeft = !left;
                visitRight = !right;
            }
            else if ( ( node.op == Op::Or || node.op == Op::Nor ) && ( left || right ) ) {
                visitLeft = left;
                visitRight = right;
            }
            if ( visitRight )
                pending.push_back( node.right );
            if ( visitLeft )
                pending.push_back( node.left );
        }
    }
    return result;
}
