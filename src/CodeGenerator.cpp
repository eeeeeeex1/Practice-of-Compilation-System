#include "CodeGenerator.h"
#include <sstream>
#include <stdexcept>
#include <cmath>

CodeGenerator::CodeGenerator(std::ostream& output, bool optimize)
    : m_out(output), m_optimize(optimize), m_labelCounter(0) {
    // Initialize register pool (t0-t6 are caller-saved temporaries)
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
        // Spill to stack if no registers available
        throw std::runtime_error("Register allocation exhausted");
    }
    std::string reg = m_freeRegs.back();
    m_freeRegs.pop_back();
    m_usedRegs.push_back(reg);
    return reg;
}

void CodeGenerator::freeReg(const std::string& reg) {
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

    // Data section for global variables and constants
    if (!comp.globals.empty()) {
        emit(".data");
        for (auto& decl : comp.globals) {
            generateGlobal(decl.get());
        }
    }

    // Text section
    emit(".text");

    // Entry point - call main and exit
    emit(".globl _start");
    emitLabel("_start");
    emit("call main");
    emit("mv a0, a0");       // result already in a0
    emit("li a7, 93");       // exit syscall number
    emit("ecall");           // invoke syscall

    // Generate all functions
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
    freeAllRegs();

    // Compute frame size
    // Layout: [saved regs] [local vars] [saved fp] [saved ra]
    // fp points to saved fp location
    // sp = fp - frameSize
    
    int localSize = func->stackSize;  // space for locals and params
    
    // Minimum frame: ra (4 bytes) + fp (4 bytes) = 8 bytes
    int minFrame = 8 + localSize;
    
    // Round up to 16-byte alignment (RISC-V ABI requirement)
    int frameSize = ((minFrame + 15) / 16) * 16;
    
    // Offsets within frame
    int raOffset = frameSize - 4;   // ra saved at top of frame
    int fpOffset = frameSize - 8;   // fp saved just below ra

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

    // Save parameters passed in a0-a7 to stack
    emitComment("Save parameters");
    for (size_t i = 0; i < func->params.size() && i < 8; i++) {
        int offset = func->paramOffsets[i];
        std::string aReg = "a" + std::to_string(i);
        emit("sw " + aReg + ", " + std::to_string(offset) + "(fp)");
    }

    // Generate function body
    if (func->body) {
        for (auto& stmt : func->body->stmts) {
            generateStmt(stmt.get());
        }
    }

    // Epilogue label (for void functions or fall-through)
    std::string endLabel = func->name + "_end";
    emitLabel(endLabel);
    emitComment("Epilogue");
    emit("lw ra, " + std::to_string(raOffset) + "(sp)");
    emit("lw fp, " + std::to_string(fpOffset) + "(sp)");
    emit("addi sp, sp, " + std::to_string(frameSize));
    emit("jr ra");

    m_currentFunc = nullptr;
}

// === Statement generation ===

void CodeGenerator::generateStmt(const Stmt* stmt) {
    if (!stmt) return;
    
    switch (stmt->kind) {
        case StmtKind::BLOCK: {
            auto* bs = dynamic_cast<const BlockStmt*>(stmt);
            generateBlock(bs->block.get());
            break;
        }
        case StmtKind::EMPTY:
            // Empty statement - nothing to generate
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

            if (as->varIsGlobal) {
                // Global variable: use la + sw
                emit("la t0, " + as->name);
                emit("sw " + valReg + ", 0(t0)");
            } else {
                // Local variable: store at offset from fp
                int offset = as->varOffset;
                emit("sw " + valReg + ", " + std::to_string(offset) + "(fp)");
            }
            freeReg(valReg);
            break;
        }
        case StmtKind::DECL: {
            auto* ds = dynamic_cast<const DeclStmt*>(stmt);
            // Evaluate initializer
            std::string valReg = generateExpr(ds->decl->init.get());
            
            if (!ds->decl->isGlobal) {
                // Local declaration: store at assigned offset
                int offset = ds->decl->varOffset;
                emit("sw " + valReg + ", " + std::to_string(offset) + "(fp)");
            }
            freeReg(valReg);
            break;
        }
        case StmtKind::IF: {
            auto* is = dynamic_cast<const IfStmt*>(stmt);
            
            std::string elseLabel = newLabel("else");
            std::string endLabel = newLabel("endif");

            // Evaluate condition
            std::string condReg = generateExpr(is->condition.get());
            emit("beqz " + condReg + ", " + elseLabel);
            freeReg(condReg);

            // Then branch
            generateStmt(is->thenStmt.get());
            
            if (is->elseStmt) {
                emit("j " + endLabel);
            }

            // Else branch
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

            // Push loop labels for break/continue
            m_loopStack.push({startLabel, endLabel});

            // Loop start: check condition
            emitLabel(startLabel);
            std::string condReg = generateExpr(ws->condition.get());
            emit("beqz " + condReg + ", " + endLabel);
            freeReg(condReg);

            // Loop body
            generateStmt(ws->body.get());
            emit("j " + startLabel);

            // Loop end
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
                // Evaluate return value and move to a0
                std::string retReg = generateExpr(rs->expr.get());
                if (retReg != "a0") {
                    emit("mv a0, " + retReg);
                }
                freeReg(retReg);
            } else {
                // Void return: set a0 = 0
                emit("mv a0, zero");
            }

            // Jump to function epilogue
            emit("j " + m_currentFunc->name + "_end");
            break;
        }
    }
}

void CodeGenerator::generateBlock(const Block* block) {
    if (!block) return;
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
            
            if (val == 0) {
                emit("mv " + reg + ", zero");
            } else if (val >= -2048 && val <= 2047) {
                emit("li " + reg + ", " + std::to_string(val));
            } else {
                // Large immediate: use lui + addi
                int upper = (val >> 12) & 0xFFFFF;
                int lower = val & 0xFFF;
                // Handle sign extension for negative lower bits
                if (lower & 0x800) {
                    // lower is negative in 12-bit representation
                    lower = lower | ~0xFFF;  // sign extend
                    // Adjust upper if lower is negative
                    if (val < 0 && lower != (val & 0xFFF)) {
                        upper = ((val + 0x1000) >> 12) & 0xFFFFF;
                        lower = (val + 0x1000) & 0xFFF;
                        if (lower & 0x800) lower = lower - 0x1000;
                    }
                }
                emit("lui " + reg + ", " + std::to_string(upper));
                if ((val & 0xFFF) != 0) {
                    emit("addi " + reg + ", " + reg + ", " + std::to_string(val & 0xFFF));
                }
            }
            return reg;
        }
        case ExprKind::ID: {
            auto* ie = dynamic_cast<const IdExpr*>(expr);
            std::string reg = allocReg();

            if (ie->varIsConst && ie->varIsGlobal) {
                // Global constant: can load directly
                emit("la " + reg + ", " + ie->name);
                emit("lw " + reg + ", 0(" + reg + ")");
            } else if (ie->varIsGlobal) {
                // Global variable
                emit("la " + reg + ", " + ie->name);
                emit("lw " + reg + ", 0(" + reg + ")");
            } else {
                // Local variable or parameter
                int offset = ie->varOffset;
                emit("lw " + reg + ", " + std::to_string(offset) + "(fp)");
            }
            return reg;
        }
        case ExprKind::BINARY: {
            auto* be = dynamic_cast<const BinaryExpr*>(expr);

            // Short-circuit evaluation for && and ||
            if (be->op == TokenType::TOK_AND) {
                std::string resultReg = allocReg();
                std::string falseLabel = newLabel("and_false");
                std::string endLabel = newLabel("and_end");

                // Evaluate left
                std::string leftReg = generateExpr(be->left.get());
                emit("beqz " + leftReg + ", " + falseLabel);
                freeReg(leftReg);

                // Evaluate right (only if left was true)
                std::string rightReg = generateExpr(be->right.get());
                emit("snez " + resultReg + ", " + rightReg);
                freeReg(rightReg);
                emit("j " + endLabel);

                // Left was false: result = 0
                emitLabel(falseLabel);
                emit("mv " + resultReg + ", zero");

                emitLabel(endLabel);
                return resultReg;
            }

            if (be->op == TokenType::TOK_OR) {
                std::string resultReg = allocReg();
                std::string trueLabel = newLabel("or_true");
                std::string endLabel = newLabel("or_end");

                // Evaluate left
                std::string leftReg = generateExpr(be->left.get());
                emit("bnez " + leftReg + ", " + trueLabel);
                freeReg(leftReg);

                // Evaluate right (only if left was false)
                std::string rightReg = generateExpr(be->right.get());
                emit("snez " + resultReg + ", " + rightReg);
                freeReg(rightReg);
                emit("j " + endLabel);

                // Left was true: result = 1
                emitLabel(trueLabel);
                emit("li " + resultReg + ", 1");

                emitLabel(endLabel);
                return resultReg;
            }

            // Normal binary operations
            std::string leftReg = generateExpr(be->left.get());
            std::string rightReg = generateExpr(be->right.get());

            switch (be->op) {
                case TokenType::TOK_ADD:
                    emit("add " + leftReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_SUB:
                    emit("sub " + leftReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_MUL:
                    emit("mul " + leftReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_DIV:
                    emit("div " + leftReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_MOD:
                    emit("rem " + leftReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_LT:
                    emit("slt " + leftReg + ", " + leftReg + ", " + rightReg);
                    break;
                case TokenType::TOK_GT:
                    emit("slt " + leftReg + ", " + rightReg + ", " + leftReg);
                    break;
                case TokenType::TOK_LE:
                    // a <= b == !(b < a)
                    emit("slt " + leftReg + ", " + rightReg + ", " + leftReg);
                    emit("xori " + leftReg + ", " + leftReg + ", 1");
                    break;
                case TokenType::TOK_GE:
                    // a >= b == !(a < b)
                    emit("slt " + leftReg + ", " + leftReg + ", " + rightReg);
                    emit("xori " + leftReg + ", " + leftReg + ", 1");
                    break;
                case TokenType::TOK_EQ:
                    emit("sub " + leftReg + ", " + leftReg + ", " + rightReg);
                    emit("seqz " + leftReg + ", " + leftReg);
                    break;
                case TokenType::TOK_NE:
                    emit("sub " + leftReg + ", " + leftReg + ", " + rightReg);
                    emit("snez " + leftReg + ", " + leftReg);
                    break;
                default:
                    break;
            }

            freeReg(rightReg);
            return leftReg;
        }
        case ExprKind::UNARY: {
            auto* ue = dynamic_cast<const UnaryExpr*>(expr);
            std::string opReg = generateExpr(ue->operand.get());

            switch (ue->op) {
                case TokenType::TOK_SUB:
                    emit("neg " + opReg + ", " + opReg);
                    break;
                case TokenType::TOK_ADD:
                    // Unary plus: no change
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

            // Save caller-saved registers (t0-t6) that are currently in use
            // We need to save them because the callee may modify them
            int savedCount = static_cast<int>(m_usedRegs.size());
            int saveSize = savedCount * 4;
            
            if (saveSize > 0) {
                emit("addi sp, sp, -" + std::to_string(saveSize));
                for (int i = 0; i < savedCount; i++) {
                    emit("sw " + m_usedRegs[i] + ", " + std::to_string(i * 4) + "(sp)");
                }
            }

            // Evaluate arguments into a0-a7
            // Note: we evaluate arguments AFTER saving temporaries
            // because argument evaluation may use temporaries
            for (size_t i = 0; i < ce->args.size() && i < 8; i++) {
                std::string argReg = generateExpr(ce->args[i].get());
                emit("mv a" + std::to_string(i) + ", " + argReg);
                freeReg(argReg);
            }

            // Call the function
            emit("call " + ce->funcName);

            // Result is in a0 - move to a temporary
            std::string resultReg = allocReg();
            emit("mv " + resultReg + ", a0");

            // Restore saved temporaries
            if (saveSize > 0) {
                for (int i = 0; i < savedCount; i++) {
                    emit("lw " + m_usedRegs[i] + ", " + std::to_string(i * 4) + "(sp)");
                }
                emit("addi sp, sp, " + std::to_string(saveSize));
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