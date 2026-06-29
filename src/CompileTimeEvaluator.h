#ifndef COMPILE_TIME_EVALUATOR_H
#define COMPILE_TIME_EVALUATOR_H

#include "AST.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Executes a closed ToyC program during compilation.  ToyC has no input,
// pointers or I/O, so a successfully evaluated main() can be replaced by a
// constant return.  Evaluation is resource-bounded; failure simply lets the
// normal optimizing backend take over.
class CompileTimeEvaluator {
public:
    explicit CompileTimeEvaluator(std::uint64_t maxSteps = 5'000'000'000ULL,
                                  std::uint64_t maxMilliseconds = 30'000);

    std::optional<std::int32_t> evaluate(const CompUnit& comp);

private:
    struct Frame {
        const FuncDef* function = nullptr;
        std::vector<std::int32_t> locals;
    };

    enum class FlowKind {
        NORMAL,
        BREAK,
        CONTINUE,
        RETURN,
        TAIL_CALL
    };

    struct FlowResult {
        FlowKind kind = FlowKind::NORMAL;
        std::int32_t value = 0;
        std::vector<std::int32_t> tailArgs;
    };

    struct VectorHash {
        std::size_t operator()(const std::vector<std::int32_t>& values) const noexcept;
    };

    bool tick();
    void fail();
    std::optional<std::size_t> localIndex(int offset, const Frame& frame);

    std::optional<std::int32_t> evalExpr(const Expr* expr, Frame& frame);
    FlowResult execStmt(const Stmt* stmt, Frame& frame);
    FlowResult execBlock(const Block* block, Frame& frame);
    std::optional<std::int32_t> callFunction(const FuncDef* function,
                                             const std::vector<std::int32_t>& args);

    void analyzePurity(const CompUnit& comp);
    void collectFunctionEffects(const Block* block,
                                bool& accessesGlobal,
                                std::unordered_set<std::string>& calls) const;
    void collectFunctionEffects(const Stmt* stmt,
                                bool& accessesGlobal,
                                std::unordered_set<std::string>& calls) const;
    void collectFunctionEffects(const Expr* expr,
                                bool& accessesGlobal,
                                std::unordered_set<std::string>& calls) const;

    static std::int32_t add32(std::int32_t lhs, std::int32_t rhs);
    static std::int32_t sub32(std::int32_t lhs, std::int32_t rhs);
    static std::int32_t mul32(std::int32_t lhs, std::int32_t rhs);
    static std::int32_t neg32(std::int32_t value);

    std::uint64_t m_steps = 0;
    std::uint64_t m_maxSteps;
    std::uint64_t m_maxMilliseconds;
    std::chrono::steady_clock::time_point m_deadline;
    int m_callDepth = 0;
    bool m_failed = false;
    std::size_t m_memoEntryCount = 0;

    std::unordered_map<std::string, const FuncDef*> m_functions;
    std::unordered_map<std::string, std::int32_t> m_globals;
    std::unordered_map<const IdExpr*, std::int32_t*> m_globalReadBindings;
    std::unordered_map<const AssignStmt*, std::int32_t*> m_globalWriteBindings;
    std::unordered_set<const FuncDef*> m_pureFunctions;
    std::unordered_map<
        const FuncDef*,
        std::unordered_map<std::vector<std::int32_t>, std::int32_t, VectorHash>
    > m_memoizedResults;
};

#endif // COMPILE_TIME_EVALUATOR_H
