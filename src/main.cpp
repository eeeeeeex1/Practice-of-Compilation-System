#include "Lexer.h"
#include "Parser.h"
#include "SemanticAnalyzer.h"
#include "CodeGenerator.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>

int main(int argc, char* argv[]) {
    bool optimize = false;

    // Check for -opt flag
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-opt") == 0) {
            optimize = true;
        }
    }

    // Read all input from stdin
    std::string source;
    {
        std::ostringstream oss;
        oss << std::cin.rdbuf();
        source = oss.str();
    }

    if (source.empty()) {
        std::cerr << "Error: Empty input\n";
        return 1;
    }

    try {
        // Lexer
        std::istringstream input(source);
        Lexer lexer(input);

        // Parser
        Parser parser(lexer);
        auto compUnit = parser.parseCompUnit();

        // Semantic analysis
        SemanticAnalyzer semAnalyzer(optimize);
        semAnalyzer.analyze(*compUnit);

        // Code generation
        std::ostringstream output;
        CodeGenerator codeGen(output, optimize);
        codeGen.generate(*compUnit);

        // Write output
        std::cout << output.str();

        return 0;
    } catch (const SemanticError& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Internal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
