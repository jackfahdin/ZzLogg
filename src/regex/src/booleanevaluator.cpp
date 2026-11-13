/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "booleanevaluator.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>

class BooleanExpressionEvaluator::Compiler {
public:
    Compiler( BooleanExpressionEvaluator& evaluator, std::string_view expression,
              const klogg::vector<RegularExpressionPattern>& patterns )
        : evaluator_( evaluator )
        , expression_( expression )
    {
        for ( size_t i = 0; i < patterns.size(); ++i ) {
            if ( !variables_.emplace( patterns[ i ].id(), static_cast<uint32_t>( i ) ).second ) {
                fail( "Duplicate sub-pattern identifier", 0 );
            }
        }
    }

    void compile()
    {
        next();
        parseOr();
        if ( token_.kind != Kind::End )
            fail( "Unexpected token", token_.position );
        evaluator_.stack_.resize( maxStack_ );
    }

private:
    enum class Kind {
        End,
        Name,
        Open,
        Close,
        Not,
        And,
        Nand,
        Or,
        Nor,
        Xor,
        Xnor,
        ShortAnd,
        ShortOr
    };
    struct Token {
        Kind kind = Kind::End;
        std::string_view text;
        size_t position = 0;
    };

    [[noreturn]] static void fail( const std::string& message, size_t position )
    {
        throw std::runtime_error( message + " at " + std::to_string( position ) );
    }

    void next()
    {
        while ( cursor_ < expression_.size()
                && std::isspace( static_cast<unsigned char>( expression_[ cursor_ ] ) ) )
            ++cursor_;
        token_ = { Kind::End, {}, cursor_ };
        if ( cursor_ == expression_.size() )
            return;
        const auto start = cursor_++;
        const char ch = expression_[ start ];
        switch ( ch ) {
        case '(':
            token_.kind = Kind::Open;
            return;
        case ')':
            token_.kind = Kind::Close;
            return;
        case '&':
            token_.kind = Kind::ShortAnd;
            return;
        case '|':
            token_.kind = Kind::ShortOr;
            return;
        default:
            break;
        }
        const auto isLetter = []( char c ) {
            return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_';
        };
        if ( !isLetter( ch ) )
            fail( "Unsupported token", start );
        while ( cursor_ < expression_.size() ) {
            const auto c = expression_[ cursor_ ];
            if ( !isLetter( c ) && !( c >= '0' && c <= '9' ) )
                break;
            ++cursor_;
        }
        token_.text = expression_.substr( start, cursor_ - start );
        token_.kind = Kind::Name;
        if ( token_.text == "not" )
            token_.kind = Kind::Not;
        else if ( token_.text == "and" )
            token_.kind = Kind::And;
        else if ( token_.text == "nand" )
            token_.kind = Kind::Nand;
        else if ( token_.text == "or" )
            token_.kind = Kind::Or;
        else if ( token_.text == "nor" )
            token_.kind = Kind::Nor;
        else if ( token_.text == "xor" )
            token_.kind = Kind::Xor;
        else if ( token_.text == "xnor" )
            token_.kind = Kind::Xnor;
    }

    void emitBinary( Operation operation )
    {
        // Fold a plain RHS variable into the operation, avoiding a separate push
        // and interpreter dispatch on every line. Jump targets remain unchanged.
        if ( evaluator_.program_.back().operation == Operation::Variable ) {
            evaluator_.program_.back().operation = operation;
        }
        else {
            evaluator_.program_.push_back( { operation, StackOperand } );
        }
        --stackDepth_;
    }

    // ExprTk groups or/nor/xor/xnor/| at the same, left-associative precedence.
    void parseOr()
    {
        parseAnd();
        while ( token_.kind == Kind::Or || token_.kind == Kind::Nor || token_.kind == Kind::Xor
                || token_.kind == Kind::Xnor || token_.kind == Kind::ShortOr ) {
            const auto kind = token_.kind;
            next();
            const auto jump = evaluator_.program_.size();
            if ( kind == Kind::ShortOr )
                evaluator_.program_.push_back( { Operation::JumpIfTrue, 0 } );
            parseAnd();
            auto operation = Operation::Or;
            if ( kind == Kind::Nor )
                operation = Operation::Nor;
            else if ( kind == Kind::Xor )
                operation = Operation::Xor;
            else if ( kind == Kind::Xnor )
                operation = Operation::Xnor;
            emitBinary( operation );
            if ( kind == Kind::ShortOr )
                evaluator_.program_[ jump ].operand
                    = static_cast<uint32_t>( evaluator_.program_.size() );
        }
    }

    void parseAnd()
    {
        parseProduct();
        while ( token_.kind == Kind::And || token_.kind == Kind::Nand
                || token_.kind == Kind::ShortAnd ) {
            const auto kind = token_.kind;
            next();
            const auto jump = evaluator_.program_.size();
            if ( kind == Kind::ShortAnd )
                evaluator_.program_.push_back( { Operation::JumpIfFalse, 0 } );
            parseProduct();
            emitBinary( kind == Kind::Nand ? Operation::Nand : Operation::And );
            if ( kind == Kind::ShortAnd )
                evaluator_.program_[ jump ].operand
                    = static_cast<uint32_t>( evaluator_.program_.size() );
        }
    }

    void parseProduct()
    {
        parsePrimary();
        // The existing front end also accepts ExprTk's implicit multiplication:
        // p_0 p_1, p_0(p_1), (p_0)(p_1). For 0/1 inputs this is logical AND.
        while ( token_.kind == Kind::Name || token_.kind == Kind::Open ) {
            parsePrimary();
            emitBinary( Operation::And );
        }
    }

    void parsePrimary()
    {
        if ( token_.kind == Kind::Name ) {
            const auto found = variables_.find( std::string( token_.text ) );
            if ( found == variables_.end() )
                fail( "Unknown sub-pattern", token_.position );
            evaluator_.program_.push_back( { Operation::Variable, found->second } );
            maxStack_ = std::max( maxStack_, ++stackDepth_ );
            next();
            return;
        }
        const bool negate = token_.kind == Kind::Not;
        if ( negate )
            next();
        if ( token_.kind != Kind::Open ) {
            fail( negate ? "Expected '(' after not" : "Expected sub-pattern or '('",
                  token_.position );
        }
        if ( nesting_ == MaxNestingDepth )
            fail( "Nesting limit is 256", token_.position );
        ++nesting_;
        next();
        parseOr();
        if ( token_.kind != Kind::Close )
            fail( "Expected ')'", token_.position );
        --nesting_;
        next();
        if ( negate )
            evaluator_.program_.push_back( { Operation::Not, 0 } );
    }

    BooleanExpressionEvaluator& evaluator_;
    std::string_view expression_;
    std::unordered_map<std::string, uint32_t> variables_;
    size_t cursor_ = 0;
    size_t nesting_ = 0;
    size_t stackDepth_ = 0;
    size_t maxStack_ = 0;
    Token token_;
};

BooleanExpressionEvaluator::BooleanExpressionEvaluator(
    const std::string& expression, const klogg::vector<RegularExpressionPattern>& patterns )
    : variableCount_( patterns.size() )
{
    if ( expression.size() > MaxExpressionLength ) {
        errorString_ = "Expression length limit is 65536 bytes at 65536";
        return;
    }
    if ( variableCount_ > MaxPatterns ) {
        errorString_ = "Sub-pattern limit is 4096 at 0";
        return;
    }
    try {
        Compiler( *this, expression, patterns ).compile();
        isValid_ = true;
    } catch ( const std::runtime_error& error ) {
        errorString_ = error.what();
        return;
    }
    if ( variableCount_ <= 4 ) {
        std::array<char, 4> variables{};
        for ( unsigned combination = 0; combination < ( 1U << variableCount_ ); ++combination ) {
            for ( size_t i = 0; i < variableCount_; ++i )
                variables[ i ] = static_cast<char>( ( combination >> i ) & 1U );
            precomputedResults_[ combination ]
                = execute( std::string_view( variables.data(), variableCount_ ) );
        }
    }
}

bool BooleanExpressionEvaluator::evaluate( std::string_view variables )
{
    if ( !isValid_ || variables.size() != variableCount_ )
        return false;
    if ( variableCount_ <= 4 ) {
        unsigned combination = 0;
        for ( size_t i = 0; i < variables.size(); ++i ) {
            if ( variables[ i ] )
                combination |= 1U << i;
        }
        return precomputedResults_[ combination ];
    }
    return execute( variables );
}

bool BooleanExpressionEvaluator::execute( std::string_view variables )
{
    size_t top = 0;
    for ( size_t pc = 0; pc < program_.size(); ++pc ) {
        const auto instruction = program_[ pc ];
        switch ( instruction.operation ) {
        case Operation::Variable:
            stack_[ top++ ] = static_cast<uint8_t>( variables[ instruction.operand ] != 0 );
            break;
        case Operation::Not:
            stack_[ top - 1 ] ^= 1;
            break;
        case Operation::JumpIfFalse:
        case Operation::JumpIfTrue:
            if ( ( stack_[ top - 1 ] != 0 )
                 == ( instruction.operation == Operation::JumpIfTrue ) ) {
                pc = instruction.operand - 1;
            }
            break;
        default: {
            const auto right = instruction.operand == StackOperand
                                   ? stack_[ --top ]
                                   : static_cast<uint8_t>( variables[ instruction.operand ] != 0 );
            auto& left = stack_[ top - 1 ];
            switch ( instruction.operation ) {
            case Operation::And:
                left = static_cast<uint8_t>( left && right );
                break;
            case Operation::Nand:
                left = static_cast<uint8_t>( !( left && right ) );
                break;
            case Operation::Or:
                left = static_cast<uint8_t>( left || right );
                break;
            case Operation::Nor:
                left = static_cast<uint8_t>( !( left || right ) );
                break;
            case Operation::Xor:
                left = static_cast<uint8_t>( left != right );
                break;
            case Operation::Xnor:
                left = static_cast<uint8_t>( left == right );
                break;
            default:
                break;
            }
            break;
        }
        }
    }
    return stack_[ 0 ] != 0;
}
