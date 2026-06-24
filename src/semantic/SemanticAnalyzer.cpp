#include "semantic/SemanticAnalyzer.h"
#include "semantic/ConstEvaluator.h"
#include <algorithm>

namespace semantic {

// =============================================================================
// SemanticResult
// =============================================================================
void SemanticResult::addError(const std::string& msg) {
    success = false;
    errors.push_back(msg);
}

void SemanticResult::addWarning(const std::string& msg) {
    warnings.push_back(msg);
}

// =============================================================================
// SemanticAnalyzer
// =============================================================================
SemanticAnalyzer::SemanticAnalyzer() = default;

SemanticResult SemanticAnalyzer::analyze(const CompUnit& compUnit) {
    result_ = SemanticResult{};
    symTable_ = SymbolTable{};
    hasMainFunction_ = false;

    // First pass: register all top-level symbols (function declarations, global vars)
    for (const auto& item : compUnit.items) {
        std::visit(overloaded{
            [&](const FuncDef& funcDef) {
                // Check for duplicate function definition
                if (symTable_.lookupCurrentScope(funcDef.name)) {
                    result_.addError("Function '" + funcDef.name +
                                     "' is already defined in this scope");
                    return;
                }
                auto funcSym = std::make_unique<FunctionSymbol>(
                    funcDef.name, funcDef.returnType, funcDef.params, true);
                symTable_.insert(std::move(funcSym));

                if (funcDef.name == "main") {
                    hasMainFunction_ = true;
                }
            },
            [&](const ConstDecl& decl) {
                if (symTable_.lookupCurrentScope(decl.name)) {
                    result_.addError("Constant '" + decl.name +
                                     "' is already defined in this scope");
                    return;
                }
                auto varSym = std::make_unique<VariableSymbol>(
                    decl.name, TypeKind::Int, true, false);
                symTable_.insert(std::move(varSym));
            },
            [&](const VarDecl& decl) {
                if (symTable_.lookupCurrentScope(decl.name)) {
                    result_.addError("Variable '" + decl.name +
                                     "' is already defined in this scope");
                    return;
                }
                auto varSym = std::make_unique<VariableSymbol>(
                    decl.name, TypeKind::Int, false, false);
                symTable_.insert(std::move(varSym));
            }
        }, item);
    }

    // Second pass: analyze bodies
    for (const auto& item : compUnit.items) {
        analyzeTopLevel(item);
    }

    // Final checks
    checkMainFunction();

    return result_;
}

// =============================================================================
// Top-level analysis
// =============================================================================
void SemanticAnalyzer::analyzeTopLevel(const TopLevel& item) {
    std::visit(overloaded{
        [&](const FuncDef& funcDef) { analyzeFuncDef(funcDef); },
        [&](const ConstDecl& decl) { analyzeConstDecl(decl); },
        [&](const VarDecl& decl) { analyzeVarDecl(decl); }
    }, item);
}

void SemanticAnalyzer::analyzeFuncDef(const FuncDef& funcDef) {
    // Check main function signature
    if (funcDef.name == "main") {
        if (funcDef.returnType != TypeKind::Int) {
            result_.addError("Function 'main' must return 'int'");
        }
        if (!funcDef.params.empty()) {
            result_.addError("Function 'main' must have no parameters");
        }
    }

    // Enter function scope
    symTable_.enterScope(Scope::ScopeKind::Function);
    currentFunctionReturnType_ = funcDef.returnType;
    hasReturnStatement_ = false;

    // Register parameters
    for (const auto& param : funcDef.params) {
        if (symTable_.lookupCurrentScope(param.name)) {
            result_.addError("Parameter '" + param.name +
                             "' is already defined in function '" +
                             funcDef.name + "'");
            continue;
        }
        auto paramSym = std::make_unique<VariableSymbol>(
            param.name, TypeKind::Int, false, true);
        symTable_.insert(std::move(paramSym));
    }

    // Analyze function body
    analyzeBlock(funcDef.body);

    // Check return path for int functions
    if (funcDef.returnType == TypeKind::Int) {
        if (!hasReturnStatement_) {
            result_.addWarning("Function '" + funcDef.name +
                               "' has 'int' return type but no return statement");
        }
    }

    currentFunctionReturnType_ = TypeKind::Void;
    hasReturnStatement_ = false;
    symTable_.exitScope();
}

void SemanticAnalyzer::analyzeConstDecl(const ConstDecl& decl) {
    // Evaluate init expression
    TypeKind initType = analyzeExpr(*decl.init);

    if (initType != TypeKind::Int) {
        result_.addError("Constant '" + decl.name +
                         "' must be initialized with an integer expression");
        return;
    }

    // Try compile-time evaluation
    auto constVal = ConstEvaluator::evaluate(*decl.init, symTable_);
    if (!constVal) {
        result_.addError("Constant '" + decl.name +
                         "' initializer is not a compile-time constant");
        return;
    }

    // Store the constant value
    Symbol* sym = symTable_.lookupCurrentScope(decl.name);
    if (sym && sym->kind == SymbolKind::Const) {
        auto* varSym = static_cast<VariableSymbol*>(sym);
        varSym->constValue = *constVal;
    }
}

void SemanticAnalyzer::analyzeVarDecl(const VarDecl& decl) {
    TypeKind initType = analyzeExpr(*decl.init);

    if (initType != TypeKind::Int) {
        result_.addError("Variable '" + decl.name +
                         "' must be initialized with an integer expression");
    }
}

// =============================================================================
// Statement analysis
// =============================================================================
void SemanticAnalyzer::analyzeStmt(const Stmt& stmt) {
    std::visit(overloaded{
        [&](const Block& s) { analyzeBlock(s); },
        [&](const IfStmt& s) { analyzeIfStmt(s); },
        [&](const WhileStmt& s) { analyzeWhileStmt(s); },
        [&](const BreakStmt& s) { analyzeBreakStmt(s); },
        [&](const ContinueStmt& s) { analyzeContinueStmt(s); },
        [&](const ReturnStmt& s) { analyzeReturnStmt(s); },
        [&](const ExprStmt& s) { analyzeExprStmt(s); },
        [&](const AssignStmt& s) { analyzeAssignStmt(s); },
        [&](const EmptyStmt& /*s*/) { analyzeEmptyStmt({}); },
        [&](const ConstDecl& s) {
            // Const declaration inside a block
            if (symTable_.lookupCurrentScope(s.name)) {
                result_.addError("Constant '" + s.name +
                                 "' is already defined in this scope");
                return;
            }
            auto varSym = std::make_unique<VariableSymbol>(
                s.name, TypeKind::Int, true, false);
            symTable_.insert(std::move(varSym));
            analyzeConstDecl(s);
        },
        [&](const VarDecl& s) {
            // Variable declaration inside a block
            if (symTable_.lookupCurrentScope(s.name)) {
                result_.addError("Variable '" + s.name +
                                 "' is already defined in this scope");
                return;
            }
            auto varSym = std::make_unique<VariableSymbol>(
                s.name, TypeKind::Int, false, false);
            symTable_.insert(std::move(varSym));
            analyzeVarDecl(s);
        }
    }, stmt);
}

void SemanticAnalyzer::analyzeBlock(const Block& block) {
    symTable_.enterScope(Scope::ScopeKind::Block);

    for (const auto& stmt : block.stmts) {
        analyzeStmt(*stmt);
    }

    symTable_.exitScope();
}

void SemanticAnalyzer::analyzeIfStmt(const IfStmt& stmt) {
    TypeKind condType = analyzeExpr(*stmt.cond);
    if (condType != TypeKind::Int) {
        result_.addError("Condition in 'if' statement must be an integer expression");
    }

    analyzeStmt(*stmt.thenStmt);

    if (stmt.elseStmt) {
        analyzeStmt(*stmt.elseStmt);
    }
}

void SemanticAnalyzer::analyzeWhileStmt(const WhileStmt& stmt) {
    TypeKind condType = analyzeExpr(*stmt.cond);
    if (condType != TypeKind::Int) {
        result_.addError("Condition in 'while' statement must be an integer expression");
    }

    symTable_.enterScope(Scope::ScopeKind::Loop);
    analyzeStmt(*stmt.body);
    symTable_.exitScope();
}

void SemanticAnalyzer::analyzeBreakStmt(const BreakStmt& /*stmt*/) {
    if (!symTable_.isInLoop()) {
        result_.addError("'break' statement is not inside a loop");
    }
}

void SemanticAnalyzer::analyzeContinueStmt(const ContinueStmt& /*stmt*/) {
    if (!symTable_.isInLoop()) {
        result_.addError("'continue' statement is not inside a loop");
    }
}

void SemanticAnalyzer::analyzeReturnStmt(const ReturnStmt& stmt) {
    if (!symTable_.isInFunction()) {
        result_.addError("'return' statement is outside of any function");
        return;
    }

    hasReturnStatement_ = true;

    if (currentFunctionReturnType_ == TypeKind::Int) {
        if (!stmt.value) {
            result_.addError("Function returning 'int' must return a value");
        } else {
            TypeKind retType = analyzeExpr(*stmt.value);
            if (retType != TypeKind::Int) {
                result_.addError("Return value must be an integer expression");
            }
        }
    } else {
        // void function
        if (stmt.value) {
            result_.addError("Function returning 'void' must not return a value");
        }
    }
}

void SemanticAnalyzer::analyzeExprStmt(const ExprStmt& stmt) {
    analyzeExpr(*stmt.expr);
}

void SemanticAnalyzer::analyzeAssignStmt(const AssignStmt& stmt) {
    // Check that the variable exists
    Symbol* sym = symTable_.lookup(stmt.name);
    if (!sym) {
        result_.addError("Variable '" + stmt.name + "' is not declared");
        return;
    }

    // Check that it's a variable (not a const or function)
    if (sym->kind == SymbolKind::Const) {
        result_.addError("Cannot assign to constant '" + stmt.name + "'");
        return;
    }
    if (sym->kind == SymbolKind::Function) {
        result_.addError("'" + stmt.name + "' is a function, not a variable");
        return;
    }

    // Check the value type
    TypeKind valueType = analyzeExpr(*stmt.value);
    if (valueType != TypeKind::Int) {
        result_.addError("Assignment value must be an integer expression");
    }
}

void SemanticAnalyzer::analyzeEmptyStmt(const EmptyStmt& /*stmt*/) {
    // Empty statement is always valid
}

// =============================================================================
// Expression analysis
// =============================================================================
TypeKind SemanticAnalyzer::analyzeExpr(const Expr& expr) {
    return std::visit(overloaded{
        [&](const NumberExpr& e) { return analyzeNumberExpr(e); },
        [&](const IdExpr& e) { return analyzeIdExpr(e); },
        [&](const BinaryExpr& e) { return analyzeBinaryExpr(e); },
        [&](const UnaryExpr& e) { return analyzeUnaryExpr(e); },
        [&](const CallExpr& e) { return analyzeCallExpr(e); }
    }, expr);
}

TypeKind SemanticAnalyzer::analyzeNumberExpr(const NumberExpr& /*expr*/) {
    return TypeKind::Int;
}

TypeKind SemanticAnalyzer::analyzeIdExpr(const IdExpr& expr) {
    Symbol* sym = symTable_.lookup(expr.name);
    if (!sym) {
        result_.addError("Identifier '" + expr.name + "' is not declared");
        return TypeKind::Int; // Assume int for error recovery
    }

    if (sym->kind == SymbolKind::Function) {
        result_.addError("Function '" + expr.name +
                         "' is used as an expression (missing function call?)");
        return TypeKind::Int;
    }

    // Variable, const, or param
    return TypeKind::Int;
}

TypeKind SemanticAnalyzer::analyzeBinaryExpr(const BinaryExpr& expr) {
    TypeKind leftType = analyzeExpr(*expr.left);
    TypeKind rightType = analyzeExpr(*expr.right);

    if (leftType != TypeKind::Int) {
        result_.addError("Left operand of binary expression must be integer");
    }
    if (rightType != TypeKind::Int) {
        result_.addError("Right operand of binary expression must be integer");
    }

    return TypeKind::Int;
}

TypeKind SemanticAnalyzer::analyzeUnaryExpr(const UnaryExpr& expr) {
    TypeKind operandType = analyzeExpr(*expr.operand);

    if (operandType != TypeKind::Int) {
        result_.addError("Operand of unary expression must be integer");
    }

    return TypeKind::Int;
}

TypeKind SemanticAnalyzer::analyzeCallExpr(const CallExpr& expr) {
    Symbol* sym = symTable_.lookup(expr.name);
    if (!sym) {
        result_.addError("Function '" + expr.name + "' is not declared");
        return TypeKind::Int;
    }

    if (sym->kind != SymbolKind::Function) {
        result_.addError("'" + expr.name +
                         "' is not a function and cannot be called");
        return TypeKind::Int;
    }

    auto* funcSym = static_cast<FunctionSymbol*>(sym);

    // Check argument count
    if (expr.args.size() != funcSym->params.size()) {
        result_.addError("Function '" + expr.name + "' expects " +
                         std::to_string(funcSym->params.size()) +
                         " arguments, but " +
                         std::to_string(expr.args.size()) + " were provided");
    }

    // Check argument types
    for (size_t i = 0; i < expr.args.size() && i < funcSym->params.size(); ++i) {
        TypeKind argType = analyzeExpr(*expr.args[i]);
        if (argType != TypeKind::Int) {
            result_.addError("Argument " + std::to_string(i + 1) +
                             " of function '" + expr.name +
                             "' must be an integer expression");
        }
    }

    return funcSym->returnType;
}

// =============================================================================
// Helper methods
// =============================================================================
void SemanticAnalyzer::checkMainFunction() {
    if (!hasMainFunction_) {
        result_.addError("Program must contain a 'main' function");
    }
}

bool SemanticAnalyzer::isBooleanOp(BinaryOp op) const {
    return op == BinaryOp::And || op == BinaryOp::Or;
}

bool SemanticAnalyzer::isComparisonOp(BinaryOp op) const {
    return op == BinaryOp::Lt || op == BinaryOp::Gt ||
           op == BinaryOp::Le || op == BinaryOp::Ge ||
           op == BinaryOp::Eq || op == BinaryOp::Ne;
}

bool SemanticAnalyzer::isArithmeticOp(BinaryOp op) const {
    return op == BinaryOp::Add || op == BinaryOp::Sub ||
           op == BinaryOp::Mul || op == BinaryOp::Div || op == BinaryOp::Mod;
}

} // namespace semantic