#include "../catch.hpp"

#include <tsn/utils/ModuleSource.h>
#include <tsn/compiler/Lexer.h>
#include <tsn/compiler/Parser.h>

#include <utils/Array.hpp>

using namespace tsn;
using namespace compiler;
using namespace utils;

TEST_CASE("Parser", "[parser]") {
    const char* testSource =
        "type fn = ((a: i32, b: i32[]) => i32)[];\n"
        "const f : fn = (a: i32, arr: i32[]) => (a >= 0) ? arr[a] : 0;\n"
        "const a = f(1, 2);\n"
        "let b = f(f(1, a), f(2, a + 1));\n"
    ;
    ModuleSource src = ModuleSource("Parser.tsn", testSource);
    Lexer l(&src);
    Parser ps(&l);

    ParseNode* n = ps.parse();
    const auto& errors = ps.errors();
    for (const auto& e : errors) {
        printf("Error [%d, %d]: %s\n", e.src.src.getLine(), e.src.src.getCol(), e.text.c_str());
    }

    if (n) n->json();
    std::cout << std::endl;
}