#include "CodeGenerator.h"
#include <sstream>
#include <stdexcept>
#include <cmath>

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
    int frameSize = func->stackSize + 8;  // +8 for saved ra and fp
    frameSize = ((frameSize + 15) / 16) * 16;  // Round up to 16-byte alignment

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
        std::string aReg;
        if (i < 8) {
            aReg = "a" + std::to_string(i);
        } else {
            break;  // Only support up to 8 params for now
        }
        int offset = func->paramOffsets[i];
        std::string offsetStr;
        if (offset >= -2048 && offset < 2048) {
            offsetStr = std::to_string(offset);
        } else {
            // Need lui + addi for large offsets
            emit("lui t0, " + std::to_string((offset >> 12) & 0xFFFFF));
            emit("add t0, t0, fp");
            emit("sw " + aReg + ", 0(t0)");
            continue;
        }
        emit("sw " + aReg + ", " + offsetStr + "(fp)");
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
                emit("la t0, " + as->name);
                emit("sw " + valReg + ", 0(t0)");
            } else {
                int offset = as->varOffset;
                std::string offsetStr;
                if (offset >= -2048 && offset < 2048) {
                    offsetStr = std::to_string(offset);
                    emit("sw " + valReg + ", " + offsetStr + "(fp)");
                } else {
                    emit("lui t0, " + std::to_string((offset >> 12) & 0xFFFFF));
                    emit("add t0, t0, fp");
                    emit("sw " + valReg + ", 0(t0)");
                }
            }
            freeReg(valReg);
            break;
        }
        case StmtKind::DECL: {
            auto* ds = dynamic_cast<const DeclStmt*>(stmt);
            // Evaluate initializer and store
            std::string valReg = generateExpr(ds->decl->init.get());
            if (!ds->decl->isGlobal) {
                int offset = ds->decl->varOffset;
                if (offset >= -2048 && offset < 2048) {
                    emit("sw " + valReg + ", " + std::to_string(offset) + "(fp)");
                } else {
                    emit("lui t0, " + std::to_string((offset >> 12) & 0xFFFFF));
                    emit("add t0, t0, fp");
                    emit("sw " + valReg + ", 0(t0)");
                }
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
            int frameSize = m_currentFunc->stackSize + 8;
            frameSize = ((frameSize + 15) / 16) * 16;
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
            int val = ne->value;
            if (val >= -2048 && val < 2048) {
                emit("li " + reg + ", " + std::to_string(val));
            } else {
                // Load upper 20 bits, then add lower 12
                int upper = (val >> 12) & 0xFFFFF;
                int lower = val & 0xFFF;
                if (lower & 0x800) lower |= ~0xFFF;  // sign-extend
                emit("lui " + reg + ", " + std::to_string(upper));
                if (lower != 0) {
                    emit("addi " + reg + ", " + reg + ", " + std::to_string(lower));
                }
            }
            return reg;
        }
        case ExprKind::ID: {
            auto* ie = dynamic_cast<const IdExpr*>(expr);
            std::string reg = allocReg();

            if (ie->varIsGlobal) {
                emit("la " + reg + ", " + ie->name);
                emit("lw " + reg + ", 0(" + reg + ")");
            } else {
                int offset = ie->varOffset;
                if (offset >= -2048 && offset < 2048) {
                    emit("lw " + reg + ", " + std::to_string(offset) + "(fp)");
                } else {
                    emit("lui " + reg + ", " + std::to_string((offset >> 12) & 0xFFFFF));
                    emit("add " + reg + ", " + reg + ", fp");
                    emit("lw " + reg + ", 0(" + reg + ")");
                }
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

            // Save caller-saved registers that are in use?
            // For simplicity, save and restore t0-t6, a0-a7
            int frameSize = 64;  // Enough to save 8 regs
            emit("addi sp, sp, -" + std::to_string(frameSize));

            // Save used temporary registers
            int saveOffset = 0;
            for (size_t i = 0; i < m_usedRegs.size() && i < 6; i++) {
                emit("sw " + m_usedRegs[i] + ", " + std::to_string(saveOffset) + "(sp)");
                saveOffset += 4;
            }

            // Save argument registers (a0-a7)
            for (int i = 0; i < 8; i++) {
                emit("sw a" + std::to_string(i) + ", " + std::to_string(saveOffset) + "(sp)");
                saveOffset += 4;
            }

            // Evaluate arguments into a0-a7
            for (size_t i = 0; i < ce->args.size() && i < 8; i++) {
                std::string argReg = generateExpr(ce->args[i].get());
                emit("mv a" + std::to_string(i) + ", " + argReg);
                freeReg(argReg);
            }

            // Call the function
            emit("call " + ce->funcName);

            // Result is in a0
            std::string resultReg;
            if (ce->type == TypeKind::VOID) {
                resultReg = allocReg();
                emit("mv " + resultReg + ", a0");  // discard but keep consistent
            } else {
                resultReg = allocReg();
                emit("mv " + resultReg + ", a0");
            }

            // Restore used registers
            int restoreOffset = 0;
            for (size_t i = 0; i < m_usedRegs.size() - 1 && i < 6; i++) {
                emit("lw " + m_usedRegs[i] + ", " + std::to_string(restoreOffset) + "(sp)");
                restoreOffset += 4;
            }
            // Skip the resultReg - restore the rest
            restoreOffset = m_usedRegs.size() > 0 ? 4 * (std::min((int)m_usedRegs.size() - 1, 6)) : 0;

            // Restore a0-a7
            for (int i = 0; i < 8; i++) {
                emit("lw a" + std::to_string(i) + ", " + std::to_string(restoreOffset) + "(sp)");
                restoreOffset += 4;
            }

            emit("addi sp, sp, " + std::to_string(frameSize));

            // Put a0 back (it was overwritten by the restore)
            if (ce->type != TypeKind::VOID) {
                emit("mv a0, " + resultReg);
            }

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
