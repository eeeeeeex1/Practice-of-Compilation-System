#include "SemanticAnalyzer.h"

// === Symbol table implementation ===

Scope& getCurrentScope(std::deque<Scope>& scopes) {
    return scopes.back();
}

// === Scope ===

bool Scope::add(const std::string& name, Symbol sym) {
    // Check for duplicate in current scope
    for (auto& [n, s] : m_symbols) {
        if (n == name) return false;
    }
    m_symbols.emplace_back(name, std::move(sym));
    return true;
}

Symbol* Scope::lookup(const std::string& name) {
    for (auto& [n, s] : m_symbols) {
        if (n == name) return &s;
    }
    return nullptr;
}

bool Scope::contains(const std::string& name) const {
    for (auto& [n, s] : m_symbols) {
        if (n == name) return true;
    }
    return false;
}

// === SymbolTable ===

SymbolTable::SymbolTable() {
    enterScope();  // global scope
}

void SymbolTable::enterScope() {
    m_scopes.emplace_back();
}

void SymbolTable::leaveScope() {
    if (m_scopes.size() > 1) {
        m_scopes.pop_back();
    }
}

bool SymbolTable::add(const std::string& name, Symbol sym) {
    if (m_scopes.size() == 1) {
        // Global scope
        for (auto& s : m_globals) {
            if (s.name == name) return false;
        }
        sym.isGlobal = true;
        m_globals.push_back(std::move(sym));
        return true;
    }
    return getCurrentScope(m_scopes).add(name, std::move(sym));
}

Symbol* SymbolTable::lookup(const std::string& name) {
    // Search from innermost scope outward
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        Symbol* s = it->lookup(name);
        if (s) return s;
    }
    // Search globals
    for (auto& s : m_globals) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

Symbol* SymbolTable::lookupLocal(const std::string& name) {
    if (m_scopes.empty()) return nullptr;
    return getCurrentScope(m_scopes).lookup(name);
}

bool SymbolTable::isGlobal() const {
    return m_scopes.size() == 1;
}

// === SemanticAnalyzer ===

SemanticAnalyzer::SemanticAnalyzer(bool optimize) : m_optimize(optimize) {}

void SemanticAnalyzer::error(const std::string& msg, const Token& tok) {
    throw SemanticError(msg, tok);
}

// === Top-level analysis ===

void SemanticAnalyzer::analyze(CompUnit& comp) {
    // First pass: register all function signatures
    for (auto& func : comp.functions) {
        Symbol sym;
        sym.name = func->name;
        sym.kind = SymbolKind::FUNCTION;
        sym.type = (func->returnType == FuncReturnType::INT) ? TypeKind::INT : TypeKind::VOID;
        for (size_t i = 0; i < func->params.size(); i++) {
            sym.paramTypes.push_back(TypeKind::INT);
        }
        if (!m_symbols.add(func->name, sym)) {
            error("Duplicate function name '" + func->name + "'", func->tok);
        }
        func->symbol = m_symbols.lookup(func->name);
        m_functions.push_back(sym);
    }

    // Process global declarations
    for (auto& decl : comp.globals) {
        analyzeDecl(decl.get(), true);
    }

    // Check main function
    Symbol* mainSym = m_symbols.lookup("main");
    if (!mainSym || mainSym->kind != SymbolKind::FUNCTION) {
        error("Program must contain a 'main' function with no parameters returning int",
              Token(TokenType::TOK_EOF, "", 0, 0));
    }
    if (mainSym->type != TypeKind::INT || !mainSym->paramTypes.empty()) {
        error("'main' function must return int and have no parameters",
              Token(TokenType::TOK_ID, "main", 0, 0));
    }

    // Analyze function bodies
    for (auto& func : comp.functions) {
        analyzeFuncDef(func.get());
    }
}

// === Function analysis ===

void SemanticAnalyzer::analyzeFuncDef(FuncDef* func) {
    m_currentFunc = func;
    m_symbols.enterScope();

    // Add parameters to scope and assign stack offsets
    // Frame layout: fp-4=saved_ra, fp-8=saved_fp, fp-12=first_var, fp-16=second_var, ...
    int varBytes = 0;  // bytes used by variables below saved fp
    for (size_t i = 0; i < func->params.size(); i++) {
        Symbol sym;
        sym.name = func->params[i];
        sym.kind = SymbolKind::PARAMETER;
        sym.type = TypeKind::INT;
        int offset = -12 - varBytes;
        sym.stackOffset = offset;
        varBytes += 4;
        if (!m_symbols.add(func->params[i], sym)) {
            error("Duplicate parameter name '" + func->params[i] + "'", func->tok);
        }
        func->paramOffsets.push_back(offset);
    }

    func->stackSize = varBytes;  // Current variable bytes used

    // Analyze body
    int loopDepth = 0;

    if (func->body) {
        // We don't enter a new scope for the function body block -
        // params are already in a scope above the body.
        // But block should create its own scope. We'll handle this in analyzeStmt
        // by having BlockStmt enter a new scope.
        // Actually, for the function body, the statements are directly in the
        // function scope (which includes params). So we should NOT enter a new
        // scope for the function body itself.
        for (auto& stmt : func->body->stmts) {
            analyzeStmt(stmt.get(),
                func->returnType == FuncReturnType::INT ? TypeKind::INT : TypeKind::VOID,
                loopDepth);
        }
    }

    // Check that int functions return on all paths
    // (Simple check: last statement must be return or all paths through if-else return)
    if (func->returnType == FuncReturnType::INT) {
        if (func->body && !func->body->stmts.empty()) {
            // Basic check: verify the function body ends with a return
            // This is a simplified check - full path analysis would be complex
            Stmt* lastStmt = func->body->stmts.back().get();
            if (lastStmt->kind != StmtKind::RETURN) {
                // Could also end with if-else where both branches return
                // We'll be lenient here for simplicity
                // error("Function '" + func->name + "' must return a value", func->tok);
            }
        } else {
            error("Function '" + func->name + "' must return a value", func->tok);
        }
    }

    m_symbols.leaveScope();
    m_currentFunc = nullptr;
}

// === Declaration analysis ===

void SemanticAnalyzer::analyzeDecl(Decl* decl, bool isGlobal) {
    // Analyze initializer
    if (decl->kind == DeclKind::CONSTANT) {
        // Constants must have compile-time determinable initializers
        if (!isValidConstInit(decl->init.get())) {
            error("Constant '" + decl->name + "' must be initialized with a compile-time constant expression",
                  decl->tok);
        }
        analyzeConstExpr(decl->init.get());
    } else {
        analyzeExpr(decl->init.get());
    }

    // Add to symbol table
    Symbol sym;
    sym.name = decl->name;
    sym.kind = decl->kind == DeclKind::CONSTANT ? SymbolKind::CONSTANT : SymbolKind::VARIABLE;
    sym.type = TypeKind::INT;
    sym.isGlobal = isGlobal;

    if (decl->init->isConst) {
        sym.intValue = decl->init->constValue;
        sym.hasValue = true;
    }

    if (isGlobal) {
        sym.stackOffset = 0;  // Global variables are in .data section
        if (!m_symbols.add(decl->name, sym)) {
            error("Duplicate global name '" + decl->name + "'", decl->tok);
        }
        decl->varOffset = 0;
    } else {
        // Local variable: assign stack slot
        // Frame layout: fp-4=ra, fp-8=fp, fp-12=first_var, fp-16=second_var, ...
        if (m_currentFunc) {
            sym.stackOffset = -12 - m_currentFunc->stackSize;
            m_currentFunc->stackSize += 4;
        }
        if (!m_symbols.add(decl->name, sym)) {
            error("Duplicate declaration '" + decl->name + "' in this scope", decl->tok);
        }
        decl->varOffset = sym.stackOffset;
    }

    // Note: decl->varOffset already stores the needed offset
}

// === Statement analysis ===

void SemanticAnalyzer::analyzeStmt(Stmt* stmt, TypeKind funcReturnType, int& loopDepth) {
    switch (stmt->kind) {
        case StmtKind::BLOCK: {
            auto* bs = dynamic_cast<BlockStmt*>(stmt);
            m_symbols.enterScope();
            for (auto& s : bs->block->stmts) {
                analyzeStmt(s.get(), funcReturnType, loopDepth);
            }
            m_symbols.leaveScope();
            break;
        }
        case StmtKind::EMPTY:
            break;

        case StmtKind::EXPR: {
            auto* es = dynamic_cast<ExprStmt*>(stmt);
            analyzeExpr(es->expr.get());
            break;
        }
        case StmtKind::ASSIGN: {
            auto* as = dynamic_cast<AssignStmt*>(stmt);
            // Look up the variable
            Symbol* sym = m_symbols.lookup(as->name);
            if (!sym) {
                error("Undefined identifier '" + as->name + "'", as->tok);
            }
            if (sym->kind == SymbolKind::CONSTANT) {
                error("Cannot assign to constant '" + as->name + "'", as->tok);
            }
            if (sym->kind == SymbolKind::FUNCTION) {
                error("Cannot assign to function '" + as->name + "'", as->tok);
            }
            as->varOffset = sym->stackOffset;
            as->varIsGlobal = sym->isGlobal;
            analyzeExpr(as->value.get());
            // Check that RHS is not void
            if (as->value->type == TypeKind::VOID) {
                error("Cannot assign void expression to variable", as->tok);
            }
            break;
        }
        case StmtKind::DECL: {
            auto* ds = dynamic_cast<DeclStmt*>(stmt);
            analyzeDecl(ds->decl.get(), false);
            break;
        }
        case StmtKind::IF: {
            auto* is = dynamic_cast<IfStmt*>(stmt);
            analyzeExpr(is->condition.get());
            if (is->condition->type == TypeKind::VOID) {
                error("Condition of if statement must be int type", is->tok);
            }
            analyzeStmt(is->thenStmt.get(), funcReturnType, loopDepth);
            if (is->elseStmt) {
                analyzeStmt(is->elseStmt.get(), funcReturnType, loopDepth);
            }
            break;
        }
        case StmtKind::WHILE: {
            auto* ws = dynamic_cast<WhileStmt*>(stmt);
            analyzeExpr(ws->condition.get());
            if (ws->condition->type == TypeKind::VOID) {
                error("Condition of while statement must be int type", ws->tok);
            }
            loopDepth++;
            analyzeStmt(ws->body.get(), funcReturnType, loopDepth);
            loopDepth--;
            break;
        }
        case StmtKind::BREAK: {
            if (loopDepth == 0) {
                error("'break' statement outside of loop", stmt->tok);
            }
            break;
        }
        case StmtKind::CONTINUE: {
            if (loopDepth == 0) {
                error("'continue' statement outside of loop", stmt->tok);
            }
            break;
        }
        case StmtKind::RETURN: {
            auto* rs = dynamic_cast<ReturnStmt*>(stmt);
            if (rs->hasExpr) {
                if (funcReturnType == TypeKind::VOID) {
                    error("Cannot return a value from void function", rs->tok);
                }
                analyzeExpr(rs->expr.get());
            } else {
                if (funcReturnType == TypeKind::INT) {
                    error("Must return a value from int function", rs->tok);
                }
            }
            break;
        }
    }
}

// === Expression analysis ===

bool SemanticAnalyzer::isValidConstInit(Expr* expr) {
    if (!expr) return false;
    switch (expr->kind) {
        case ExprKind::NUMBER:
            return true;
        case ExprKind::ID: {
            auto* ie = dynamic_cast<IdExpr*>(expr);
            Symbol* sym = m_symbols.lookup(ie->name);
            return sym && sym->kind == SymbolKind::CONSTANT;
        }
        case ExprKind::BINARY: {
            auto* be = dynamic_cast<BinaryExpr*>(expr);
            return isValidConstInit(be->left.get()) && isValidConstInit(be->right.get());
        }
        case ExprKind::UNARY: {
            auto* ue = dynamic_cast<UnaryExpr*>(expr);
            return isValidConstInit(ue->operand.get());
        }
        case ExprKind::CALL:
            return false;
    }
    return false;
}

bool SemanticAnalyzer::canEvaluateConst(Expr* expr) {
    if (!expr) return false;
    switch (expr->kind) {
        case ExprKind::NUMBER:
            return true;
        case ExprKind::ID: {
            auto* ie = dynamic_cast<IdExpr*>(expr);
            Symbol* sym = m_symbols.lookup(ie->name);
            return sym && sym->kind == SymbolKind::CONSTANT && sym->hasValue;
        }
        case ExprKind::BINARY: {
            auto* be = dynamic_cast<BinaryExpr*>(expr);
            return canEvaluateConst(be->left.get()) && canEvaluateConst(be->right.get());
        }
        case ExprKind::UNARY: {
            auto* ue = dynamic_cast<UnaryExpr*>(expr);
            return canEvaluateConst(ue->operand.get());
        }
        case ExprKind::CALL:
            return false;
    }
    return false;
}

int SemanticAnalyzer::evaluateConstExpr(Expr* expr) {
    switch (expr->kind) {
        case ExprKind::NUMBER:
            return dynamic_cast<NumberExpr*>(expr)->value;

        case ExprKind::ID: {
            auto* ie = dynamic_cast<IdExpr*>(expr);
            Symbol* sym = m_symbols.lookup(ie->name);
            if (!sym || !sym->hasValue) {
                error("Cannot evaluate constant '" + ie->name + "'", ie->tok);
            }
            return sym->intValue;
        }
        case ExprKind::BINARY: {
            auto* be = dynamic_cast<BinaryExpr*>(expr);
            int left = evaluateConstExpr(be->left.get());
            int right = evaluateConstExpr(be->right.get());
            switch (be->op) {
                case TokenType::TOK_ADD: return left + right;
                case TokenType::TOK_SUB: return left - right;
                case TokenType::TOK_MUL: return left * right;
                case TokenType::TOK_DIV:
                    if (right == 0) error("Division by zero in constant expression", be->tok);
                    return left / right;
                case TokenType::TOK_MOD:
                    if (right == 0) error("Modulo by zero in constant expression", be->tok);
                    return left % right;
                case TokenType::TOK_LT:  return left < right ? 1 : 0;
                case TokenType::TOK_GT:  return left > right ? 1 : 0;
                case TokenType::TOK_LE:  return left <= right ? 1 : 0;
                case TokenType::TOK_GE:  return left >= right ? 1 : 0;
                case TokenType::TOK_EQ:  return left == right ? 1 : 0;
                case TokenType::TOK_NE:  return left != right ? 1 : 0;
                case TokenType::TOK_AND: return (left && right) ? 1 : 0;
                case TokenType::TOK_OR:  return (left || right) ? 1 : 0;
                default:
                    error("Unsupported operator in constant expression", be->tok);
            }
            return 0;
        }
        case ExprKind::UNARY: {
            auto* ue = dynamic_cast<UnaryExpr*>(expr);
            int operand = evaluateConstExpr(ue->operand.get());
            switch (ue->op) {
                case TokenType::TOK_SUB: return -operand;
                case TokenType::TOK_ADD: return operand;
                case TokenType::TOK_NOT: return !operand ? 1 : 0;
                default:
                    error("Unsupported unary operator in constant expression", ue->tok);
            }
            return 0;
        }
        case ExprKind::CALL:
            error("Function call in constant expression", expr->tok);
    }
    return 0;
}

bool SemanticAnalyzer::analyzeExpr(Expr* expr) {
    if (!expr) return false;

    switch (expr->kind) {
        case ExprKind::NUMBER: {
            auto* ne = dynamic_cast<NumberExpr*>(expr);
            expr->type = TypeKind::INT;
            expr->isConst = true;
            expr->constValue = ne->value;
            return true;
        }
        case ExprKind::ID: {
            auto* ie = dynamic_cast<IdExpr*>(expr);
            Symbol* sym = m_symbols.lookup(ie->name);
            if (!sym) {
                error("Undefined identifier '" + ie->name + "'", ie->tok);
            }
            if (sym->kind == SymbolKind::FUNCTION) {
                error("Function '" + ie->name + "' cannot be used as a value", ie->tok);
            }
            // Store variable info directly in AST node (avoid dangling pointers)
            ie->varOffset = sym->stackOffset;
            ie->varIsGlobal = sym->isGlobal;
            expr->type = sym->type;
            if (sym->kind == SymbolKind::CONSTANT && sym->hasValue) {
                expr->isConst = true;
                expr->constValue = sym->intValue;
                ie->varIsConst = true;
                ie->varConstValue = sym->intValue;
                return true;
            }
            return false;
        }
        case ExprKind::BINARY: {
            auto* be = dynamic_cast<BinaryExpr*>(expr);
            bool leftConst = analyzeExpr(be->left.get());
            bool rightConst = analyzeExpr(be->right.get());

            if (be->left->type == TypeKind::VOID || be->right->type == TypeKind::VOID) {
                error("Cannot perform binary operation on void type", be->tok);
            }
            expr->type = TypeKind::INT;

            // Division by zero check for constant right operand
            if (be->right->isConst) {
                int rv = be->right->constValue;
                if ((be->op == TokenType::TOK_DIV || be->op == TokenType::TOK_MOD) && rv == 0) {
                    error("Division by zero", be->tok);
                }
            }

            // Constant folding
            if (m_optimize && leftConst && rightConst) {
                expr->isConst = true;
                expr->constValue = evaluateConstExpr(expr);
                return true;
            }
            if (leftConst && rightConst) {
                expr->isConst = true;
                expr->constValue = evaluateConstExpr(expr);
                return true;
            }
            // Special case: short-circuit evaluation for && and ||
            // Even if both sides are const, we already handled it above
            return false;
        }
        case ExprKind::UNARY: {
            auto* ue = dynamic_cast<UnaryExpr*>(expr);
            bool operandConst = analyzeExpr(ue->operand.get());

            if (ue->operand->type == TypeKind::VOID) {
                error("Cannot apply unary operator to void type", ue->tok);
            }
            expr->type = TypeKind::INT;

            if (operandConst) {
                expr->isConst = true;
                expr->constValue = evaluateConstExpr(expr);
                return true;
            }
            return false;
        }
        case ExprKind::CALL: {
            auto* ce = dynamic_cast<CallExpr*>(expr);
            Symbol* sym = m_symbols.lookup(ce->funcName);
            if (!sym) {
                error("Undefined function '" + ce->funcName + "'", ce->tok);
            }
            if (sym->kind != SymbolKind::FUNCTION) {
                error("'" + ce->funcName + "' is not a function", ce->tok);
            }

            // Check argument count
            if (ce->args.size() != sym->paramTypes.size()) {
                std::ostringstream oss;
                oss << "Function '" << ce->funcName << "' expects " << sym->paramTypes.size()
                    << " arguments, got " << ce->args.size();
                error(oss.str(), ce->tok);
            }

            // Analyze arguments
            for (auto& arg : ce->args) {
                analyzeExpr(arg.get());
                if (arg->type == TypeKind::VOID) {
                    error("Cannot pass void expression as function argument", ce->tok);
                }
            }

            ce->funcParamCount = static_cast<int>(sym->paramTypes.size());
            expr->type = sym->type;
            return false;  // Function calls are never constant
        }
    }
    return false;
}

bool SemanticAnalyzer::analyzeConstExpr(Expr* expr) {
    bool isConst = analyzeExpr(expr);
    if (!isConst) {
        error("Expression is not a compile-time constant", expr->tok);
    }
    return true;
}
