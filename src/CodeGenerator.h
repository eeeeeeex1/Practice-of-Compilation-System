#ifndef CODE_GENERATOR_H
#define CODE_GENERATOR_H

#include "AST.h"
#include <ostream>
#include <string>
#include <vector>
#include <stack>

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

    // Statement generation
    void generateStmt(const Stmt* stmt);
    void generateBlock(const Block* block);

    // Expression generation: returns the register holding the result
    std::string generateExpr(const Expr* expr);

    // Register allocation
    std::string allocReg();
    void freeReg(const std::string& reg);
    void freeAllRegs();

    // Utility
    bool fitsImm12(int value) const;
    int alignTo(int value, int alignment) const;
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
