#include "complexity.h"

#include <gtest/gtest.h>

#include <string>

#include "lexer.h"
#include "parser.h"

namespace {

std::string analyze(const std::string& source) {
    Lexer lexer("complexity.ps", source);
    auto tokens = lexer.make_tokens();
    for (const auto& token : tokens) {
        EXPECT_NE(token->get_type(), TOKEN_ERROR) << token->get_value();
    }
    Parser parser(tokens);
    auto ast = parser.parse();
    for (const auto& node : ast) {
        EXPECT_NE(node->get_type(), NODE_ERROR) << node->get_node();
    }
    return analyze_complexity(ast);
}

void expect_bound(const std::string& report, const std::string& label, const std::string& bound) {
    EXPECT_NE(report.find(label + ": " + bound + "\n"), std::string::npos) << report;
}

TEST(ComplexityTest, ConstantLoopsAndEmptyProgram) {
    expect_bound(analyze(""), "Program", "O(1)");
    auto report = analyze("for i <- 1 to 10 do\n    for j <- 10 to 1 step -1 do x <- i + j\n");
    expect_bound(report, "Program", "O(1)");
}

TEST(ComplexityTest, NestedLoopsMultiplyAndSequentialLoopsUseLargestTerm) {
    auto report = analyze(
        "Algorithm Linear(n):\n"
        "    for i <- 1 to n do x <- i\n"
        "    for j <- 1 to n do x <- j\n"
        "Algorithm Pairs(n):\n"
        "    for i <- 1 to n do\n"
        "        for j <- 1 to i do x <- i + j\n");
    expect_bound(report, "Program", "O(1)");
    expect_bound(report, "Algorithm Linear", "O(n)");
    expect_bound(report, "Algorithm Pairs", "O(n^2)");
}

TEST(ComplexityTest, SymbolicPolynomialBoundsAndAliases) {
    auto report = analyze(
        "Algorithm Squares(n):\n"
        "    limit <- n * n\n"
        "    for i <- 1 to limit do x <- i\n"
        "Algorithm Cubes(n):\n"
        "    for i <- 1 to n^3 do x <- i\n");
    expect_bound(report, "Algorithm Squares", "O(n^2)");
    expect_bound(report, "Algorithm Cubes", "O(n^3)");
}

TEST(ComplexityTest, ConditionalPathsUseWorstBound) {
    auto report = analyze(
        "Algorithm Branch(n):\n"
        "    if n > 10 then\n"
        "        for i <- 1 to n do x <- i\n"
        "    else\n"
        "        for i <- 1 to n do\n"
        "            for j <- 1 to n do x <- j\n");
    expect_bound(report, "Algorithm Branch", "O(n^2)");
}

TEST(ComplexityTest, ForBoundsUseTheNewlyAssignedCounter) {
    auto report = analyze("i <- 1\nfor i <- n to i * i do x <- i\n");
    expect_bound(report, "Program", "O(n^2)");
}

TEST(ComplexityTest, RecognizesMonotonicLinearAndLogarithmicLoops) {
    auto report = analyze(
        "Algorithm LinearWhile(n):\n"
        "    i <- 0\n"
        "    while i < n do i <- i + 1\n"
        "Algorithm Doubling(n):\n"
        "    i <- 1\n"
        "    while i <= n do i <- i * 2\n"
        "Algorithm Halving(n):\n"
        "    i <- n\n"
        "    while i > 1 do i <- i / 2\n"
        "Algorithm RepeatCount(n):\n"
        "    i <- 0\n"
        "    repeat i <- i + 1 until i >= n\n");
    expect_bound(report, "Algorithm LinearWhile", "O(n)");
    expect_bound(report, "Algorithm Doubling", "O(log n)");
    expect_bound(report, "Algorithm Halving", "O(log n)");
    expect_bound(report, "Algorithm RepeatCount", "O(n)");
}

TEST(ComplexityTest, CallsUseArgumentBoundsAndDetectRecursion) {
    auto report = analyze(
        "Algorithm Linear(n):\n"
        "    for i <- 1 to n do x <- i\n"
        "Algorithm SquareCall(n):\n"
        "    return Linear(n * n)\n"
        "Algorithm Recursive(n):\n"
        "    if n < 1 then return 0\n"
        "    return Recursive(n - 1)\n"
        "Linear(10)\n");
    expect_bound(report, "Program", "O(1)");
    expect_bound(report, "Algorithm SquareCall", "O(n^2)");
    expect_bound(report, "Algorithm Recursive", "unknown");
    EXPECT_NE(report.find("Recursive call involving 'Recursive'"), std::string::npos);
}

TEST(ComplexityTest, IndirectCallsAndUnknownMethodsRemainUnknown) {
    auto report = analyze(
        "Algorithm Invoke(f, n):\n"
        "    return f(n)\n"
        "Algorithm Measure(a):\n"
        "    return a.size()\n"
        "Struct Box:\n"
        "    value\n"
        "    Algorithm get():\n"
        "        return self.value\n");
    expect_bound(report, "Algorithm Invoke", "unknown");
    expect_bound(report, "Algorithm Measure", "unknown");
    expect_bound(report, "Algorithm Box.get", "O(1)");
}

TEST(ComplexityTest, RejectsUnprovedTerminationAndChangingBounds) {
    const char* sources[] = {
        "i <- 0\nwhile i < n do i <- i * 2\n",
        "i <- 1\nwhile i < n do i <- i - 1\n",
        "i <- 0\nwhile i < n do\n    if n > 10 then continue\n    i <- i + 1\n",
        "i <- 0\nwhile i < n do\n    n <- n + 1\n    i <- i + 1\n",
        "i <- n\nwhile i >= 0 do i <- i / 2\n",
        "for i <- 1 to n step 0 do x <- i\n",
        "for i <- 1 to n step n do x <- i\n",
        "while true do x <- 1\n",
    };
    for (const auto* source : sources) {
        SCOPED_TRACE(source);
        expect_bound(analyze(source), "Program", "unknown");
    }
}

TEST(ComplexityTest, DoesNotKeepSingleIterationValuesAfterLoopsOrBranches) {
    const char* sources[] = {
        "limit <- 1\nfor i <- 1 to n do limit <- limit * 2\n"
        "for j <- 1 to limit do x <- j\n",
        "limit <- 1\nif n > 10 then limit <- n * n\n"
        "for j <- 1 to limit do x <- j\n",
        "limit <- 1\nfor i <- 1 to n do\n"
        "    for j <- 1 to limit do x <- j\n"
        "    limit <- limit * 2\n",
    };
    for (const auto* source : sources) {
        SCOPED_TRACE(source);
        expect_bound(analyze(source), "Program", "unknown");
    }
}

TEST(ComplexityTest, ShortCircuitAssignmentsDoNotReplaceUnconditionalBounds) {
    for (const auto* expression : {"false and (limit <- 1)", "true or (limit <- 1)"}) {
        SCOPED_TRACE(expression);
        auto report = analyze(std::string("limit <- n * n\n") + expression +
                              "\nfor i <- 1 to limit do 1\n");
        expect_bound(report, "Program", "unknown");
    }
    auto left_assignment = analyze(
        "(limit <- n * n) and true\n"
        "for i <- 1 to limit do 1\n");
    expect_bound(left_assignment, "Program", "O(n^2)");
}

TEST(ComplexityTest, ArrayAliasesAndMutatingCallsInvalidateLengths) {
    auto aliases = analyze(
        "a <- {}\n"
        "b <- a\n"
        "for i <- 1 to n do b.push(i)\n"
        "for j <- 1 to a.size() do\n"
        "    for k <- 1 to a.size() do x <- k\n");
    expect_bound(aliases, "Program", "unknown");
    auto calls = analyze(
        "Algorithm Grow(a, n):\n"
        "    for i <- 1 to n do a.push(i)\n"
        "a <- {}\n"
        "Grow(a, n)\n"
        "for j <- 1 to a.size() do\n"
        "    for k <- 1 to a.size() do x <- k\n");
    expect_bound(calls, "Program", "unknown");
}

TEST(ComplexityTest, NamedCallsInsideControlFlowInvalidateArrayLengths) {
    const char* bodies[] = {
        "if n > 0 then\n    Grow(a, n)\n",
        "if n < 0 then 0\nelse\n    Grow(a, n)\n",
        "for i <- 1 to 1 do\n    Grow(a, n)\n",
    };
    const std::string setup =
        "Algorithm Grow(a, n):\n"
        "    for i <- 1 to n do a.push(i)\n"
        "a <- {}\n";
    for (const auto* body : bodies) {
        SCOPED_TRACE(body);
        auto report = analyze(setup + body + "for j <- 1 to a.size() * a.size() do 1\n");
        expect_bound(report, "Program", "unknown");
    }
}

TEST(ComplexityTest, UserPrintDefinitionTakesPrecedenceOverBuiltin) {
    auto report = analyze(
        "Algorithm print(n):\n"
        "    for i <- 1 to n do x <- i\n"
        "print(n)\n");
    expect_bound(report, "Program", "O(n)");
    expect_bound(report, "Algorithm print", "O(n)");
}

TEST(ComplexityTest, NestedDefinitionsDoNotResolveToUnrelatedGlobals) {
    auto report = analyze(
        "Algorithm Work(n):\n"
        "    return n\n"
        "Algorithm Outer(n):\n"
        "    Algorithm Work(n):\n"
        "        for i <- 1 to n do x <- i\n"
        "    return Work(n)\n");
    expect_bound(report, "Algorithm Outer", "unknown");
}

TEST(ComplexityTest, ArrayMutationCannotMakeWhileBoundAppearInvariant) {
    auto report = analyze(
        "Algorithm Grow(a):\n"
        "    a.push(1)\n"
        "a <- {1}\n"
        "i <- 0\n"
        "while i < a.size() do\n"
        "    Grow(a)\n"
        "    i <- i + 1\n");
    expect_bound(report, "Program", "unknown");
}

TEST(ComplexityTest, DefinitionsInsideControlFlowInvalidateBindings) {
    const char* sources[] = {
        "Algorithm Work(n): return 0\n"
        "for i <- 1 to 1 do\n"
        "    Algorithm Work(n):\n"
        "        for j <- 1 to n do x <- j\n"
        "Work(n)\n",
        "Algorithm Work(n): return 0\n"
        "if n > 0 then\n"
        "    Algorithm Work(n):\n"
        "        for j <- 1 to n do x <- j\n"
        "Work(n)\n",
    };
    for (const auto* source : sources) {
        SCOPED_TRACE(source);
        expect_bound(analyze(source), "Program", "unknown");
    }
}

TEST(ComplexityTest, ArgumentsEvaluateInLocalScopeBeforeParameterBindings) {
    auto sequential = analyze(
        "Algorithm Work(a, b):\n"
        "    for i <- 1 to b do x <- i\n"
        "a <- 1\n"
        "Work(n, a * a)\n");
    expect_bound(sequential, "Program", "O(1)");
    auto local_assignment = analyze(
        "Algorithm Identity(a): return a\n"
        "limit <- n * n\n"
        "Identity(limit <- 1)\n"
        "for i <- 1 to limit do x <- i\n");
    expect_bound(local_assignment, "Program", "O(n^2)");
    auto argument_assignment = analyze(
        "Algorithm Work(a, b):\n"
        "    for i <- 1 to b do x <- i\n"
        "Work(limit <- n, limit * limit)\n");
    expect_bound(argument_assignment, "Program", "O(n^2)");
    auto print_assignment = analyze(
        "limit <- n * n\n"
        "print(limit <- 1)\n"
        "for i <- 1 to limit do x <- i\n");
    expect_bound(print_assignment, "Program", "O(n^2)");
}

TEST(ComplexityTest, LaterArgumentsCanMutateEarlierArrayArguments) {
    auto report = analyze(
        "Algorithm Grow(a, n):\n"
        "    for i <- 1 to n do a.push(i)\n"
        "Algorithm Work(a, b):\n"
        "    for i <- 1 to a.size() * a.size() do 1\n"
        "a <- {1}\n"
        "Work(a, Grow(a, n))\n");
    expect_bound(report, "Program", "unknown");
}

TEST(ComplexityTest, ReportExplainsLimitsAndDoesNotExecuteProgram) {
    auto report = analyze("print(\"do not execute\")\n");
    expect_bound(report, "Program", "O(1)");
    EXPECT_NE(report.find("conditional estimates, not termination proofs"), std::string::npos);
    EXPECT_NE(report.find("memoization effects"), std::string::npos);
    EXPECT_NE(report.find("I/O and formatting work are excluded"), std::string::npos);
}

}  // namespace
