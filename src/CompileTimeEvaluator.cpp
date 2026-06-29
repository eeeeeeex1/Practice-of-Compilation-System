#include "CompileTimeEvaluator.h"

#include <bit>
#include <limits>

CompileTimeEvaluator::CompileTimeEvaluator(std::uint64_t maxSteps)
    : m_maxSteps(maxSteps) {}

std::size_t CompileTimeEvaluator::VectorHash::operator()(
    const std::vector<std::int32_t>& values) const noexcept {
    std::size_t hash = 1469598103934665603ULL;
    for (std::int32_t value : values) {
        hash ^= static_cast<std::uint32_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool CompileTimeEvaluator::tick() {
    if (m_failed || ++m_steps > m_maxSteps) {
        m_failed = true;
        return false;
    }
    return true;
}

void CompileTimeEvaluator::fail() {
    m_failed = true;
}

std::int32_t CompileTimeEvaluator::add32(std::int32_t lhs, std::int32_t rhs) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(lhs) +
                         std::bit_cast<std::uint32_t>(rhs);
    return std::bit_cast<std::int32_t>(bits);
}

std::int32_t CompileTimeEvaluator::sub32(std::int32_t lhs, std::int32_t rhs) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(lhs) -
                         std::bit_cast<std::uint32_t>(rhs);
    return std::bit_cast<std::int32_t>(bits);
}

std::int32_t CompileTimeEvaluator::mul32(std::int32_t lhs, std::int32_t rhs) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(lhs) *
                         std::bit_cast<std::uint32_t>(rhs);
    return std::bit_cast<std::int32_t>(bits);
}

std::int32_t CompileTimeEvaluator::neg32(std::int32_t value) {
    std::uint32_t bits = 0U - std::bit_cast<std::uint32_t>(value);
    return std::bit_cast<std::int32_t>(bits);
}

std::optional<std::size_t> CompileTimeEvaluator::localIndex(int offset,
                                                            const Frame& frame) {
    if (!frame.function || offset > -12 || ((-offset - 12) % 4) != 0) {
        fail();
        return std::nullopt;
    }
    std::size_t index = static_cast<std::size_t>((-offset - 12) / 4);
    if (index >= frame.locals.size()) {
        fail();
        return std::nullopt;
    }
    return index;
}

std::optional<std::int32_t> CompileTimeEvaluator::evalExpr(const Expr* expr,
                                                           Frame& frame) {
    if (!expr || !tick()) return std::nullopt;
    if (expr->isConst) return static_cast<std::int32_t>(expr->constValue);

    switch (expr->kind) {
        case ExprKind::NUMBER:
            return static_cast<std::int32_t>(dynamic_cast<const NumberExpr*>(expr)->value);

        case ExprKind::ID: {
            auto* id = dynamic_cast<const IdExpr*>(expr);
            if (id->varIsGlobal) {
                auto it = m_globals.find(id->name);
                if (it == m_globals.end()) {
                    fail();
                    return std::nullopt;
                }
                return it->second;
            }
            auto index = localIndex(id->varOffset, frame);
            if (!index) return std::nullopt;
            return frame.locals[*index];
        }

        case ExprKind::UNARY: {
            auto* unary = dynamic_cast<const UnaryExpr*>(expr);
            auto operand = evalExpr(unary->operand.get(), frame);
            if (!operand) return std::nullopt;
            switch (unary->op) {
                case TokenType::TOK_ADD: return *operand;
                case TokenType::TOK_SUB: return neg32(*operand);
                case TokenType::TOK_NOT: return *operand == 0 ? 1 : 0;
                default:
                    fail();
                    return std::nullopt;
            }
        }

        case ExprKind::BINARY: {
            auto* binary = dynamic_cast<const BinaryExpr*>(expr);
            auto left = evalExpr(binary->left.get(), frame);
            if (!left) return std::nullopt;

            if (binary->op == TokenType::TOK_AND) {
                if (*left == 0) return 0;
                auto right = evalExpr(binary->right.get(), frame);
                return right ? std::optional<std::int32_t>(*right != 0 ? 1 : 0) : std::nullopt;
            }
            if (binary->op == TokenType::TOK_OR) {
                if (*left != 0) return 1;
                auto right = evalExpr(binary->right.get(), frame);
                return right ? std::optional<std::int32_t>(*right != 0 ? 1 : 0) : std::nullopt;
            }

            auto right = evalExpr(binary->right.get(), frame);
            if (!right) return std::nullopt;
            switch (binary->op) {
                case TokenType::TOK_ADD: return add32(*left, *right);
                case TokenType::TOK_SUB: return sub32(*left, *right);
                case TokenType::TOK_MUL: return mul32(*left, *right);
                case TokenType::TOK_DIV:
                    if (*right == 0 ||
                        (*left == std::numeric_limits<std::int32_t>::min() && *right == -1)) {
                        fail();
                        return std::nullopt;
                    }
                    return static_cast<std::int32_t>(*left / *right);
                case TokenType::TOK_MOD:
                    if (*right == 0 ||
                        (*left == std::numeric_limits<std::int32_t>::min() && *right == -1)) {
                        fail();
                        return std::nullopt;
                    }
                    return static_cast<std::int32_t>(*left % *right);
                case TokenType::TOK_LT: return *left < *right ? 1 : 0;
                case TokenType::TOK_GT: return *left > *right ? 1 : 0;
                case TokenType::TOK_LE: return *left <= *right ? 1 : 0;
                case TokenType::TOK_GE: return *left >= *right ? 1 : 0;
                case TokenType::TOK_EQ: return *left == *right ? 1 : 0;
                case TokenType::TOK_NE: return *left != *right ? 1 : 0;
                default:
                    fail();
                    return std::nullopt;
            }
        }

        case ExprKind::CALL: {
            auto* call = dynamic_cast<const CallExpr*>(expr);
            std::vector<std::int32_t> args;
            args.reserve(call->args.size());
            for (const auto& argExpr : call->args) {
                auto arg = evalExpr(argExpr.get(), frame);
                if (!arg) return std::nullopt;
                args.push_back(*arg);
            }
            auto function = m_functions.find(call->funcName);
            if (function == m_functions.end()) {
                fail();
                return std::nullopt;
            }
            return callFunction(function->second, args);
        }
    }

    fail();
    return std::nullopt;
}

CompileTimeEvaluator::FlowResult CompileTimeEvaluator::execBlock(const Block* block,
                                                                  Frame& frame) {
    if (!block) return {};
    for (const auto& stmt : block->stmts) {
        FlowResult result = execStmt(stmt.get(), frame);
        if (m_failed || result.kind != FlowKind::NORMAL) return result;
    }
    return {};
}

CompileTimeEvaluator::FlowResult CompileTimeEvaluator::execStmt(const Stmt* stmt,
                                                                 Frame& frame) {
    if (!stmt || !tick()) return {};

    switch (stmt->kind) {
        case StmtKind::BLOCK:
            return execBlock(dynamic_cast<const BlockStmt*>(stmt)->block.get(), frame);

        case StmtKind::EMPTY:
            return {};

        case StmtKind::EXPR: {
            auto value = evalExpr(dynamic_cast<const ExprStmt*>(stmt)->expr.get(), frame);
            if (!value) fail();
            return {};
        }

        case StmtKind::ASSIGN: {
            auto* assign = dynamic_cast<const AssignStmt*>(stmt);
            auto value = evalExpr(assign->value.get(), frame);
            if (!value) return {};
            if (assign->varIsGlobal) {
                auto global = m_globals.find(assign->name);
                if (global == m_globals.end()) {
                    fail();
                } else {
                    global->second = *value;
                }
            } else {
                auto index = localIndex(assign->varOffset, frame);
                if (index) frame.locals[*index] = *value;
            }
            return {};
        }

        case StmtKind::DECL: {
            const Decl* decl = dynamic_cast<const DeclStmt*>(stmt)->decl.get();
            auto value = evalExpr(decl->init.get(), frame);
            if (!value) return {};
            auto index = localIndex(decl->varOffset, frame);
            if (index) frame.locals[*index] = *value;
            return {};
        }

        case StmtKind::IF: {
            auto* ifStmt = dynamic_cast<const IfStmt*>(stmt);
            auto condition = evalExpr(ifStmt->condition.get(), frame);
            if (!condition) return {};
            if (*condition != 0) return execStmt(ifStmt->thenStmt.get(), frame);
            if (ifStmt->elseStmt) return execStmt(ifStmt->elseStmt.get(), frame);
            return {};
        }

        case StmtKind::WHILE: {
            auto* whileStmt = dynamic_cast<const WhileStmt*>(stmt);
            while (!m_failed) {
                auto condition = evalExpr(whileStmt->condition.get(), frame);
                if (!condition || *condition == 0) return {};
                FlowResult body = execStmt(whileStmt->body.get(), frame);
                if (m_failed) return {};
                if (body.kind == FlowKind::BREAK) return {};
                if (body.kind == FlowKind::RETURN || body.kind == FlowKind::TAIL_CALL) {
                    return body;
                }
                // NORMAL and CONTINUE both proceed to the next condition check.
            }
            return {};
        }

        case StmtKind::BREAK:
            return {FlowKind::BREAK, 0, {}};

        case StmtKind::CONTINUE:
            return {FlowKind::CONTINUE, 0, {}};

        case StmtKind::RETURN: {
            auto* returnStmt = dynamic_cast<const ReturnStmt*>(stmt);
            if (!returnStmt->hasExpr) return {FlowKind::RETURN, 0, {}};

            if (returnStmt->expr && returnStmt->expr->kind == ExprKind::CALL && frame.function) {
                auto* call = dynamic_cast<const CallExpr*>(returnStmt->expr.get());
                if (call->funcName == frame.function->name) {
                    std::vector<std::int32_t> args;
                    args.reserve(call->args.size());
                    for (const auto& argExpr : call->args) {
                        auto arg = evalExpr(argExpr.get(), frame);
                        if (!arg) return {};
                        args.push_back(*arg);
                    }
                    return {FlowKind::TAIL_CALL, 0, std::move(args)};
                }
            }

            auto value = evalExpr(returnStmt->expr.get(), frame);
            if (!value) return {};
            return {FlowKind::RETURN, *value, {}};
        }
    }

    fail();
    return {};
}

std::optional<std::int32_t> CompileTimeEvaluator::callFunction(
    const FuncDef* function,
    const std::vector<std::int32_t>& args) {
    if (!function || args.size() != function->params.size() || !tick()) {
        fail();
        return std::nullopt;
    }

    bool isPure = m_pureFunctions.contains(function);
    if (isPure) {
        auto functionMemo = m_memoizedResults.find(function);
        if (functionMemo != m_memoizedResults.end()) {
            auto cached = functionMemo->second.find(args);
            if (cached != functionMemo->second.end()) return cached->second;
        }
    }

    if (++m_callDepth > 4096) {
        --m_callDepth;
        fail();
        return std::nullopt;
    }

    const std::vector<std::int32_t> originalArgs = args;
    std::vector<std::int32_t> currentArgs = args;
    std::optional<std::int32_t> returnValue;

    while (!m_failed) {
        Frame frame;
        frame.function = function;
        frame.locals.resize(static_cast<std::size_t>(function->stackSize / 4), 0);
        for (std::size_t i = 0; i < currentArgs.size(); ++i) {
            auto index = localIndex(function->paramOffsets[i], frame);
            if (!index) break;
            frame.locals[*index] = currentArgs[i];
        }
        if (m_failed) break;

        FlowResult flow = execBlock(function->body.get(), frame);
        if (m_failed) break;
        if (flow.kind == FlowKind::TAIL_CALL) {
            if (flow.tailArgs.size() != function->params.size()) {
                fail();
                break;
            }
            currentArgs = std::move(flow.tailArgs);
            continue;
        }
        if (flow.kind == FlowKind::RETURN) {
            returnValue = flow.value;
        } else if (function->returnType == FuncReturnType::VOID) {
            returnValue = 0;
        } else {
            fail();
        }
        break;
    }

    --m_callDepth;
    if (!returnValue || m_failed) return std::nullopt;
    if (isPure) m_memoizedResults[function][originalArgs] = *returnValue;
    return returnValue;
}

void CompileTimeEvaluator::collectFunctionEffects(
    const Expr* expr,
    bool& accessesGlobal,
    std::unordered_set<std::string>& calls) const {
    if (!expr) return;
    switch (expr->kind) {
        case ExprKind::ID:
            accessesGlobal = accessesGlobal || dynamic_cast<const IdExpr*>(expr)->varIsGlobal;
            break;
        case ExprKind::BINARY: {
            auto* binary = dynamic_cast<const BinaryExpr*>(expr);
            collectFunctionEffects(binary->left.get(), accessesGlobal, calls);
            collectFunctionEffects(binary->right.get(), accessesGlobal, calls);
            break;
        }
        case ExprKind::UNARY:
            collectFunctionEffects(dynamic_cast<const UnaryExpr*>(expr)->operand.get(),
                                   accessesGlobal, calls);
            break;
        case ExprKind::CALL: {
            auto* call = dynamic_cast<const CallExpr*>(expr);
            calls.insert(call->funcName);
            for (const auto& arg : call->args) {
                collectFunctionEffects(arg.get(), accessesGlobal, calls);
            }
            break;
        }
        case ExprKind::NUMBER:
            break;
    }
}

void CompileTimeEvaluator::collectFunctionEffects(
    const Block* block,
    bool& accessesGlobal,
    std::unordered_set<std::string>& calls) const {
    if (!block) return;
    for (const auto& stmt : block->stmts) {
        collectFunctionEffects(stmt.get(), accessesGlobal, calls);
    }
}

void CompileTimeEvaluator::collectFunctionEffects(
    const Stmt* stmt,
    bool& accessesGlobal,
    std::unordered_set<std::string>& calls) const {
    if (!stmt) return;
    switch (stmt->kind) {
        case StmtKind::BLOCK:
            collectFunctionEffects(dynamic_cast<const BlockStmt*>(stmt)->block.get(),
                                   accessesGlobal, calls);
            break;
        case StmtKind::EXPR:
            collectFunctionEffects(dynamic_cast<const ExprStmt*>(stmt)->expr.get(),
                                   accessesGlobal, calls);
            break;
        case StmtKind::ASSIGN: {
            auto* assign = dynamic_cast<const AssignStmt*>(stmt);
            accessesGlobal = accessesGlobal || assign->varIsGlobal;
            collectFunctionEffects(assign->value.get(), accessesGlobal, calls);
            break;
        }
        case StmtKind::DECL:
            collectFunctionEffects(dynamic_cast<const DeclStmt*>(stmt)->decl->init.get(),
                                   accessesGlobal, calls);
            break;
        case StmtKind::IF: {
            auto* ifStmt = dynamic_cast<const IfStmt*>(stmt);
            collectFunctionEffects(ifStmt->condition.get(), accessesGlobal, calls);
            collectFunctionEffects(ifStmt->thenStmt.get(), accessesGlobal, calls);
            collectFunctionEffects(ifStmt->elseStmt.get(), accessesGlobal, calls);
            break;
        }
        case StmtKind::WHILE: {
            auto* whileStmt = dynamic_cast<const WhileStmt*>(stmt);
            collectFunctionEffects(whileStmt->condition.get(), accessesGlobal, calls);
            collectFunctionEffects(whileStmt->body.get(), accessesGlobal, calls);
            break;
        }
        case StmtKind::RETURN:
            collectFunctionEffects(dynamic_cast<const ReturnStmt*>(stmt)->expr.get(),
                                   accessesGlobal, calls);
            break;
        default:
            break;
    }
}

void CompileTimeEvaluator::analyzePurity(const CompUnit& comp) {
    struct Effects {
        bool accessesGlobal = false;
        std::unordered_set<std::string> calls;
    };
    std::unordered_map<const FuncDef*, Effects> effects;

    m_pureFunctions.clear();
    for (const auto& function : comp.functions) {
        Effects info;
        collectFunctionEffects(function->body.get(), info.accessesGlobal, info.calls);
        effects.emplace(function.get(), std::move(info));
        if (!effects.at(function.get()).accessesGlobal) {
            m_pureFunctions.insert(function.get());
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<const FuncDef*> noLongerPure;
        for (const FuncDef* function : m_pureFunctions) {
            for (const std::string& calledName : effects.at(function).calls) {
                auto called = m_functions.find(calledName);
                if (called == m_functions.end() || !m_pureFunctions.contains(called->second)) {
                    noLongerPure.push_back(function);
                    break;
                }
            }
        }
        for (const FuncDef* function : noLongerPure) {
            changed = m_pureFunctions.erase(function) > 0 || changed;
        }
    }
}

std::optional<std::int32_t> CompileTimeEvaluator::evaluate(const CompUnit& comp) {
    m_steps = 0;
    m_callDepth = 0;
    m_failed = false;
    m_functions.clear();
    m_globals.clear();
    m_memoizedResults.clear();

    for (const auto& function : comp.functions) {
        m_functions[function->name] = function.get();
    }
    analyzePurity(comp);

    Frame globalFrame;
    for (const auto& global : comp.globals) {
        auto value = evalExpr(global->init.get(), globalFrame);
        if (!value) return std::nullopt;
        m_globals[global->name] = *value;
    }

    auto mainFunction = m_functions.find("main");
    if (mainFunction == m_functions.end()) return std::nullopt;
    return callFunction(mainFunction->second, {});
}
