#include "CodeGenerator.h"
#include "CompileTimeEvaluator.h"
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <functional>
#include <limits>

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

void CodeGenerator::emitAdjustStack(int amount, const std::string& scratchReg) {
    if (amount == 0) return;
    if (fitsImm12(amount)) {
        emit("addi sp, sp, " + std::to_string(amount));
    } else {
        emit("li " + scratchReg + ", " + std::to_string(amount));
        emit("add sp, sp, " + scratchReg);
    }
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

    if (m_optimize) {
        CompileTimeEvaluator evaluator;
        if (auto result = evaluator.evaluate(comp)) {
            emitComment("whole-program compile-time evaluation");
            emit(".text");
            emit(".globl main");
            emitLabel("main");
            emit("li a0, " + std::to_string(*result));
            emit("jr ra");
            return;
        }
        scanGlobalWrites(comp);
    }

    // Data section for global variables
    emit(".data");
    for (auto& decl : comp.globals) {
        generateGlobal(decl.get());
    }

    // Text section
    emit(".text");

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

void CodeGenerator::scanGlobalWrites(const CompUnit& comp) {
    m_globalInitialValues.clear();
    m_knownGlobalConstants.clear();
    m_writtenGlobals.clear();

    for (const auto& decl : comp.globals) {
        if (decl->init && decl->init->isConst) {
            m_globalInitialValues[decl->name] = decl->init->constValue;
        }
    }

    std::function<void(const Stmt*)> scanStmt;
    std::function<void(const Block*)> scanBlock;
    scanBlock = [&](const Block* block) {
        if (!block) return;
        for (const auto& stmt : block->stmts) scanStmt(stmt.get());
    };
    scanStmt = [&](const Stmt* stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
            case StmtKind::BLOCK:
                scanBlock(dynamic_cast<const BlockStmt*>(stmt)->block.get());
                break;
            case StmtKind::ASSIGN: {
                auto* assign = dynamic_cast<const AssignStmt*>(stmt);
                if (assign->varIsGlobal) m_writtenGlobals.insert(assign->name);
                break;
            }
            case StmtKind::IF: {
                auto* ifStmt = dynamic_cast<const IfStmt*>(stmt);
                scanStmt(ifStmt->thenStmt.get());
                scanStmt(ifStmt->elseStmt.get());
                break;
            }
            case StmtKind::WHILE:
                scanStmt(dynamic_cast<const WhileStmt*>(stmt)->body.get());
                break;
            default:
                break;
        }
    };

    for (const auto& func : comp.functions) scanBlock(func->body.get());
    for (const auto& [name, value] : m_globalInitialValues) {
        if (!m_writtenGlobals.contains(name)) m_knownGlobalConstants[name] = value;
    }
}

void CodeGenerator::collectUsage(const Expr* expr, int weight) {
    if (!expr) return;
    switch (expr->kind) {
        case ExprKind::ID: {
            auto* id = dynamic_cast<const IdExpr*>(expr);
            if (id->varIsGlobal) {
                if (!m_knownGlobalConstants.contains(id->name)) {
                    m_globalUseScores[id->name] += 3 * weight;
                }
            } else if (!m_knownLocalConstants.contains(id->varOffset)) {
                m_localReadCounts[id->varOffset]++;
                m_localUseScores[id->varOffset] += 3 * weight;
            }
            break;
        }
        case ExprKind::BINARY: {
            auto* binary = dynamic_cast<const BinaryExpr*>(expr);
            collectUsage(binary->left.get(), weight);
            collectUsage(binary->right.get(), weight);
            break;
        }
        case ExprKind::UNARY:
            collectUsage(dynamic_cast<const UnaryExpr*>(expr)->operand.get(), weight);
            break;
        case ExprKind::CALL:
            for (const auto& arg : dynamic_cast<const CallExpr*>(expr)->args) {
                collectUsage(arg.get(), weight);
            }
            break;
        case ExprKind::NUMBER:
            break;
    }
}

void CodeGenerator::collectUsage(const Block* block, int weight) {
    if (!block) return;
    for (const auto& stmt : block->stmts) collectUsage(stmt.get(), weight);
}

void CodeGenerator::collectUsage(const Stmt* stmt, int weight) {
    if (!stmt) return;
    switch (stmt->kind) {
        case StmtKind::BLOCK:
            collectUsage(dynamic_cast<const BlockStmt*>(stmt)->block.get(), weight);
            break;
        case StmtKind::EXPR:
            collectUsage(dynamic_cast<const ExprStmt*>(stmt)->expr.get(), weight);
            break;
        case StmtKind::ASSIGN: {
            auto* assign = dynamic_cast<const AssignStmt*>(stmt);
            if (assign->varIsGlobal) {
                m_globalUseScores[assign->name] += 2 * weight;
                m_functionWrittenGlobals.insert(assign->name);
            } else {
                m_localWriteCounts[assign->varOffset]++;
                m_localUseScores[assign->varOffset] += 2 * weight;
            }
            collectUsage(assign->value.get(), weight);
            break;
        }
        case StmtKind::DECL: {
            auto* declStmt = dynamic_cast<const DeclStmt*>(stmt);
            if (!m_knownLocalConstants.contains(declStmt->decl->varOffset)) {
                m_localUseScores[declStmt->decl->varOffset] += weight;
                collectUsage(declStmt->decl->init.get(), weight);
            }
            break;
        }
        case StmtKind::IF: {
            auto* ifStmt = dynamic_cast<const IfStmt*>(stmt);
            collectUsage(ifStmt->condition.get(), weight);
            collectUsage(ifStmt->thenStmt.get(), weight);
            collectUsage(ifStmt->elseStmt.get(), weight);
            break;
        }
        case StmtKind::WHILE: {
            auto* whileStmt = dynamic_cast<const WhileStmt*>(stmt);
            int loopWeight = std::min(weight * 16, 1 << 20);
            collectUsage(whileStmt->condition.get(), loopWeight);
            collectUsage(whileStmt->body.get(), loopWeight);
            break;
        }
        case StmtKind::RETURN:
            collectUsage(dynamic_cast<const ReturnStmt*>(stmt)->expr.get(), weight);
            break;
        default:
            break;
    }
}

void CodeGenerator::collectLocalConstants(const Block* block) {
    if (!block) return;
    for (const auto& stmt : block->stmts) collectLocalConstants(stmt.get());
}

void CodeGenerator::collectLocalConstants(const Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case StmtKind::BLOCK:
            collectLocalConstants(dynamic_cast<const BlockStmt*>(stmt)->block.get());
            break;
        case StmtKind::DECL: {
            auto* declStmt = dynamic_cast<const DeclStmt*>(stmt);
            const Decl* decl = declStmt->decl.get();
            if (m_localWriteCounts[decl->varOffset] == 0 && decl->init && decl->init->isConst) {
                m_knownLocalConstants[decl->varOffset] = decl->init->constValue;
            }
            break;
        }
        case StmtKind::IF: {
            auto* ifStmt = dynamic_cast<const IfStmt*>(stmt);
            collectLocalConstants(ifStmt->thenStmt.get());
            collectLocalConstants(ifStmt->elseStmt.get());
            break;
        }
        case StmtKind::WHILE:
            collectLocalConstants(dynamic_cast<const WhileStmt*>(stmt)->body.get());
            break;
        default:
            break;
    }
}

int CodeGenerator::localAliasRoot(int offset) const {
    std::unordered_set<int> seen;
    auto it = m_localCopySource.find(offset);
    while (it != m_localCopySource.end() && !seen.contains(offset)) {
        seen.insert(offset);
        offset = it->second;
        it = m_localCopySource.find(offset);
    }
    return offset;
}

void CodeGenerator::collectCopyAliases(const Block* block) {
    if (!block) return;
    for (const auto& stmt : block->stmts) collectCopyAliases(stmt.get());
}

void CodeGenerator::collectCopyAliases(const Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case StmtKind::BLOCK:
            collectCopyAliases(dynamic_cast<const BlockStmt*>(stmt)->block.get());
            break;
        case StmtKind::DECL: {
            const Decl* decl = dynamic_cast<const DeclStmt*>(stmt)->decl.get();
            if (m_localWriteCounts[decl->varOffset] == 0 && decl->init &&
                decl->init->kind == ExprKind::ID &&
                !m_knownLocalConstants.contains(decl->varOffset)) {
                auto* source = dynamic_cast<const IdExpr*>(decl->init.get());
                if (!source->varIsGlobal && m_localWriteCounts[source->varOffset] == 0 &&
                    !m_knownLocalConstants.contains(source->varOffset)) {
                    m_localCopySource[decl->varOffset] = localAliasRoot(source->varOffset);
                }
            }
            break;
        }
        case StmtKind::IF: {
            auto* ifStmt = dynamic_cast<const IfStmt*>(stmt);
            collectCopyAliases(ifStmt->thenStmt.get());
            collectCopyAliases(ifStmt->elseStmt.get());
            break;
        }
        case StmtKind::WHILE:
            collectCopyAliases(dynamic_cast<const WhileStmt*>(stmt)->body.get());
            break;
        default:
            break;
    }
}

bool CodeGenerator::blockHasNonTailCall(const Block* block) const {
    if (!block) return false;
    for (const auto& stmt : block->stmts) {
        if (stmtHasNonTailCall(stmt.get())) return true;
    }
    return false;
}

bool CodeGenerator::stmtHasNonTailCall(const Stmt* stmt) const {
    if (!stmt) return false;
    switch (stmt->kind) {
        case StmtKind::BLOCK:
            return blockHasNonTailCall(dynamic_cast<const BlockStmt*>(stmt)->block.get());
        case StmtKind::EXPR:
            return exprHasCall(dynamic_cast<const ExprStmt*>(stmt)->expr.get());
        case StmtKind::ASSIGN:
            return exprHasCall(dynamic_cast<const AssignStmt*>(stmt)->value.get());
        case StmtKind::DECL:
            return exprHasCall(dynamic_cast<const DeclStmt*>(stmt)->decl->init.get());
        case StmtKind::IF: {
            auto* ifStmt = dynamic_cast<const IfStmt*>(stmt);
            return exprHasCall(ifStmt->condition.get()) ||
                   stmtHasNonTailCall(ifStmt->thenStmt.get()) ||
                   stmtHasNonTailCall(ifStmt->elseStmt.get());
        }
        case StmtKind::WHILE: {
            auto* whileStmt = dynamic_cast<const WhileStmt*>(stmt);
            return exprHasCall(whileStmt->condition.get()) ||
                   stmtHasNonTailCall(whileStmt->body.get());
        }
        case StmtKind::RETURN: {
            auto* returnStmt = dynamic_cast<const ReturnStmt*>(stmt);
            if (!returnStmt->hasExpr || !returnStmt->expr) return false;
            if (returnStmt->expr->kind == ExprKind::CALL) {
                auto* call = dynamic_cast<const CallExpr*>(returnStmt->expr.get());
                if (m_currentFunc && call->funcName == m_currentFunc->name) {
                    for (const auto& arg : call->args) {
                        if (exprHasCall(arg.get())) return true;
                    }
                    return false;
                }
            }
            return exprHasCall(returnStmt->expr.get());
        }
        default:
            return false;
    }
}

void CodeGenerator::setupLocalRegs(const FuncDef* func) {
    m_localRegByOffset.clear();
    m_globalRegByName.clear();
    m_cachedGlobalNames.clear();
    m_savedSRegs.clear();
    m_localReadCounts.clear();
    m_localWriteCounts.clear();
    m_localUseScores.clear();
    m_globalUseScores.clear();
    m_knownLocalConstants.clear();
    m_localCopySource.clear();
    m_functionWrittenGlobals.clear();
    m_cseRegs.clear();

    if (!m_optimize || !func) return;

    // First pass finds reassigned locals; locals initialized once to a constant
    // can then disappear entirely from the run-time program.
    collectUsage(func->body.get());
    collectLocalConstants(func->body.get());
    collectCopyAliases(func->body.get());

    m_localReadCounts.clear();
    m_localWriteCounts.clear();
    m_localUseScores.clear();
    m_globalUseScores.clear();
    m_functionWrittenGlobals.clear();
    collectUsage(func->body.get());

    struct Candidate {
        int score;
        int offset;
        std::string globalName;
    };
    std::vector<Candidate> candidates;
    std::unordered_map<int, int> aliasedScores;
    for (const auto& [offset, score] : m_localUseScores) {
        if (m_localReadCounts[offset] > 0) aliasedScores[localAliasRoot(offset)] += score;
    }
    for (const auto& [offset, score] : aliasedScores) {
        if (score > 0) candidates.push_back({score, offset, ""});
    }
    for (const auto& [name, score] : m_globalUseScores) {
        if (score > 0 && !m_knownGlobalConstants.contains(name)) {
            candidates.push_back({score, 0, name});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.score > rhs.score;
    });

    const std::vector<std::string> regs = {
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11"
    };

    for (const Candidate& candidate : candidates) {
        if (m_savedSRegs.size() >= regs.size()) break;
        const std::string& reg = regs[m_savedSRegs.size()];
        if (candidate.globalName.empty()) {
            for (const auto& [offset, score] : m_localUseScores) {
                if (m_localReadCounts[offset] > 0 && localAliasRoot(offset) == candidate.offset) {
                    m_localRegByOffset[offset] = reg;
                }
            }
        } else {
            m_globalRegByName[candidate.globalName] = reg;
            m_cachedGlobalNames.push_back(candidate.globalName);
        }
        m_savedSRegs.push_back(reg);
    }
}

void CodeGenerator::emitLoadCachedGlobals() {
    for (const std::string& name : m_cachedGlobalNames) {
        emit("la t0, " + name);
        emit("lw " + m_globalRegByName.at(name) + ", 0(t0)");
    }
}

void CodeGenerator::emitFlushCachedGlobals() {
    for (const std::string& name : m_cachedGlobalNames) {
        if (!m_functionWrittenGlobals.contains(name)) continue;
        emit("la t0, " + name);
        emit("sw " + m_globalRegByName.at(name) + ", 0(t0)");
    }
}

void CodeGenerator::emitFunctionEpilogue(int frameSize, int raOffset, int fpOffset) {
    if (m_optimize) emitFlushCachedGlobals();
    for (size_t i = 0; i < m_savedSRegs.size(); i++) {
        emitLoadStack(m_savedSRegs[i], "sp", static_cast<int>(i * 4));
    }
    if (m_saveRa) emitLoadStack("ra", "sp", raOffset);
    if (m_saveFp) emitLoadStack("fp", "sp", fpOffset);
    emitAdjustStack(frameSize);
    emit("jr ra");
}

// === Function definition ===

void CodeGenerator::generateFuncDef(const FuncDef* func) {
    m_currentFunc = func;
    m_paramSaveOffset = -12;
    setupLocalRegs(func);

    // Compute the smallest ABI-compliant frame this function actually needs.
    int savedSBytes = static_cast<int>(m_savedSRegs.size() * 4);
    bool needsFramePointer = !m_optimize;
    if (m_optimize) {
        for (const auto& [offset, reads] : m_localReadCounts) {
            if (reads > 0 && !m_localRegByOffset.contains(offset) &&
                !m_knownLocalConstants.contains(offset)) {
                needsFramePointer = true;
                break;
            }
        }
        for (size_t i = 8; i < func->paramOffsets.size(); ++i) {
            if (m_localReadCounts[func->paramOffsets[i]] > 0) needsFramePointer = true;
        }
    }
    m_saveRa = !m_optimize || blockHasNonTailCall(func->body.get());
    m_saveFp = needsFramePointer;

    int frameSize;
    if (needsFramePointer) {
        frameSize = alignTo(func->stackSize + 8 + savedSBytes, 16);
    } else {
        frameSize = alignTo(savedSBytes + (m_saveRa ? 4 : 0), 16);
    }

    int raOffset = frameSize - 4;
    int fpOffset = frameSize - 8;
    m_currentFrameSize = frameSize;
    m_currentRaOffset = raOffset;
    m_currentFpOffset = fpOffset;

    // Function label
    emit("");
    emit(".globl " + func->name);
    emitLabel(func->name);

    // Prologue
    emitComment("Prologue");
    emitAdjustStack(-frameSize);
    if (m_saveRa) emitStoreStack("ra", "sp", raOffset);
    if (m_saveFp) emitStoreStack("fp", "sp", fpOffset);
    for (size_t i = 0; i < m_savedSRegs.size(); i++) {
        emitStoreStack(m_savedSRegs[i], "sp", static_cast<int>(i * 4));
    }
    if (m_saveFp) {
        if (fitsImm12(frameSize)) {
            emit("addi fp, sp, " + std::to_string(frameSize));
        } else {
            emit("li t0, " + std::to_string(frameSize));
            emit("add fp, sp, t0");
        }
    }

    // Save parameters to stack
    emitComment("Save parameters");
    for (size_t i = 0; i < func->params.size(); i++) {
        int offset = func->paramOffsets[i];
        auto regIt = m_localRegByOffset.find(offset);
        if (m_optimize && m_localReadCounts[offset] == 0) continue;
        if (i < 8) {
            std::string aReg = "a" + std::to_string(i);
            if (regIt != m_localRegByOffset.end()) {
                emit("mv " + regIt->second + ", " + aReg);
            } else {
                emitStoreStack(aReg, "fp", offset);
            }
        } else {
            int incomingOffset = static_cast<int>((i - 8) * 4);
            emitLoadStack("t0", "fp", incomingOffset);
            if (regIt != m_localRegByOffset.end()) {
                emit("mv " + regIt->second + ", t0");
            } else {
                emitStoreStack("t0", "fp", offset);
            }
        }
    }

    if (m_optimize) emitLoadCachedGlobals();

    m_currentBodyLabel = newLabel("func_body");
    emitLabel(m_currentBodyLabel);

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
    emitFunctionEpilogue(frameSize, raOffset, fpOffset);

    m_currentFunc = nullptr;
    m_localRegByOffset.clear();
    m_globalRegByName.clear();
    m_cachedGlobalNames.clear();
    m_savedSRegs.clear();
    m_currentBodyLabel.clear();
    m_currentFrameSize = 0;
    m_currentRaOffset = -1;
    m_currentFpOffset = -1;
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
            if (m_optimize && exprIsPure(es->expr.get())) break;
            std::string reg = generateExpr(es->expr.get());
            freeReg(reg);
            if (m_optimize) m_cseRegs.clear();
            break;
        }
        case StmtKind::ASSIGN: {
            auto* as = dynamic_cast<const AssignStmt*>(stmt);
            std::string cseKey = m_optimize ? expressionKey(as->value.get()) : "";
            auto generateCached = [&](const std::string& destReg, bool selfReference) {
                auto existing = m_cseRegs.find(cseKey);
                if (!cseKey.empty() && existing != m_cseRegs.end()) {
                    if (destReg != existing->second) emit("mv " + destReg + ", " + existing->second);
                } else {
                    generateExprInto(as->value.get(), destReg);
                }
                m_cseRegs.clear();
                if (!cseKey.empty() && !selfReference) m_cseRegs[cseKey] = destReg;
            };

            if (m_optimize && !as->varIsGlobal && m_localReadCounts[as->varOffset] == 0) {
                if (exprHasCall(as->value.get())) {
                    std::string ignored = generateExpr(as->value.get());
                    freeReg(ignored);
                }
                break;
            }

            // Store to variable
            if (as->varIsGlobal) {
                auto globalReg = m_globalRegByName.find(as->name);
                if (m_optimize && globalReg != m_globalRegByName.end()) {
                    generateCached(globalReg->second,
                                   exprReferencesGlobal(as->value.get(), as->name));
                } else {
                    std::string valReg = generateExpr(as->value.get());
                    std::string scratch = scratchRegAvoiding(valReg);
                    emit("la " + scratch + ", " + as->name);
                    emit("sw " + valReg + ", 0(" + scratch + ")");
                    freeReg(valReg);
                    if (m_optimize) m_cseRegs.clear();
                }
            } else {
                auto regIt = m_localRegByOffset.find(as->varOffset);
                if (m_optimize && regIt != m_localRegByOffset.end()) {
                    generateCached(regIt->second,
                                   exprReferencesLocal(as->value.get(), as->varOffset));
                } else {
                    std::string valReg = generateExpr(as->value.get());
                    emitStoreStack(valReg, "fp", as->varOffset);
                    freeReg(valReg);
                    if (m_optimize) m_cseRegs.clear();
                }
            }
            break;
        }
        case StmtKind::DECL: {
            auto* ds = dynamic_cast<const DeclStmt*>(stmt);
            if (m_optimize && m_knownLocalConstants.contains(ds->decl->varOffset)) break;
            if (m_optimize && m_localReadCounts[ds->decl->varOffset] == 0) {
                if (exprHasCall(ds->decl->init.get())) {
                    std::string ignored = generateExpr(ds->decl->init.get());
                    freeReg(ignored);
                }
                break;
            }
            // Evaluate initializer and store
            if (!ds->decl->isGlobal) {
                auto regIt = m_localRegByOffset.find(ds->decl->varOffset);
                if (m_optimize && regIt != m_localRegByOffset.end()) {
                    std::string cseKey = expressionKey(ds->decl->init.get());
                    auto existing = m_cseRegs.find(cseKey);
                    if (!cseKey.empty() && existing != m_cseRegs.end()) {
                        if (regIt->second != existing->second) {
                            emit("mv " + regIt->second + ", " + existing->second);
                        }
                    } else {
                        generateExprInto(ds->decl->init.get(), regIt->second);
                    }
                    m_cseRegs.clear();
                    if (!cseKey.empty()) m_cseRegs[cseKey] = regIt->second;
                } else {
                    std::string valReg = generateExpr(ds->decl->init.get());
                    emitStoreStack(valReg, "fp", ds->decl->varOffset);
                    freeReg(valReg);
                    if (m_optimize) m_cseRegs.clear();
                }
            }
            break;
        }
        case StmtKind::IF: {
            auto* is = dynamic_cast<const IfStmt*>(stmt);

            if (m_optimize && is->condition->isConst) {
                if (is->condition->constValue != 0) {
                    generateStmt(is->thenStmt.get());
                } else if (is->elseStmt) {
                    generateStmt(is->elseStmt.get());
                }
                break;
            }

            std::string elseLabel = newLabel("else");
            std::string endLabel = newLabel("endif");

            if (m_optimize) {
                generateCondBranch(is->condition.get(), elseLabel, false);
                m_cseRegs.clear();
            } else {
                std::string condReg = generateExpr(is->condition.get());
                emit("beqz " + condReg + ", " + elseLabel);
                freeReg(condReg);
            }

            generateStmt(is->thenStmt.get());

            if (is->elseStmt) {
                emit("j " + endLabel);
            }

            emitLabel(elseLabel);

            if (is->elseStmt) {
                if (m_optimize) m_cseRegs.clear();
                generateStmt(is->elseStmt.get());
                emitLabel(endLabel);
            }
            if (m_optimize) m_cseRegs.clear();
            break;
        }
        case StmtKind::WHILE: {
            auto* ws = dynamic_cast<const WhileStmt*>(stmt);

            if (m_optimize && ws->condition->isConst && ws->condition->constValue == 0) {
                break;
            }

            std::string startLabel = newLabel("while_cond");
            std::string endLabel = newLabel("while_end");

            m_loopStack.push({startLabel, endLabel});

            if (m_optimize) {
                // Rotate the loop so the hot back-edge is a single conditional
                // branch instead of a conditional branch plus an unconditional jump.
                std::string bodyLabel = newLabel("while_body");
                emit("j " + startLabel);
                emitLabel(bodyLabel);
                m_cseRegs.clear();
                generateStmt(ws->body.get());
                emitLabel(startLabel);
                m_cseRegs.clear();
                if (ws->condition->isConst) {
                    emit("j " + bodyLabel);
                } else {
                    generateCondBranch(ws->condition.get(), bodyLabel, true);
                }
            } else {
                emitLabel(startLabel);
                std::string condReg = generateExpr(ws->condition.get());
                emit("beqz " + condReg + ", " + endLabel);
                freeReg(condReg);
                generateStmt(ws->body.get());
                emit("j " + startLabel);
            }

            emitLabel(endLabel);
            if (m_optimize) m_cseRegs.clear();

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

            if (m_optimize && rs->hasExpr && rs->expr && rs->expr->kind == ExprKind::CALL) {
                auto* call = dynamic_cast<const CallExpr*>(rs->expr.get());
                if (call->funcName == m_currentFunc->name &&
                    call->args.size() == m_currentFunc->params.size()) {
                    if (call->args.size() <= 4) {
                        std::vector<std::string> argRegs;
                        for (const auto& arg : call->args) {
                            argRegs.push_back(generateExpr(arg.get()));
                        }
                        for (size_t i = 0; i < argRegs.size(); ++i) {
                            int paramOffset = m_currentFunc->paramOffsets[i];
                            if (m_localReadCounts[paramOffset] == 0) continue;
                            auto paramReg = m_localRegByOffset.find(paramOffset);
                            if (paramReg != m_localRegByOffset.end()) {
                                emit("mv " + paramReg->second + ", " + argRegs[i]);
                            } else {
                                emitStoreStack(argRegs[i], "fp", paramOffset);
                            }
                        }
                        for (const std::string& argReg : argRegs) freeReg(argReg);
                    } else {
                        int tempBytes = alignTo(static_cast<int>(call->args.size() * 4), 16);
                        emitAdjustStack(-tempBytes);
                        for (size_t i = 0; i < call->args.size(); ++i) {
                            std::string argReg = generateExpr(call->args[i].get());
                            emitStoreStack(argReg, "sp", static_cast<int>(i * 4));
                            freeReg(argReg);
                        }
                        for (size_t i = 0; i < call->args.size(); ++i) {
                            int paramOffset = m_currentFunc->paramOffsets[i];
                            if (m_localReadCounts[paramOffset] == 0) continue;
                            emitLoadStack("t0", "sp", static_cast<int>(i * 4));
                            auto paramReg = m_localRegByOffset.find(paramOffset);
                            if (paramReg != m_localRegByOffset.end()) {
                                emit("mv " + paramReg->second + ", t0");
                            } else {
                                emitStoreStack("t0", "fp", paramOffset);
                            }
                        }
                        emitAdjustStack(tempBytes);
                    }
                    emit("j " + m_currentBodyLabel);
                    break;
                }
            }

            if (rs->hasExpr) {
                if (m_optimize) {
                    generateExprInto(rs->expr.get(), "a0");
                } else {
                    std::string retReg = generateExpr(rs->expr.get());
                    // Move result to a0
                    if (retReg != "a0") {
                        emit("mv a0, " + retReg);
                    }
                    freeReg(retReg);
                }
            } else {
                emit("mv a0, zero");
            }

            // Epilogue and return
            emitFunctionEpilogue(m_currentFrameSize, m_currentRaOffset, m_currentFpOffset);
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

bool CodeGenerator::exprHasCall(const Expr* expr) const {
    if (!expr) return false;
    switch (expr->kind) {
        case ExprKind::CALL:
            return true;
        case ExprKind::BINARY: {
            auto* binary = dynamic_cast<const BinaryExpr*>(expr);
            return exprHasCall(binary->left.get()) || exprHasCall(binary->right.get());
        }
        case ExprKind::UNARY:
            return exprHasCall(dynamic_cast<const UnaryExpr*>(expr)->operand.get());
        default:
            return false;
    }
}

bool CodeGenerator::exprIsPure(const Expr* expr) const {
    return expr && !exprHasCall(expr);
}

bool CodeGenerator::getKnownConstant(const Expr* expr, int& value) const {
    if (!expr) return false;
    if (expr->isConst) {
        value = expr->constValue;
        return true;
    }
    if (expr->kind != ExprKind::ID) return false;
    auto* id = dynamic_cast<const IdExpr*>(expr);
    if (id->varIsGlobal) {
        auto it = m_knownGlobalConstants.find(id->name);
        if (it == m_knownGlobalConstants.end()) return false;
        value = it->second;
        return true;
    }
    auto it = m_knownLocalConstants.find(id->varOffset);
    if (it == m_knownLocalConstants.end()) return false;
    value = it->second;
    return true;
}

bool CodeGenerator::exprReferencesLocal(const Expr* expr, int offset) const {
    if (!expr) return false;
    switch (expr->kind) {
        case ExprKind::ID: {
            auto* id = dynamic_cast<const IdExpr*>(expr);
            return !id->varIsGlobal && id->varOffset == offset;
        }
        case ExprKind::BINARY: {
            auto* binary = dynamic_cast<const BinaryExpr*>(expr);
            return exprReferencesLocal(binary->left.get(), offset) ||
                   exprReferencesLocal(binary->right.get(), offset);
        }
        case ExprKind::UNARY:
            return exprReferencesLocal(dynamic_cast<const UnaryExpr*>(expr)->operand.get(), offset);
        case ExprKind::CALL:
            for (const auto& arg : dynamic_cast<const CallExpr*>(expr)->args) {
                if (exprReferencesLocal(arg.get(), offset)) return true;
            }
            return false;
        default:
            return false;
    }
}

bool CodeGenerator::exprReferencesGlobal(const Expr* expr, const std::string& name) const {
    if (!expr) return false;
    switch (expr->kind) {
        case ExprKind::ID: {
            auto* id = dynamic_cast<const IdExpr*>(expr);
            return id->varIsGlobal && id->name == name;
        }
        case ExprKind::BINARY: {
            auto* binary = dynamic_cast<const BinaryExpr*>(expr);
            return exprReferencesGlobal(binary->left.get(), name) ||
                   exprReferencesGlobal(binary->right.get(), name);
        }
        case ExprKind::UNARY:
            return exprReferencesGlobal(dynamic_cast<const UnaryExpr*>(expr)->operand.get(), name);
        case ExprKind::CALL:
            for (const auto& arg : dynamic_cast<const CallExpr*>(expr)->args) {
                if (exprReferencesGlobal(arg.get(), name)) return true;
            }
            return false;
        default:
            return false;
    }
}

std::string CodeGenerator::expressionKey(const Expr* expr) const {
    if (!expr || !exprIsPure(expr)) return "";
    int constValue = 0;
    if (getKnownConstant(expr, constValue)) return "#" + std::to_string(constValue);
    switch (expr->kind) {
        case ExprKind::NUMBER:
            return "#" + std::to_string(dynamic_cast<const NumberExpr*>(expr)->value);
        case ExprKind::ID: {
            auto* id = dynamic_cast<const IdExpr*>(expr);
            return id->varIsGlobal ? "G" + id->name : "L" + std::to_string(id->varOffset);
        }
        case ExprKind::UNARY: {
            auto* unary = dynamic_cast<const UnaryExpr*>(expr);
            return "U" + std::to_string(static_cast<int>(unary->op)) +
                   "(" + expressionKey(unary->operand.get()) + ")";
        }
        case ExprKind::BINARY: {
            auto* binary = dynamic_cast<const BinaryExpr*>(expr);
            return "B" + std::to_string(static_cast<int>(binary->op)) +
                   "(" + expressionKey(binary->left.get()) + "," +
                   expressionKey(binary->right.get()) + ")";
        }
        case ExprKind::CALL:
            return "";
    }
    return "";
}

CodeGenerator::RegValue CodeGenerator::materializeReadOnly(const Expr* expr) {
    int value = 0;
    if (m_optimize && getKnownConstant(expr, value)) {
        if (value == 0) return {"zero", false};
        std::string reg = allocReg();
        emit("li " + reg + ", " + std::to_string(value));
        return {reg, true};
    }

    if (m_optimize && expr && expr->kind == ExprKind::ID) {
        auto* id = dynamic_cast<const IdExpr*>(expr);
        if (id->varIsGlobal) {
            auto it = m_globalRegByName.find(id->name);
            if (it != m_globalRegByName.end()) return {it->second, false};
        } else {
            auto it = m_localRegByOffset.find(id->varOffset);
            if (it != m_localRegByOffset.end()) return {it->second, false};
        }
    }

    return {generateExpr(expr), true};
}

void CodeGenerator::release(const RegValue& value) {
    if (value.temporary) freeReg(value.reg);
}

void CodeGenerator::generateCondBranch(const Expr* expr,
                                       const std::string& label,
                                       bool branchIfTrue) {
    int constant = 0;
    if (getKnownConstant(expr, constant)) {
        if ((constant != 0) == branchIfTrue) emit("j " + label);
        return;
    }

    if (expr->kind == ExprKind::UNARY) {
        auto* unary = dynamic_cast<const UnaryExpr*>(expr);
        if (unary->op == TokenType::TOK_NOT) {
            generateCondBranch(unary->operand.get(), label, !branchIfTrue);
            return;
        }
    }

    if (expr->kind == ExprKind::BINARY) {
        auto* binary = dynamic_cast<const BinaryExpr*>(expr);
        if (binary->op == TokenType::TOK_AND) {
            if (branchIfTrue) {
                std::string skip = newLabel("and_false");
                generateCondBranch(binary->left.get(), skip, false);
                generateCondBranch(binary->right.get(), label, true);
                emitLabel(skip);
            } else {
                generateCondBranch(binary->left.get(), label, false);
                generateCondBranch(binary->right.get(), label, false);
            }
            return;
        }
        if (binary->op == TokenType::TOK_OR) {
            if (branchIfTrue) {
                generateCondBranch(binary->left.get(), label, true);
                generateCondBranch(binary->right.get(), label, true);
            } else {
                std::string skip = newLabel("or_true");
                generateCondBranch(binary->left.get(), skip, true);
                generateCondBranch(binary->right.get(), label, false);
                emitLabel(skip);
            }
            return;
        }

        if (binary->op == TokenType::TOK_LT || binary->op == TokenType::TOK_GT ||
            binary->op == TokenType::TOK_LE || binary->op == TokenType::TOK_GE ||
            binary->op == TokenType::TOK_EQ || binary->op == TokenType::TOK_NE) {
            RegValue left = materializeReadOnly(binary->left.get());
            RegValue right = materializeReadOnly(binary->right.get());
            std::string insn;
            std::string lhs = left.reg;
            std::string rhs = right.reg;
            switch (binary->op) {
                case TokenType::TOK_LT: insn = branchIfTrue ? "blt" : "bge"; break;
                case TokenType::TOK_GT:
                    insn = branchIfTrue ? "blt" : "bge";
                    std::swap(lhs, rhs);
                    break;
                case TokenType::TOK_LE:
                    insn = branchIfTrue ? "bge" : "blt";
                    std::swap(lhs, rhs);
                    break;
                case TokenType::TOK_GE: insn = branchIfTrue ? "bge" : "blt"; break;
                case TokenType::TOK_EQ: insn = branchIfTrue ? "beq" : "bne"; break;
                case TokenType::TOK_NE: insn = branchIfTrue ? "bne" : "beq"; break;
                default: break;
            }
            emit(insn + " " + lhs + ", " + rhs + ", " + label);
            release(right);
            release(left);
            return;
        }
    }

    RegValue value = materializeReadOnly(expr);
    emit(std::string(branchIfTrue ? "bnez " : "beqz ") + value.reg + ", " + label);
    release(value);
}

void CodeGenerator::generateExprInto(const Expr* expr, const std::string& destReg) {
    if (!expr) {
        emit("mv " + destReg + ", zero");
        return;
    }

    int constant = 0;
    if (getKnownConstant(expr, constant)) {
        emit("li " + destReg + ", " + std::to_string(constant));
        return;
    }

    switch (expr->kind) {
        case ExprKind::NUMBER:
            emit("li " + destReg + ", " +
                 std::to_string(dynamic_cast<const NumberExpr*>(expr)->value));
            return;

        case ExprKind::ID: {
            auto* id = dynamic_cast<const IdExpr*>(expr);
            if (id->varIsGlobal) {
                auto globalReg = m_globalRegByName.find(id->name);
                if (globalReg != m_globalRegByName.end()) {
                    if (destReg != globalReg->second) emit("mv " + destReg + ", " + globalReg->second);
                } else {
                    emit("la " + destReg + ", " + id->name);
                    emit("lw " + destReg + ", 0(" + destReg + ")");
                }
            } else {
                auto localReg = m_localRegByOffset.find(id->varOffset);
                if (localReg != m_localRegByOffset.end()) {
                    if (destReg != localReg->second) emit("mv " + destReg + ", " + localReg->second);
                } else {
                    emitLoadStack(destReg, "fp", id->varOffset);
                }
            }
            return;
        }

        case ExprKind::UNARY: {
            auto* unary = dynamic_cast<const UnaryExpr*>(expr);
            RegValue operand = materializeReadOnly(unary->operand.get());
            if (unary->op == TokenType::TOK_ADD) {
                if (destReg != operand.reg) emit("mv " + destReg + ", " + operand.reg);
            } else if (unary->op == TokenType::TOK_SUB) {
                emit("sub " + destReg + ", zero, " + operand.reg);
            } else if (unary->op == TokenType::TOK_NOT) {
                emit("seqz " + destReg + ", " + operand.reg);
            }
            release(operand);
            return;
        }

        case ExprKind::BINARY: {
            auto* binary = dynamic_cast<const BinaryExpr*>(expr);

            if (binary->op == TokenType::TOK_AND || binary->op == TokenType::TOK_OR) {
                std::string falseLabel = newLabel("bool_false");
                std::string endLabel = newLabel("bool_end");
                generateCondBranch(expr, falseLabel, false);
                emit("li " + destReg + ", 1");
                emit("j " + endLabel);
                emitLabel(falseLabel);
                emit("mv " + destReg + ", zero");
                emitLabel(endLabel);
                return;
            }

            std::string leftKey = expressionKey(binary->left.get());
            std::string rightKey = expressionKey(binary->right.get());
            if (!leftKey.empty() && leftKey == rightKey) {
                RegValue value = materializeReadOnly(binary->left.get());
                switch (binary->op) {
                    case TokenType::TOK_ADD:
                        emit("slli " + destReg + ", " + value.reg + ", 1");
                        break;
                    case TokenType::TOK_SUB:
                    case TokenType::TOK_MOD:
                    case TokenType::TOK_LT:
                    case TokenType::TOK_GT:
                    case TokenType::TOK_NE:
                        emit("mv " + destReg + ", zero");
                        break;
                    case TokenType::TOK_DIV:
                    case TokenType::TOK_LE:
                    case TokenType::TOK_GE:
                    case TokenType::TOK_EQ:
                        emit("li " + destReg + ", 1");
                        break;
                    case TokenType::TOK_MUL:
                        emit("mul " + destReg + ", " + value.reg + ", " + value.reg);
                        break;
                    default:
                        break;
                }
                release(value);
                return;
            }

            int rightConstant = 0;
            bool rightIsConstant = getKnownConstant(binary->right.get(), rightConstant);
            int leftConstant = 0;
            bool leftIsConstant = getKnownConstant(binary->left.get(), leftConstant);

            if (rightIsConstant) {
                if ((binary->op == TokenType::TOK_ADD || binary->op == TokenType::TOK_SUB) &&
                    rightConstant == 0) {
                    RegValue left = materializeReadOnly(binary->left.get());
                    if (destReg != left.reg) emit("mv " + destReg + ", " + left.reg);
                    release(left);
                    return;
                }
                if ((binary->op == TokenType::TOK_MUL || binary->op == TokenType::TOK_DIV) &&
                    rightConstant == 1) {
                    RegValue left = materializeReadOnly(binary->left.get());
                    if (destReg != left.reg) emit("mv " + destReg + ", " + left.reg);
                    release(left);
                    return;
                }
                if (binary->op == TokenType::TOK_MUL && rightConstant == 0) {
                    RegValue left = materializeReadOnly(binary->left.get());
                    release(left);
                    emit("mv " + destReg + ", zero");
                    return;
                }
                if (binary->op == TokenType::TOK_MOD &&
                    (rightConstant == 1 || rightConstant == -1)) {
                    RegValue left = materializeReadOnly(binary->left.get());
                    release(left);
                    emit("mv " + destReg + ", zero");
                    return;
                }

                RegValue left = materializeReadOnly(binary->left.get());
                long long immediateValue = binary->op == TokenType::TOK_SUB
                    ? -static_cast<long long>(rightConstant)
                    : static_cast<long long>(rightConstant);
                if ((binary->op == TokenType::TOK_ADD || binary->op == TokenType::TOK_SUB) &&
                    immediateValue >= -2048 && immediateValue < 2048) {
                    int immediate = static_cast<int>(immediateValue);
                    emit("addi " + destReg + ", " + left.reg + ", " + std::to_string(immediate));
                    release(left);
                    return;
                }

                if (binary->op == TokenType::TOK_MUL) {
                    long long magnitude = rightConstant < 0
                        ? -static_cast<long long>(rightConstant)
                        : static_cast<long long>(rightConstant);
                    if (magnitude > 0 && (magnitude & (magnitude - 1)) == 0) {
                        int shift = 0;
                        while ((1LL << shift) != magnitude) ++shift;
                        emit("slli " + destReg + ", " + left.reg + ", " + std::to_string(shift));
                        if (rightConstant < 0) emit("sub " + destReg + ", zero, " + destReg);
                        release(left);
                        return;
                    }
                }

                if ((binary->op == TokenType::TOK_DIV || binary->op == TokenType::TOK_MOD) &&
                    rightConstant != std::numeric_limits<int>::min()) {
                    int magnitude = std::abs(rightConstant);
                    if (magnitude > 1 && (magnitude & (magnitude - 1)) == 0) {
                        int shift = 0;
                        while ((1 << shift) != magnitude) ++shift;
                        std::string source = left.reg;
                        std::string sourceCopy;
                        if (source == destReg) {
                            sourceCopy = allocReg();
                            emit("mv " + sourceCopy + ", " + source);
                            source = sourceCopy;
                        }
                        emit("srai " + destReg + ", " + source + ", 31");
                        emit("srli " + destReg + ", " + destReg + ", " + std::to_string(32 - shift));
                        emit("add " + destReg + ", " + source + ", " + destReg);
                        emit("srai " + destReg + ", " + destReg + ", " + std::to_string(shift));
                        if (binary->op == TokenType::TOK_DIV && rightConstant < 0) {
                            emit("sub " + destReg + ", zero, " + destReg);
                        } else if (binary->op == TokenType::TOK_MOD) {
                            emit("slli " + destReg + ", " + destReg + ", " + std::to_string(shift));
                            emit("sub " + destReg + ", " + source + ", " + destReg);
                        }
                        if (!sourceCopy.empty()) freeReg(sourceCopy);
                        release(left);
                        return;
                    }
                }

                if (binary->op == TokenType::TOK_LT && fitsImm12(rightConstant)) {
                    emit("slti " + destReg + ", " + left.reg + ", " + std::to_string(rightConstant));
                    release(left);
                    return;
                }
                if ((binary->op == TokenType::TOK_LE || binary->op == TokenType::TOK_GT) &&
                    rightConstant != std::numeric_limits<int>::max() && fitsImm12(rightConstant + 1)) {
                    emit("slti " + destReg + ", " + left.reg + ", " + std::to_string(rightConstant + 1));
                    if (binary->op == TokenType::TOK_GT) {
                        emit("xori " + destReg + ", " + destReg + ", 1");
                    }
                    release(left);
                    return;
                }
                if (binary->op == TokenType::TOK_GE && fitsImm12(rightConstant)) {
                    emit("slti " + destReg + ", " + left.reg + ", " + std::to_string(rightConstant));
                    emit("xori " + destReg + ", " + destReg + ", 1");
                    release(left);
                    return;
                }
                if ((binary->op == TokenType::TOK_EQ || binary->op == TokenType::TOK_NE) &&
                    rightConstant == 0) {
                    emit(std::string(binary->op == TokenType::TOK_EQ ? "seqz " : "snez ") +
                         destReg + ", " + left.reg);
                    release(left);
                    return;
                }
                RegValue right = materializeReadOnly(binary->right.get());
                switch (binary->op) {
                    case TokenType::TOK_ADD: emit("add " + destReg + ", " + left.reg + ", " + right.reg); break;
                    case TokenType::TOK_SUB: emit("sub " + destReg + ", " + left.reg + ", " + right.reg); break;
                    case TokenType::TOK_MUL: emit("mul " + destReg + ", " + left.reg + ", " + right.reg); break;
                    case TokenType::TOK_DIV: emit("div " + destReg + ", " + left.reg + ", " + right.reg); break;
                    case TokenType::TOK_MOD: emit("rem " + destReg + ", " + left.reg + ", " + right.reg); break;
                    case TokenType::TOK_LT: emit("slt " + destReg + ", " + left.reg + ", " + right.reg); break;
                    case TokenType::TOK_GT: emit("slt " + destReg + ", " + right.reg + ", " + left.reg); break;
                    case TokenType::TOK_LE:
                        emit("slt " + destReg + ", " + right.reg + ", " + left.reg);
                        emit("xori " + destReg + ", " + destReg + ", 1");
                        break;
                    case TokenType::TOK_GE:
                        emit("slt " + destReg + ", " + left.reg + ", " + right.reg);
                        emit("xori " + destReg + ", " + destReg + ", 1");
                        break;
                    case TokenType::TOK_EQ:
                        emit("sub " + destReg + ", " + left.reg + ", " + right.reg);
                        emit("seqz " + destReg + ", " + destReg);
                        break;
                    case TokenType::TOK_NE:
                        emit("sub " + destReg + ", " + left.reg + ", " + right.reg);
                        emit("snez " + destReg + ", " + destReg);
                        break;
                    default:
                        break;
                }
                release(right);
                release(left);
                return;
            }

            if (leftIsConstant && binary->op == TokenType::TOK_ADD && fitsImm12(leftConstant)) {
                RegValue right = materializeReadOnly(binary->right.get());
                emit("addi " + destReg + ", " + right.reg + ", " + std::to_string(leftConstant));
                release(right);
                return;
            }
            if (leftIsConstant && binary->op == TokenType::TOK_MUL && leftConstant == 0) {
                RegValue right = materializeReadOnly(binary->right.get());
                release(right);
                emit("mv " + destReg + ", zero");
                return;
            }

            RegValue left = materializeReadOnly(binary->left.get());
            RegValue right = materializeReadOnly(binary->right.get());
            switch (binary->op) {
                case TokenType::TOK_ADD: emit("add " + destReg + ", " + left.reg + ", " + right.reg); break;
                case TokenType::TOK_SUB: emit("sub " + destReg + ", " + left.reg + ", " + right.reg); break;
                case TokenType::TOK_MUL: emit("mul " + destReg + ", " + left.reg + ", " + right.reg); break;
                case TokenType::TOK_DIV: emit("div " + destReg + ", " + left.reg + ", " + right.reg); break;
                case TokenType::TOK_MOD: emit("rem " + destReg + ", " + left.reg + ", " + right.reg); break;
                case TokenType::TOK_LT: emit("slt " + destReg + ", " + left.reg + ", " + right.reg); break;
                case TokenType::TOK_GT: emit("slt " + destReg + ", " + right.reg + ", " + left.reg); break;
                case TokenType::TOK_LE:
                    emit("slt " + destReg + ", " + right.reg + ", " + left.reg);
                    emit("xori " + destReg + ", " + destReg + ", 1");
                    break;
                case TokenType::TOK_GE:
                    emit("slt " + destReg + ", " + left.reg + ", " + right.reg);
                    emit("xori " + destReg + ", " + destReg + ", 1");
                    break;
                case TokenType::TOK_EQ:
                    emit("sub " + destReg + ", " + left.reg + ", " + right.reg);
                    emit("seqz " + destReg + ", " + destReg);
                    break;
                case TokenType::TOK_NE:
                    emit("sub " + destReg + ", " + left.reg + ", " + right.reg);
                    emit("snez " + destReg + ", " + destReg);
                    break;
                default:
                    break;
            }
            release(right);
            release(left);
            return;
        }

        case ExprKind::CALL: {
            m_requestedCallResultReg = destReg;
            std::string result = generateExpr(expr);
            if (result != destReg) emit("mv " + destReg + ", " + result);
            freeReg(result);
            return;
        }
    }
}

std::string CodeGenerator::generateExpr(const Expr* expr) {
    if (!expr) {
        std::string reg = allocReg();
        emit("mv " + reg + ", zero");
        return reg;
    }

    if (m_optimize && expr->isConst) {
        std::string reg = allocReg();
        emit("li " + reg + ", " + std::to_string(expr->constValue));
        return reg;
    }

    if (m_optimize && expr->kind != ExprKind::CALL) {
        std::string reg = allocReg();
        std::string key = expressionKey(expr);
        auto existing = m_cseRegs.find(key);
        if (!key.empty() && existing != m_cseRegs.end()) {
            emit("mv " + reg + ", " + existing->second);
        } else {
            generateExprInto(expr, reg);
        }
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
                auto regIt = m_localRegByOffset.find(ie->varOffset);
                if (regIt != m_localRegByOffset.end()) {
                    emit("mv " + reg + ", " + regIt->second);
                } else {
                    emitLoadStack(reg, "fp", ie->varOffset);
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

            if (m_optimize) {
                auto emitShiftForMul = [&](const Expr* valueExpr, int factor) -> std::string {
                    int shift = 0;
                    int value = factor;
                    while (value > 1 && (value % 2) == 0) {
                        value /= 2;
                        shift++;
                    }
                    if (value == 1 && shift > 0) {
                        std::string reg = generateExpr(valueExpr);
                        emit("slli " + reg + ", " + reg + ", " + std::to_string(shift));
                        return reg;
                    }
                    return "";
                };

                if (be->right->isConst) {
                    int rv = be->right->constValue;
                    if ((be->op == TokenType::TOK_ADD || be->op == TokenType::TOK_SUB) && rv == 0) {
                        return generateExpr(be->left.get());
                    }
                    if ((be->op == TokenType::TOK_MUL || be->op == TokenType::TOK_DIV) && rv == 1) {
                        return generateExpr(be->left.get());
                    }
                    if (be->op == TokenType::TOK_MUL && rv > 1) {
                        std::string shifted = emitShiftForMul(be->left.get(), rv);
                        if (!shifted.empty()) {
                            return shifted;
                        }
                    }
                }

                if (be->left->isConst) {
                    int lv = be->left->constValue;
                    if (be->op == TokenType::TOK_ADD && lv == 0) {
                        return generateExpr(be->right.get());
                    }
                    if (be->op == TokenType::TOK_MUL && lv == 1) {
                        return generateExpr(be->right.get());
                    }
                    if (be->op == TokenType::TOK_MUL && lv > 1) {
                        std::string shifted = emitShiftForMul(be->right.get(), lv);
                        if (!shifted.empty()) {
                            return shifted;
                        }
                    }
                }
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
            std::string requestedResultReg = m_requestedCallResultReg;
            m_requestedCallResultReg.clear();

            std::vector<std::string> regsToSave = m_usedRegs;
            bool argumentsContainCalls = false;
            for (const auto& arg : ce->args) {
                argumentsContainCalls = argumentsContainCalls || exprHasCall(arg.get());
            }
            int outgoingBytes = ce->args.size() > 8
                ? static_cast<int>((ce->args.size() - 8) * 4)
                : 0;
            int tempSaveBase = outgoingBytes;
            int argSaveBase = tempSaveBase + static_cast<int>(regsToSave.size() * 4);
            int argSaveBytes = argumentsContainCalls
                ? static_cast<int>(std::min<size_t>(ce->args.size(), 8) * 4)
                : 0;
            int frameSize = alignTo(outgoingBytes +
                                    static_cast<int>(regsToSave.size() * 4) +
                                    argSaveBytes, 16);
            emitAdjustStack(-frameSize, "a7");

            // Save used temporary registers
            for (size_t i = 0; i < regsToSave.size(); i++) {
                emitStoreStack(regsToSave[i], "sp",
                               tempSaveBase + static_cast<int>(i * 4));
            }

            if (argumentsContainCalls) {
                // Nested calls can clobber argument registers, so stage these
                // arguments in the outgoing frame before loading a0-a7.
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
            } else {
                // The common case needs no argument stack round-trip.
                for (size_t i = 0; i < ce->args.size(); i++) {
                    if (i < 8) {
                        generateExprInto(ce->args[i].get(), "a" + std::to_string(i));
                    } else {
                        std::string argReg = generateExpr(ce->args[i].get());
                        emitStoreStack(argReg, "sp", static_cast<int>((i - 8) * 4));
                        freeReg(argReg);
                    }
                }
            }

            if (m_optimize) emitFlushCachedGlobals();

            // Call the function
            emit("call " + ce->funcName);

            if (m_optimize) emitLoadCachedGlobals();

            // Result is in a0
            std::string resultReg;
            if (!requestedResultReg.empty()) {
                resultReg = requestedResultReg;
                if (resultReg != "a0") emit("mv " + resultReg + ", a0");
            } else {
                resultReg = allocReg();
                emit("mv " + resultReg + ", a0");
            }

            // Restore used registers
            for (size_t i = 0; i < regsToSave.size(); i++) {
                emitLoadStack(regsToSave[i], "sp",
                              tempSaveBase + static_cast<int>(i * 4));
            }

            emitAdjustStack(frameSize, "a7");

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
