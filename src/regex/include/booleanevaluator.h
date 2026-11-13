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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "containers.h"
#include "regularexpressionpattern.h"

// Each PatternMatcher owns an evaluator; the reusable execution stack is not shared.
class BooleanExpressionEvaluator {
public:
    static constexpr size_t MaxExpressionLength = 65536;
    static constexpr size_t MaxPatterns = 4096;
    static constexpr size_t MaxNestingDepth = 256;

    BooleanExpressionEvaluator( const std::string& expression,
                                const klogg::vector<RegularExpressionPattern>& patterns );

    bool isValid() const
    {
        return isValid_;
    }
    std::string errorString() const
    {
        return errorString_;
    }
    bool evaluate( std::string_view variables );

private:
    enum class Operation : uint8_t {
        Variable,
        Not,
        And,
        Nand,
        Or,
        Nor,
        Xor,
        Xnor,
        JumpIfFalse,
        JumpIfTrue
    };
    static constexpr uint32_t StackOperand = std::numeric_limits<uint32_t>::max();
    struct Instruction {
        Operation operation;
        uint32_t operand = 0;
    };
    class Compiler;
    bool execute( std::string_view variables );

    bool isValid_ = false;
    std::string errorString_;
    size_t variableCount_ = 0;
    klogg::vector<Instruction> program_;
    klogg::vector<uint8_t> stack_;
    std::array<bool, 16> precomputedResults_{};
};
