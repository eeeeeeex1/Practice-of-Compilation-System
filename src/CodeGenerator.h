#ifndef CODE_GENERATOR_H
#define CODE_GENERATOR_H

#include "AST.h"
#include <ostream>
#include <string>
#include <vector>
#include <stack>
#include <unordered_map>
#include <unordered_set>

class CodeGenerator {
public:
    explicit CodeGenerator(std::ostream& output, bool optimize = false);

    void generate(const CompUnit& comp);

private:
    // Output helpers
    void emit(const std::string& line);
    void emitLabel(const std::string& label);
    void emitComment(const std::string& comment);

    // Label generation
    std::string newLabel(const std::string& prefix = "L");

    // Global declarations
    void generateGlobal(const Decl* decl);

    // Function code generation
    void generateFuncDef(const FuncDef* func);
    void scanGlobalWrites(const CompUnit& comp);
    void collectUsage(const Block* block, int weight = 1);
    void collectUsage(const Stmt* stmt, int weight = 1);
    void collectUsage(const Expr* expr, int weight = 1);
    void collectLocalConstants(const Block* block);
    void collectLocalConstants(const Stmt* stmt);
    void collectCopyAliases(const Block* block);
    void collectCopyAliases(const Stmt* stmt);
    int localAliasRoot(int offset) const;
    bool blockHasNonTailCall(const Block* block) const;
    bool stmtHasNonTailCall(const Stmt* stmt) const;
    void setupLocalRegs(const FuncDef* func);
    void emitFunctionEpilogue(int frameSize, int raOffset, int fpOffset);
    void emitLoadCachedGlobals();
    void emitFlushCachedGlobals();

    // Statement generation
    void generateStmt(const Stmt* stmt);
    void generateBlock(const Block* block);

    // Expression generation: returns the register holding the result
    std::string generateExpr(const Expr* expr);
    void generateExprInto(const Expr* expr, const std::string& destReg);
    void generateCondBranch(const Expr* expr, const std::string& label, bool branchIfTrue);

    struct RegValue {
        std::string reg;
        bool temporary = false;
    };
    RegValue materializeReadOnly(const Expr* expr);
    void release(const RegValue& value);
    bool exprHasCall(const Expr* expr) const;
    bool exprIsPure(const Expr* expr) const;
    bool getKnownConstant(const Expr* expr, int& value) const;
    bool exprReferencesLocal(const Expr* expr, int offset) const;
    bool exprReferencesGlobal(const Expr* expr, const std::string& name) const;
    int expressionDepth(const Expr* expr) const;
    std::string expressionKey(const Expr* expr) const;

    // Register allocation
    std::string allocReg();
    void freeReg(const std::string& reg);
    void freeAllRegs();

    // Utility
    bool fitsImm12(int value) const;
    int alignTo(int value, int alignment) const;
    void emitAdjustStack(int amount, const std::string& scratchReg = "t0");
    std::string scratchRegAvoiding(const std::string& reg) const;
    void emitLoadStack(const std::string& reg, const std::string& baseReg, int offset);
    void emitStoreStack(const std::string& valueReg, const std::string& baseReg, int offset);
    std::string opToInsn(TokenType op);
    std::string branchOpToInsn(TokenType op, bool invert = false);

    std::ostream& m_out;
    bool m_optimize;
    int m_labelCounter;

    // Register pool
    std::vector<std::string> m_freeRegs;
    std::vector<std::string> m_usedRegs;

    // Current function context
    const FuncDef* m_currentFunc = nullptr;
    std::unordered_map<int, std::string> m_localRegByOffset;
    std::unordered_map<std::string, std::string> m_globalRegByName;
    std::vector<std::string> m_cachedGlobalNames;
    std::vector<std::string> m_savedSRegs;
    std::unordered_map<int, int> m_localReadCounts;
    std::unordered_map<int, int> m_localWriteCounts;
    std::unordered_map<int, int> m_localUseScores;
    std::unordered_map<std::string, int> m_globalUseScores;
    std::unordered_map<int, int> m_knownLocalConstants;
    std::unordered_map<int, int> m_localCopySource;
    std::unordered_map<std::string, int> m_knownGlobalConstants;
    std::unordered_map<std::string, int> m_globalInitialValues;
    std::unordered_set<std::string> m_writtenGlobals;
    std::unordered_set<std::string> m_functionWrittenGlobals;
    std::unordered_map<std::string, std::string> m_cseRegs;
    std::string m_currentBodyLabel;
    std::string m_requestedCallResultReg;
    int m_currentFrameSize = 0;
    int m_currentRaOffset = -1;
    int m_currentFpOffset = -1;
    bool m_saveRa = true;
    bool m_saveFp = true;

    // Loop context for break/continue
    struct LoopLabels {
        std::string startLabel;
        std::string endLabel;
    };
    std::stack<LoopLabels> m_loopStack;

    // Stack offset for saving params
    int m_paramSaveOffset = -12;
};

#endif // CODE_GENERATOR_H
