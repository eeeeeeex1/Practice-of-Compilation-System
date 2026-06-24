#include "semantic/ConstEvaluator.h"
#include <stdexcept>
#include <cmath>

namespace semantic {

std::optional<int> ConstEvaluator::evaluate(const Expr& expr,
                                             const SymbolTable& symTable) {
    return std::visit(overloaded{
        [&](const NumberExpr& e) -> std::optional<int> {
            return e.value;
        },
        [&](const IdExpr& e) -> std::optional<int> {
            Symbol* sym = symTable.lookup(e.name);
            if (!sym) return std::nullopt;
            if (sym->kind != SymbolKind::Const) return std::nullopt;
            auto* varSym = static_cast<VariableSymbol*>(sym);
            return varSym->constValue;
        },
        [&](const BinaryExpr& e) -> std::optional<int> {
            auto left = evaluate(*e.left, symTable);
            auto right = evaluate(*e.right, symTable);
            if (!left || !right) return std::nullopt;
            return evalBinaryOp(e.op, *left, *right);
        },
        [&](const UnaryExpr& e) -> std::optional<int> {
            auto operand = evaluate(*e.operand, symTable);
            if (!operand) return std::nullopt;
            return evalUnaryOp(e.op, *operand);
        },
        [&](const CallExpr& /*e*/) -> std::optional<int> {
            return std::nullopt;
        }
    }, expr);
}

std::optional<int> ConstEvaluator::evalBinaryOp(BinaryOp op, int left, int right) {
    switch (op) {
        case BinaryOp::Add:  return left + right;
        case BinaryOp::Sub:  return left - right;
        case BinaryOp::Mul:  return left * right;
        case BinaryOp::Div:
            if (right == 0) return std::nullopt;
            return left / right;
        case BinaryOp::Mod:
            if (right == 0) return std::nullopt;
            return left % right;
        case BinaryOp::Lt:   return left < right ? 1 : 0;
        case BinaryOp::Gt:   return left > right ? 1 : 0;
        case BinaryOp::Le:   return left <= right ? 1 : 0;
        case BinaryOp::Ge:   return left >= right ? 1 : 0;
        case BinaryOp::Eq:   return left == right ? 1 : 0;
        case BinaryOp::Ne:   return left != right ? 1 : 0;
        case BinaryOp::And:  return (left && right) ? 1 : 0;
        case BinaryOp::Or:   return (left || right) ? 1 : 0;
        default:             return std::nullopt;
    }
}

std::optional<int> ConstEvaluator::evalUnaryOp(UnaryOp op, int operand) {
    switch (op) {
        case UnaryOp::Pos:  return operand;
        case UnaryOp::Neg:  return -operand;
        case UnaryOp::Not:  return !operand ? 1 : 0;
        default:            return std::nullopt;
    }
}

} // namespace semantic