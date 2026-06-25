#include "CodeGenerator.h"
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <algorithm>

CodeGenerator::CodeGenerator(std::ostream& output, bool optimize)
    : m_out(output), m_optimize(optimize), m_labelCounter(0) {
    // Initialize register pool
    m_freeRegs = {"t0", "t1", "t2", "t3", "t4", "t5", "t6"};
}

void CodeGenerator::emit(const std::string& line) {
    m_out << "    " << line << "\n";
}

void CodeGenerator::emitLabel(const std::string& label) {
    m_out << label << ":\n";
}

void CodeGenerator::emitComment(const std::string& comment) {
    m_out << "    # " << comment << "\n";
}

std::string CodeGenerator::newLabel(const std::string& prefix) {
    return prefix + std::to_string(m_labelCounter++);
}

bool CodeGenerator::fitsImm12(int value) const {
    return value >= -2048 && value < 2048;
}

int CodeGenerator::alignTo(int value, int alignment) const {
    return ((value + alignment - 1) / alignment) * alignment;
}

std::string CodeGenerator::scratchRegAvoiding(const std::string& reg) const {
    return reg == "t0" ? "t1" : "t0";
}

void CodeGenerator::emitLoadStack(const std::string& reg,
                                  const std::string& baseReg,
                                  int offset) {
    if (fitsImm12(offset)) {
        emit("lw " + reg + ", " + std::to_string(offset) + "(" + baseReg + ")");
        return;
    }
    emit("li " + reg + ", " + std::to_string(offset));
    emit("add " + reg + ", " + baseReg + ", " + reg);
    emit("lw " + reg + ", 0(" + reg + ")");
}

void CodeGenerator::emitStoreStack(const std::string& valueReg,
                                   const std::string& baseReg,
                                   int offset) {
    if (fitsImm12(offset)) {
        emit("sw " + valueReg + ", " + std::to_string(offset) + "(" + baseReg + ")");
        return;
    }
    std::string scratch = scratchRegAvoiding(valueReg);
    emit("li " + scratch + ", " + std::to_string(offset));
    emit("add " + scratch + ", " + baseReg + ", " + scratch);
    emit("sw " + valueReg + ", 0(" + scratch + ")");
}

std::string CodeGenerator::allocReg() {
    if (m_freeRegs.empty()) {
        throw std::runtime_error("Register allocation exhausted");
    }
    std::string reg = m_freeRegs.back();
    m_freeRegs.pop_back();
    m_usedRegs.push_back(reg);
    return reg;
}

void CodeGenerator::freeReg(const std::string& reg) {
    // Find and move from used to free
    for (auto it = m_usedRegs.begin(); it != m_usedRegs.end(); ++it) {
        if (*it == reg) {
            m_usedRegs.erase(it);
            m_freeRegs.push_back(reg);
            return;
        }
    }
}

void CodeGenerator::freeAllRegs() {
    for (auto& reg : m_usedRegs) {
        m_freeRegs.push_back(reg);
    }
    m_usedRegs.clear();
}

// === Top-level generation ===

void CodeGenerator::generate(const CompUnit& comp) {
    emitComment("ToyC Compiler - RISC-V 32 Assembly Output");

    // Data section for global variables
    emit(".data");
    for (auto& decl : comp.globals) {
        generateGlobal(decl.get());
    }

    // Text section
    emit(".text");

    // Entry point
    emit(".globl _start");
    emitLabel("_start");
    emit("call main");
    emit("li a7, 93");    // exit syscall
    emit("ecall");

    // Generate function code
    for (auto& func : comp.functions) {
        generateFuncDef(func.get());
    }
}

// === Global declarations ===

void CodeGenerator::generateGlobal(const Decl* decl) {
    emit(".globl " + decl->name);
    emit(".balign 4");
    emitLabel(decl->name);
    int initVal = 0;
    if (decl->init && decl->init->isConst) {
        initVal = decl->init->constValue;
    }
    emit(".word " + std::to_string(initVal));
}

// === Function definition ===

void CodeGenerator::generateFuncDef(const FuncDef* func) {
    m_currentFunc = func;
    m_paramSaveOffset = -12;

    // Compute frame size (rounded to 16 bytes)
    int frameSize = alignTo(func->stackSize + 8, 16);  // +8 for saved ra and fp

    int raOffset = frameSize - 4;
    int fpOffset = frameSize - 8;

    // Function label
    emit("");
    emit(".globl " + func->name);
    emitLabel(func->name);

    // Prologue
    emitComment("Prologue");
    emit("addi sp, sp, -" + std::to_string(frameSize));
    emit("sw ra, " + std::to_string(raOffset) + "(sp)");
    emit("sw fp, " + std::to_string(fpOffset) + "(sp)");
    emit("addi fp, sp, " + std::to_string(frameSize));

    // Save parameters to stack
    emitComment("Save parameters");
    for (size_t i = 0; i < func->params.size(); i++) {
        int offset = func->paramOffsets[i];
        if (i < 8) {
            std::string aReg = "a" + std::to_string(i);
            emitStoreStack(aReg, "fp", offset);
        } else {
            int incomingOffset = static_cast<int>((i - 8) * 4);
            emitLoadStack("t0", "fp", incomingOffset);
            emitStoreStack("t0", "fp", offset);
        }
    }

    // Generate function body
    if (func->body) {
        for (auto& stmt : func->body->stmts) {
            generateStmt(stmt.get());
        }
    }

    // Epilogue (for void functions that fall through or as default return)
    // If we reach here, it's a void function or implicit return 0
    emitLabel(newLabel("func_end"));
    emitComment("Epilogue");
    emit("lw ra, " + std::to_string(raOffset) + "(sp)");
    emit("lw fp, " + std::to_string(fpOffset) + "(sp)");
    emit("addi sp, sp, " + std::to_string(frameSize));
    emit("jr ra");

    m_currentFunc = nullptr;
}

// === Statement generation ===

void CodeGenerator::generateStmt(const Stmt* stmt) {
    switch (stmt->kind) {
        case StmtKind::BLOCK: {
            auto* bs = dynamic_cast<const BlockStmt*>(stmt);
            generateBlock(bs->block.get());
            break;
        }
        case StmtKind::EMPTY:
            break;

        case StmtKind::EXPR: {
            auto* es = dynamic_cast<const ExprStmt*>(stmt);
            std::string reg = generateExpr(es->expr.get());
            freeReg(reg);
            break;
        }
        case StmtKind::ASSIGN: {
            auto* as = dynamic_cast<const AssignStmt*>(stmt);
            std::string valReg = generateExpr(as->value.get());

            // Store to variable
            if (as->varIsGlobal) {
                std::string scratch = scratchRegAvoiding(valReg);
                emit("la " + scratch + ", " + as->name);
                emit("sw " + valReg + ", 0(" + scratch + ")");
            } else {
                emitStoreStack(valReg, "fp", as->varOffset);
            }
            freeReg(valReg);
            break;
        }
        case StmtKind::DECL: {
            auto* ds = dynamic_cast<const DeclStmt*>(stmt);
            // Evaluate initializer and store
            std::string valReg = generateExpr(ds->decl->init.get());
            if (!ds->decl->isGlobal) {
                emitStoreStack(valReg, "fp", ds->decl->varOffset);
            }
            freeReg(valReg);
            break;
        }
        case StmtKind::IF: {
            auto* is = dynamic_cast<const IfStmt*>(stmt);
            std::string condReg = generateExpr(is->condition.get());

            std::string elseLabel = newLabel("else");
            std::string endLabel = newLabel("endif");

            emit("beqz " + condReg + ", " + elseLabel);
            freeReg(condReg);

            generateStmt(is->thenStmt.get());

            if (is->elseStmt) {
                emit("j " + endLabel);
            }

            emitLabel(elseLabel);

            if (is->elseStmt) {
                generateStmt(is->elseStmt.get());
                emitLabel(endLabel);
            }
            break;
        }
        case StmtKind::WHILE: {
            auto* ws = dynamic_cast<const WhileStmt*>(stmt);

            std::string startLabel = newLabel("while_start");
            std::string endLabel = newLabel("while_end");

            m_loopStack.push({startLabel, endLabel});

            emitLabel(startLabel);

            std::string condReg = generateExpr(ws->condition.get());
            emit("beqz " + condReg + ", " + endLabel);
            freeReg(condReg);

            generateStmt(ws->body.get());
            emit("j " + startLabel);

            emitLabel(endLabel);

            m_loopStack.pop();
            break;
        }
        case StmtKind::BREAK: {
            if (!m_loopStack.empty()) {
                emit("j " + m_loopStack.top().endLabel);
            }
            break;
        }
        case StmtKind::CONTINUE: {
            if (!m_loopStack.empty()) {
                emit("j " + m_loopStack.top().startLabel);
            }
            break;
        }
        case StmtKind::RETURN: {
            auto* rs = dynamic_cast<const ReturnStmt*>(stmt);
            if (rs->hasExpr) {
                std::string retReg = generateExpr(rs->expr.get());
                // Move result to a0
                if (retReg != "a0") {
                    emit("mv a0, " + retReg);
                }
                freeReg(retReg);
            } else {
                emit("mv a0, zero");
            }

            // Epilogue and return
            int frameSize = alignTo(m_currentFunc->stackSize + 8, 16);
            int raOffset = frameSize - 4;
            int fpOffset = frameSize - 8;

            emit("lw ra, " + std::to_string(raOffset) + "(sp)");
            emit("lw fp, " + std::to_string(fpOffset) + "(sp)");
            emit("addi sp, sp, " + std::to_string(frameSize));
            emit("jr ra");
            break;
        }
    }
}

void CodeGenerator::generateBlock(const Block* block) {
    for (auto& stmt : block->stmts) {
        generateStmt(stmt.get());
    }
}

// === Expression generation ===

std::string CodeGenerator::generateExpr(const Expr* expr) {
    if (!expr) {
        std::string reg = allocReg();
        emit("mv " + reg + ", zero");
        return reg;
    }

    switch (expr->kind) {
        case ExprKind::NUMBER: {
            auto* ne = dynamic_cast<const NumberExpr*>(expr);
            std::string reg = allocReg();
            emit("li " + reg + ", " + std::to_string(ne->value));
            return reg;
        }
        case ExprKind::ID: {
            auto* ie = dynamic_cast<const IdExpr*>(expr);
            std::string reg = allocReg();

            if (ie->varIsGlobal) {
                emit("la " + reg + ", " + ie->name);
                emit("lw " + reg + ", 0(" + reg + ")");
            } else {
                emitLoadStack(reg, "fp", ie->varOffset);
            }
            return reg;
        }
        case ExprKind::BINARY: {
            auto* be = dynamic_cast<const BinaryExpr*>(expr);

            // Short-circuit evaluation for && and ||
            if (be->op == TokenType::TOK_AND) {
                // left && right:
                // Evaluate left, if false (zero), result is 0 (false), skip right
                // If left is non-zero, evaluate right (result is 0 or 1)
                std::string resultReg = allocReg();
                std::string shortLabel = newLabel("sc_and");

                std::string leftReg = generateExpr(be->left.get());
                emit("beqz " + leftReg + ", " + shortLabel);
                freeReg(leftReg);

                std::string rightReg = generateExpr(be->right.get());
                emit("snez " + resultReg + ", " + rightReg);
                freeReg(rightReg);

                std::string endLabel = newLabel("sc_and_end");
                emit("j " + endLabel);

                emitLabel(shortLabel);
                emit("mv " + resultReg + ", zero");

                emitLabel(endLabel);
                return resultReg;
            }

            if (be->op == TokenType::TOK_OR) {
                // left || right:
                // Evaluate left, if true (non-zero), result is 1, skip right
                // If left is zero, evaluate right (result is 0 or 1)
                std::string resultReg = allocReg();
                std::string shortLabel = newLabel("sc_or");

                std::string leftReg = generateExpr(be->left.get());
                emit("bnez " + leftReg + ", " + shortLabel);
                freeReg(leftReg);

                std::string rightReg = generateExpr(be->right.get());
                emit("snez " + resultReg + ", " + rightReg);
                freeReg(rightReg);

                std::string endLabel = newLabel("sc_or_end");
                emit("j " + endLabel);

                emitLabel(shortLabel);
                emit("li " + resultReg + ", 1");

                emitLabel(endLabel);
                return resultReg;
            }

            // Normal binary operations
            std::string leftReg = generateExpr(be->left.get());
            std::string rightReg = generateExpr(be->right.get());
            std::string resultReg = leftReg;  // result goes into left reg

            switch (be->op) {
                case TokenType::TOK_ADD:
                    emit("add " + resultReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_SUB:
                    emit("sub " + resultReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_MUL:
                    emit("mul " + resultReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_DIV:
                    emit("div " + resultReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_MOD:
                    emit("rem " + resultReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_LT:
                    emit("slt " + resultReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_GT:
                    emit("slt " + resultReg + ", " + rightReg + ", " + leftReg);
                    break;
                case TokenType::TOK_LE: {
                    // left <= right === !(right < left)
                    emit("slt " + resultReg + ", " + rightReg + ", " + leftReg);
                    emit("xori " + resultReg + ", " + resultReg + ", 1");
                    break;
                }
                case TokenType::TOK_GE: {
                    // left >= right === !(left < right)
                    emit("slt " + resultReg + ", " + leftReg + ", " + rightReg);
                    emit("xori " + resultReg + ", " + resultReg + ", 1");
                    break;
                }
                case TokenType::TOK_EQ: {
                    // left == right
                    emit("sub " + resultReg + ", " + leftReg + ", " + rightReg);
                    emit("seqz " + resultReg + ", " + resultReg);
                    break;
                }
                case TokenType::TOK_NE: {
                    // left != right
                    emit("sub " + resultReg + ", " + leftReg + ", " + rightReg);
                    emit("snez " + resultReg + ", " + resultReg);
                    break;
                }
                default:
                    break;
            }

            freeReg(rightReg);
            return resultReg;
        }
        case ExprKind::UNARY: {
            auto* ue = dynamic_cast<const UnaryExpr*>(expr);
            std::string opReg = generateExpr(ue->operand.get());

            switch (ue->op) {
                case TokenType::TOK_SUB:
                    emit("sub " + opReg + ", zero, " + opReg);
                    break;
                case TokenType::TOK_ADD:
                    // Unary plus is a no-op
                    break;
                case TokenType::TOK_NOT:
                    emit("seqz " + opReg + ", " + opReg);
                    break;
                default:
                    break;
            }
            return opReg;
        }
        case ExprKind::CALL: {
            auto* ce = dynamic_cast<const CallExpr*>(expr);

            std::vector<std::string> regsToSave = m_usedRegs;
            int outgoingBytes = ce->args.size() > 8
                ? static_cast<int>((ce->args.size() - 8) * 4)
                : 0;
            int tempSaveBase = outgoingBytes;
            int argSaveBase = tempSaveBase + static_cast<int>(regsToSave.size() * 4);
            int argSaveBytes = static_cast<int>(std::min<size_t>(ce->args.size(), 8) * 4);
            int frameSize = alignTo(outgoingBytes +
                                    static_cast<int>(regsToSave.size() * 4) +
                                    argSaveBytes, 16);
            emit("addi sp, sp, -" + std::to_string(frameSize));

            // Save used temporary registers
            for (size_t i = 0; i < regsToSave.size(); i++) {
                emitStoreStack(regsToSave[i], "sp",
                               tempSaveBase + static_cast<int>(i * 4));
            }

            // Evaluate arguments into stack slots first so nested calls cannot
            // clobber argument registers that were already prepared.
            for (size_t i = 0; i < ce->args.size(); i++) {
                std::string argReg = generateExpr(ce->args[i].get());
                int argOffset = i < 8
                    ? argSaveBase + static_cast<int>(i * 4)
                    : static_cast<int>((i - 8) * 4);
                emitStoreStack(argReg, "sp", argOffset);
                freeReg(argReg);
            }

            for (size_t i = 0; i < ce->args.size() && i < 8; i++) {
                emitLoadStack("a" + std::to_string(i), "sp",
                              argSaveBase + static_cast<int>(i * 4));
            }

            // Call the function
            emit("call " + ce->funcName);

            // Result is in a0
            std::string resultReg = allocReg();
            emit("mv " + resultReg + ", a0");

            // Restore used registers
            for (size_t i = 0; i < regsToSave.size(); i++) {
                emitLoadStack(regsToSave[i], "sp",
                              tempSaveBase + static_cast<int>(i * 4));
            }

            emit("addi sp, sp, " + std::to_string(frameSize));

            return resultReg;
        }
    }

    // Fallback
    std::string reg = allocReg();
    emit("mv " + reg + ", zero");
    return reg;
}

std::string CodeGenerator::opToInsn(TokenType op) {
    switch (op) {
        case TokenType::TOK_ADD: return "add";
        case TokenType::TOK_SUB: return "sub";
        case TokenType::TOK_MUL: return "mul";
        case TokenType::TOK_DIV: return "div";
        case TokenType::TOK_MOD: return "rem";
        default: return "???";
    }
}

std::string CodeGenerator::branchOpToInsn(TokenType op, bool invert) {
    switch (op) {
        case TokenType::TOK_LT:  return invert ? "bge"  : "blt";
        case TokenType::TOK_GT:  return invert ? "ble"  : "bgt";
        case TokenType::TOK_LE:  return invert ? "bgt"  : "ble";
        case TokenType::TOK_GE:  return invert ? "blt"  : "bge";
        case TokenType::TOK_EQ:  return invert ? "bne"  : "beq";
        case TokenType::TOK_NE:  return invert ? "beq"  : "bne";
        default: return "beq";
    }
}
