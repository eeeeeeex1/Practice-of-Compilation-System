#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <cassert>

#include "ast/AST.h"
#include "semantic/SemanticAnalyzer.h"

// =============================================================================
// Helper functions for building AST nodes
// =============================================================================

// Expressions
std::unique_ptr<Expr> makeNumber(int value) {
    return std::make_unique<Expr>(NumberExpr{value});
}

std::unique_ptr<Expr> makeId(const std::string& name) {
    return std::make_unique<Expr>(IdExpr{name});
}

std::unique_ptr<Expr> makeBinary(BinaryOp op, std::unique_ptr<Expr> left, std::unique_ptr<Expr> right) {
    BinaryExpr bin;
    bin.op = op;
    bin.left = std::move(left);
    bin.right = std::move(right);
    return std::make_unique<Expr>(std::move(bin));
}

std::unique_ptr<Expr> makeUnary(UnaryOp op, std::unique_ptr<Expr> operand) {
    UnaryExpr un;
    un.op = op;
    un.operand = std::move(operand);
    return std::make_unique<Expr>(std::move(un));
}

template<typename... Args>
std::unique_ptr<Expr> makeCall(const std::string& name, Args&&... args) {
    std::vector<std::unique_ptr<Expr>> v;
    v.reserve(sizeof...(Args));
    (v.push_back(std::forward<Args>(args)), ...);
    return std::make_unique<Expr>(CallExpr{name, std::move(v)});
}

// Statements
std::unique_ptr<Stmt> makeReturn(std::unique_ptr<Expr> value) {
    return std::make_unique<Stmt>(ReturnStmt{std::move(value)});
}

std::unique_ptr<Stmt> makeEmptyReturn() {
    return std::make_unique<Stmt>(ReturnStmt{nullptr});
}

std::unique_ptr<Stmt> makeExprStmt(std::unique_ptr<Expr> expr) {
    return std::make_unique<Stmt>(ExprStmt{std::move(expr)});
}

std::unique_ptr<Stmt> makeAssign(const std::string& name, std::unique_ptr<Expr> value) {
    return std::make_unique<Stmt>(AssignStmt{name, std::move(value)});
}

std::unique_ptr<Stmt> makeBreak() {
    return std::make_unique<Stmt>(BreakStmt{});
}

std::unique_ptr<Stmt> makeContinue() {
    return std::make_unique<Stmt>(ContinueStmt{});
}

std::unique_ptr<Stmt> makeEmpty() {
    return std::make_unique<Stmt>(EmptyStmt{});
}

std::unique_ptr<Stmt> makeIf(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> thenStmt, std::unique_ptr<Stmt> elseStmt = nullptr) {
    IfStmt ifs;
    ifs.cond = std::move(cond);
    ifs.thenStmt = std::move(thenStmt);
    ifs.elseStmt = std::move(elseStmt);
    return std::make_unique<Stmt>(std::move(ifs));
}

std::unique_ptr<Stmt> makeWhile(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> body) {
    WhileStmt ws;
    ws.cond = std::move(cond);
    ws.body = std::move(body);
    return std::make_unique<Stmt>(std::move(ws));
}

std::unique_ptr<Stmt> makeConstDecl(const std::string& name, std::unique_ptr<Expr> init) {
    return std::make_unique<Stmt>(ConstDecl{name, std::move(init)});
}

std::unique_ptr<Stmt> makeVarDecl(const std::string& name, std::unique_ptr<Expr> init) {
    return std::make_unique<Stmt>(VarDecl{name, std::move(init)});
}

// Build a FuncDef manually
FuncDef makeFuncDef(const std::string& name, TypeKind returnType, std::vector<Param> params, Block body) {
    FuncDef f;
    f.name = name;
    f.returnType = returnType;
    f.params = std::move(params);
    f.body = std::move(body);
    return f;
}

// Build a CompUnit by adding items one at a time
struct CompUnitBuilder {
    CompUnit cu;
    
    CompUnitBuilder& add(TopLevel item) {
        cu.items.push_back(std::move(item));
        return *this;
    }
    
    CompUnit build() {
        return std::move(cu);
    }
};

// =============================================================================
// Test framework
// =============================================================================

int totalTests = 0;
int passedTests = 0;

void runTest(const std::string& name, const CompUnit& compUnit,
             bool expectSuccess, const std::vector<std::string>& expectedErrors = {}) {
    totalTests++;
    semantic::SemanticAnalyzer analyzer;
    semantic::SemanticResult result = analyzer.analyze(compUnit);

    bool passed = true;
    std::string msg;

    if (expectSuccess) {
        if (!result.success) {
            passed = false;
            msg = "Expected success but got errors:";
            for (const auto& e : result.errors) {
                msg += " [" + e + "]";
            }
        }
    } else {
        if (result.success) {
            passed = false;
            msg = "Expected failure but analysis succeeded";
        } else {
            for (const auto& expected : expectedErrors) {
                bool found = false;
                for (const auto& actual : result.errors) {
                    if (actual.find(expected) != std::string::npos) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    passed = false;
                    msg += "Missing expected error: '" + expected + "'. ";
                }
            }
            if (!passed && msg.empty()) {
                msg = "Errors: ";
                for (const auto& e : result.errors) {
                    msg += "[" + e + "] ";
                }
            }
        }
    }

    if (passed) {
        passedTests++;
        std::cout << "  [PASS] " << name << std::endl;
    } else {
        std::cout << "  [FAIL] " << name << ": " << msg << std::endl;
    }
}

// =============================================================================
// Helper to build a simple Block
// =============================================================================

// For a single-statement block
Block block1(std::unique_ptr<Stmt> s1) {
    Block b;
    b.stmts.push_back(std::move(s1));
    return b;
}

// For a two-statement block
Block block2(std::unique_ptr<Stmt> s1, std::unique_ptr<Stmt> s2) {
    Block b;
    b.stmts.push_back(std::move(s1));
    b.stmts.push_back(std::move(s2));
    return b;
}

// For a three-statement block
Block block3(std::unique_ptr<Stmt> s1, std::unique_ptr<Stmt> s2, std::unique_ptr<Stmt> s3) {
    Block b;
    b.stmts.push_back(std::move(s1));
    b.stmts.push_back(std::move(s2));
    b.stmts.push_back(std::move(s3));
    return b;
}

// For a block with many statements
Block blockN(std::vector<std::unique_ptr<Stmt>> stmts) {
    Block b;
    b.stmts = std::move(stmts);
    return b;
}

// =============================================================================
// Positive Test Cases (should pass)
// =============================================================================

void testValidProgram() {
    // int main() { return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Valid program with int main()", cu, true);
}

void testBreakInLoop() {
    // int main() { while (1) { break; } return 0; }
    auto whileBody = block1(makeBreak());
    auto whileStmt = makeWhile(makeNumber(1), std::make_unique<Stmt>(std::move(whileBody)));
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block2(std::move(whileStmt), makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("break inside loop (valid)", cu, true);
}

void testContinueInLoop() {
    // int main() { while (1) { continue; } return 0; }
    auto whileBody = block1(makeContinue());
    auto whileStmt = makeWhile(makeNumber(1), std::make_unique<Stmt>(std::move(whileBody)));
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block2(std::move(whileStmt), makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("continue inside loop (valid)", cu, true);
}

void testFunctionCallCorrect() {
    // int foo(int x, int y) { return x + y; }
    // int main() { return foo(1, 2); }
    std::vector<Param> fooParams;
    fooParams.push_back(Param{"x"});
    fooParams.push_back(Param{"y"});
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Int, std::move(fooParams),
        block1(makeReturn(makeBinary(BinaryOp::Add, makeId("x"), makeId("y")))))));
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeCall("foo",
            std::make_unique<Expr>(NumberExpr{1}),
            std::make_unique<Expr>(NumberExpr{2})))))));
    auto cu = cb.build();
    runTest("Function call with correct arguments", cu, true);
}

void testConstValid() {
    // int main() { const int x = 1 + 2; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block2(
            makeConstDecl("x", makeBinary(BinaryOp::Add, makeNumber(1), makeNumber(2))),
            makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Valid const declaration", cu, true);
}

void testVariableShadowing() {
    // int main() { int x = 1; { int x = 2; } return 0; }
    auto nestedBlock = block1(makeVarDecl("x", makeNumber(2)));
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeVarDecl("x", makeNumber(1)),
            std::make_unique<Stmt>(std::move(nestedBlock)),
            makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Variable shadowing in nested block", cu, true);
}

void testValidVariableUsage() {
    // int main() { int x = 1; int y = x + 1; return y; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeVarDecl("x", makeNumber(1)),
            makeVarDecl("y", makeBinary(BinaryOp::Add, makeId("x"), makeNumber(1))),
            makeReturn(makeId("y"))))));
    auto cu = cb.build();
    runTest("Valid variable usage", cu, true);
}

void testIfElse() {
    // int main() { int x = 1; if (x < 2) { x = 3; } else { x = 4; } return x; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeVarDecl("x", makeNumber(1)),
            makeIf(
                makeBinary(BinaryOp::Lt, makeId("x"), makeNumber(2)),
                std::make_unique<Stmt>(block1(makeAssign("x", makeNumber(3)))),
                std::make_unique<Stmt>(block1(makeAssign("x", makeNumber(4))))),
            makeReturn(makeId("x"))))));
    auto cu = cb.build();
    runTest("If-else statement", cu, true);
}

void testWhileLoop() {
    // int main() { int x = 0; while (x < 10) { x = x + 1; } return x; }
    auto whileBody = block1(makeAssign("x", makeBinary(BinaryOp::Add, makeId("x"), makeNumber(1))));
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeVarDecl("x", makeNumber(0)),
            makeWhile(
                makeBinary(BinaryOp::Lt, makeId("x"), makeNumber(10)),
                std::make_unique<Stmt>(std::move(whileBody))),
            makeReturn(makeId("x"))))));
    auto cu = cb.build();
    runTest("While loop", cu, true);
}

void testGlobalVariable() {
    // int g = 42;
    // int main() { return g; }
    CompUnitBuilder cb;
    cb.add(TopLevel(VarDecl{"g", std::make_unique<Expr>(NumberExpr{42})}));
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeId("g"))))));
    auto cu = cb.build();
    runTest("Global variable", cu, true);
}

void testGlobalConst() {
    // const int C = 100;
    // int main() { return C; }
    CompUnitBuilder cb;
    cb.add(TopLevel(ConstDecl{"C", std::make_unique<Expr>(NumberExpr{100})}));
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeId("C"))))));
    auto cu = cb.build();
    runTest("Global const", cu, true);
}

void testFunctionCallAsExpression() {
    // int foo() { return 42; }
    // int main() { return foo(); }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Int, {},
        block1(makeReturn(makeNumber(42))))));
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeCall("foo"))))));
    auto cu = cb.build();
    runTest("Function call as expression", cu, true);
}

void testNestedBlocks() {
    // int main() { int x = 1; { int y = 2; x = y; } return x; }
    auto nestedBlock = block2(
        makeVarDecl("y", makeNumber(2)),
        makeAssign("x", makeId("y")));
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeVarDecl("x", makeNumber(1)),
            std::make_unique<Stmt>(std::move(nestedBlock)),
            makeReturn(makeId("x"))))));
    auto cu = cb.build();
    runTest("Nested blocks", cu, true);
}

void testUnaryOperators() {
    // int main() { int x = -5; int y = !0; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeVarDecl("x", makeUnary(UnaryOp::Neg, makeNumber(5))),
            makeVarDecl("y", makeUnary(UnaryOp::Not, makeNumber(0))),
            makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Unary operators", cu, true);
}

void testAllOperators() {
    // int main() { int a=1+2; int b=3-1; int c=2*3; int d=6/2; int e=7%3;
    //              int f=1<2; int g=1>2; int h=1<=2; int i=1>=2;
    //              int j=1==2; int k=1!=2; int l=1&&0; int m=1||0; return 0; }
    std::vector<std::unique_ptr<Stmt>> stmts;
    stmts.push_back(makeVarDecl("a", makeBinary(BinaryOp::Add, makeNumber(1), makeNumber(2))));
    stmts.push_back(makeVarDecl("b", makeBinary(BinaryOp::Sub, makeNumber(3), makeNumber(1))));
    stmts.push_back(makeVarDecl("c", makeBinary(BinaryOp::Mul, makeNumber(2), makeNumber(3))));
    stmts.push_back(makeVarDecl("d", makeBinary(BinaryOp::Div, makeNumber(6), makeNumber(2))));
    stmts.push_back(makeVarDecl("e", makeBinary(BinaryOp::Mod, makeNumber(7), makeNumber(3))));
    stmts.push_back(makeVarDecl("f", makeBinary(BinaryOp::Lt, makeNumber(1), makeNumber(2))));
    stmts.push_back(makeVarDecl("g", makeBinary(BinaryOp::Gt, makeNumber(1), makeNumber(2))));
    stmts.push_back(makeVarDecl("h", makeBinary(BinaryOp::Le, makeNumber(1), makeNumber(2))));
    stmts.push_back(makeVarDecl("i", makeBinary(BinaryOp::Ge, makeNumber(1), makeNumber(2))));
    stmts.push_back(makeVarDecl("j", makeBinary(BinaryOp::Eq, makeNumber(1), makeNumber(2))));
    stmts.push_back(makeVarDecl("k", makeBinary(BinaryOp::Ne, makeNumber(1), makeNumber(2))));
    stmts.push_back(makeVarDecl("l", makeBinary(BinaryOp::And, makeNumber(1), makeNumber(0))));
    stmts.push_back(makeVarDecl("m", makeBinary(BinaryOp::Or, makeNumber(1), makeNumber(0))));
    stmts.push_back(makeReturn(makeNumber(0)));
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {}, blockN(std::move(stmts)))));
    auto cu = cb.build();
    runTest("All binary operators", cu, true);
}

void testEmptyStatement() {
    // int main() { ; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block2(makeEmpty(), makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Empty statement", cu, true);
}

void testIntMainNoReturn() {
    // int main() { }  // no return statement
    Block emptyBody;
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {}, std::move(emptyBody))));
    auto cu = cb.build();
    runTest("int main() with no return (warning)", cu, true);
}

// =============================================================================
// Negative Test Cases (should fail)
// =============================================================================

void testValidProgramWithVoidMain() {
    // void main() { }
    Block emptyBody;
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Void, {}, std::move(emptyBody))));
    auto cu = cb.build();
    runTest("void main() should fail", cu, false, {"must return 'int'"});
}

void testValidProgramWithParamsMain() {
    // int main(int x) { return 0; }
    std::vector<Param> mainParams;
    mainParams.push_back(Param{"x"});
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, std::move(mainParams),
        block1(makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("int main(int x) should fail", cu, false, {"must have no parameters"});
}

void testMissingMain() {
    // int foo() { return 0; }  -- no main
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Int, {},
        block1(makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Missing main function", cu, false, {"must contain a 'main' function"});
}

void testUndeclaredVariable() {
    // int main() { x = 5; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block2(
            makeAssign("x", makeNumber(5)),
            makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Undeclared variable assignment", cu, false, {"is not declared"});
}

void testUndeclaredVariableInExpr() {
    // int main() { return x; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeId("x"))))));
    auto cu = cb.build();
    runTest("Undeclared variable in expression", cu, false, {"is not declared"});
}

void testDuplicateFunction() {
    // int foo() { return 0; }
    // int foo() { return 1; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Int, {},
        block1(makeReturn(makeNumber(0))))));
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Int, {},
        block1(makeReturn(makeNumber(1))))));
    auto cu = cb.build();
    runTest("Duplicate function definition", cu, false, {"already defined"});
}

void testDuplicateVariable() {
    // int main() { int x = 1; int x = 2; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeVarDecl("x", makeNumber(1)),
            makeVarDecl("x", makeNumber(2)),
            makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Duplicate variable in same scope", cu, false, {"already defined"});
}

void testDuplicateParameter() {
    // int foo(int x, int x) { return 0; }
    std::vector<Param> dupParams;
    dupParams.push_back(Param{"x"});
    dupParams.push_back(Param{"x"});
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeNumber(0))))));
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Int, std::move(dupParams),
        block1(makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Duplicate parameter name", cu, false, {"already defined"});
}

void testBreakOutsideLoop() {
    // int main() { break; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block2(makeBreak(), makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("break outside loop", cu, false, {"not inside a loop"});
}

void testContinueOutsideLoop() {
    // int main() { continue; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block2(makeContinue(), makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("continue outside loop", cu, false, {"not inside a loop"});
}

void testReturnValueInVoidFunc() {
    // void foo() { return 1; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeNumber(0))))));
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Void, {},
        block1(makeReturn(makeNumber(1))))));
    auto cu = cb.build();
    runTest("return value in void function", cu, false, {"must not return a value"});
}

void testReturnNoValueInIntFunc() {
    // int foo() { return; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeNumber(0))))));
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Int, {},
        block1(makeEmptyReturn()))));
    auto cu = cb.build();
    runTest("return without value in int function", cu, false, {"must return a value"});
}

void testFunctionCallWrongArgCount() {
    // int foo(int x, int y) { return x + y; }
    // int main() { return foo(1); }
    std::vector<Param> fooParams;
    fooParams.push_back(Param{"x"});
    fooParams.push_back(Param{"y"});
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("foo", TypeKind::Int, std::move(fooParams),
        block1(makeReturn(makeBinary(BinaryOp::Add, makeId("x"), makeId("y")))))));
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block1(makeReturn(makeCall("foo",
            std::make_unique<Expr>(NumberExpr{1})))))));
    auto cu = cb.build();
    runTest("Function call with wrong argument count", cu, false, {"expects 2 arguments"});
}

void testCallNonFunction() {
    // int main() { int x = 1; return x(1); }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block2(
            makeVarDecl("x", makeNumber(1)),
            makeReturn(makeCall("x",
                std::make_unique<Expr>(NumberExpr{1})))))));
    auto cu = cb.build();
    runTest("Calling non-function", cu, false, {"is not a function"});
}

void testAssignToConst() {
    // int main() { const int x = 1; x = 2; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeConstDecl("x", makeNumber(1)),
            makeAssign("x", makeNumber(2)),
            makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("Assignment to const", cu, false, {"Cannot assign to constant"});
}

void testConstWithNonConstInit() {
    // int main() { int y = 1; const int x = y; return 0; }
    CompUnitBuilder cb;
    cb.add(TopLevel(makeFuncDef("main", TypeKind::Int, {},
        block3(
            makeVarDecl("y", makeNumber(1)),
            makeConstDecl("x", makeId("y")),
            makeReturn(makeNumber(0))))));
    auto cu = cb.build();
    runTest("const with non-const initializer", cu, false, {"not a compile-time constant"});
}

void testGlobalDuplicate() {
    // int g = 1;
    // int g = 2;
    CompUnitBuilder cb;
    cb.add(TopLevel(VarDecl{"g", std::make_unique<Expr>(NumberExpr{1})}));
    cb.add(TopLevel(VarDecl{"g", std::make_unique<Expr>(NumberExpr{2})}));
    auto cu = cb.build();
    runTest("Duplicate global variable", cu, false, {"already defined"});
}

// =============================================================================
// Main
// =============================================================================
int main() {
    std::cout << "=== ToyC Semantic Analysis Test Suite ===" << std::endl;
    std::cout << std::endl;

    std::cout << "--- Positive Tests (should pass) ---" << std::endl;
    testValidProgram();
    testBreakInLoop();
    testContinueInLoop();
    testFunctionCallCorrect();
    testConstValid();
    testVariableShadowing();
    testValidVariableUsage();
    testIfElse();
    testWhileLoop();
    testGlobalVariable();
    testGlobalConst();
    testFunctionCallAsExpression();
    testNestedBlocks();
    testUnaryOperators();
    testAllOperators();
    testEmptyStatement();
    testIntMainNoReturn();

    std::cout << std::endl;
    std::cout << "--- Negative Tests (should fail) ---" << std::endl;
    testValidProgramWithVoidMain();
    testValidProgramWithParamsMain();
    testMissingMain();
    testUndeclaredVariable();
    testUndeclaredVariableInExpr();
    testDuplicateFunction();
    testDuplicateVariable();
    testDuplicateParameter();
    testBreakOutsideLoop();
    testContinueOutsideLoop();
    testReturnValueInVoidFunc();
    testReturnNoValueInIntFunc();
    testFunctionCallWrongArgCount();
    testCallNonFunction();
    testAssignToConst();
    testConstWithNonConstInit();
    testGlobalDuplicate();

    std::cout << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Results: " << passedTests << "/" << totalTests << " tests passed" << std::endl;

    if (passedTests == totalTests) {
        std::cout << "All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << (totalTests - passedTests) << " test(s) failed." << std::endl;
        return 1;
    }
}