#pragma once

#include <optional>
#include "ast/AST.h"
#include "SymbolTable.h"

namespace semantic {

// =============================================================================
// ConstEvaluator: compile-time constant expression evaluation
// =============================================================================
class ConstEvaluator {
public:
    // Evaluate an expression to a constant integer value.
    // Returns std::nullopt if the expression cannot be evaluated at compile time.
    static std::optional<int> evaluate(const Expr& expr, const SymbolTable& symTable);

private:
    static std::optional<int> evalBinaryOp(BinaryOp op, int left, int right);
    static std::optional<int> evalUnaryOp(UnaryOp op, int operand);
};

} // namespace semantic