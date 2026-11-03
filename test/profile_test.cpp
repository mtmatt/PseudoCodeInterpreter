#include "profile.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "symboltable.h"
#include "value.h"

namespace {

NodeList parse_profile_program(const std::string& source) {
    Lexer lexer("profile_test.ps", source);
    Parser parser(lexer.make_tokens());
    NodeList ast = parser.parse();
    for (const auto& node : ast) EXPECT_NE(node->get_type(), NODE_ERROR) << node->get_node();
    return ast;
}

void execute_profile_program(const NodeList& ast, SymbolTable& symbols) {
    Interpreter interpreter(symbols, false);
    for (const auto& node : ast) {
        auto result = interpreter.visit(node);
        ASSERT_NE(result->get_type(), VALUE_ERROR) << result->get_num();
    }
}

ProfileStats measure_program(const NodeList& ast, SymbolTable& symbols) {
    ProfileStats stats;
    ProfileSession session(stats);
    execute_profile_program(ast, symbols);
    session.check_limits();
    return stats;
}

}  // namespace

TEST(ProfileTest, CountsStayStableAfterJitWarmupAndScaleWithLoopLength) {
    auto ast = parse_profile_program(
        "total <- 0\n"
        "for i <- 1 to n do total <- total + i\n");
    for (int i = 0; i < 2; ++i) {
        SymbolTable warmup;
        warmup.set("n", std::make_shared<TypedValue<int64_t>>(VALUE_INT, 40));
        execute_profile_program(ast, warmup);
    }

    ProfileStats measured[3];
    const int sizes[] = {40, 80, 40};
    for (int i = 0; i < 3; ++i) {
        SymbolTable symbols;
        symbols.set("n", std::make_shared<TypedValue<int64_t>>(VALUE_INT, sizes[i]));
        measured[i] = measure_program(ast, symbols);
        EXPECT_EQ(symbols.get("total")->as_int(), sizes[i] * (sizes[i] + 1) / 2);
        EXPECT_EQ(measured[i].loop_iterations, sizes[i]);
    }
    EXPECT_EQ(measured[0].ast_evaluations, measured[2].ast_evaluations);
    // Four body evaluations per iteration: assignment, addition, and two operands.
    EXPECT_EQ(measured[1].ast_evaluations - measured[0].ast_evaluations, 4 * 40);
    EXPECT_EQ(measured[0].algorithm_calls, 0);
}

TEST(ProfileTest, RecursiveCallsBypassExistingMemoization) {
    auto ast = parse_profile_program(
        "Algorithm fib(n):\n"
        "    if n <= 1 then return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "answer <- fib(6)\n");
    SymbolTable warmup;
    execute_profile_program(ast, warmup);
    SymbolTable symbols;
    auto stats = measure_program(ast, symbols);
    EXPECT_EQ(symbols.get("answer")->as_int(), 8);
    EXPECT_EQ(stats.algorithm_calls, 25);
    EXPECT_EQ(stats.max_call_depth, 6);
    EXPECT_GT(stats.ast_evaluations, 100);
}

TEST(ProfileTest, CountsSingleReturnAlgorithmsAndStructMethods) {
    auto ast = parse_profile_program(
        "Algorithm twice(x):\n"
        "    return x * 2\n"
        "Struct Counter:\n"
        "    value\n"
        "    Algorithm Counter constructor():\n"
        "        self.value <- 0\n"
        "    Algorithm add(x):\n"
        "        self.value <- self.value + twice(x)\n"
        "c <- Counter()\n"
        "for i <- 1 to 20 do c.add(i)\n"
        "answer <- c.value\n");
    SymbolTable symbols;
    auto stats = measure_program(ast, symbols);
    EXPECT_EQ(symbols.get("answer")->as_int(), 420);
    EXPECT_EQ(stats.algorithm_calls, 41);
    EXPECT_EQ(stats.max_call_depth, 2);
    EXPECT_EQ(stats.loop_iterations, 20);
}

TEST(ProfileTest, CountsWhileRepeatAndShortCircuitedBranches) {
    auto ast = parse_profile_program(
        "i <- 0\n"
        "while i < 3 do i <- i + 1\n"
        "repeat i <- i - 1 until i = 0\n"
        "ok <- false and missing\n");
    SymbolTable symbols;
    auto stats = measure_program(ast, symbols);
    EXPECT_EQ(symbols.get("i")->as_int(), 0);
    EXPECT_EQ(symbols.get("ok")->as_int(), 0);
    EXPECT_EQ(stats.loop_iterations, 6);
}

TEST(ProfileTest, NativeArrayCallsStillVisitArgumentsAndReceiver) {
    auto ast = parse_profile_program(
        "a <- {}\n"
        "for i <- 1 to 20 do a.push(i + 1)\n"
        "count <- a.size()\n");
    SymbolTable symbols;
    auto first = measure_program(ast, symbols);
    SymbolTable again;
    auto second = measure_program(ast, again);
    EXPECT_EQ(symbols.get("count")->as_int(), 20);
    EXPECT_EQ(first.ast_evaluations, second.ast_evaluations);
    EXPECT_GE(first.ast_evaluations, 20 * 6);
    EXPECT_EQ(first.algorithm_calls, 0);
}

TEST(ProfileTest, StepLimitStopsInfiniteLoopsAndRestoresNormalExecution) {
    auto ast = parse_profile_program("while true do 1\n");
    ProfileStats stats;
    {
        ProfileSession session(stats, 50);
        SymbolTable symbols;
        EXPECT_THROW(execute_profile_program(ast, symbols), ProfileLimitExceeded);
        EXPECT_EQ(stats.ast_evaluations + stats.loop_iterations, 50);
        EXPECT_THROW(session.check_limits(), ProfileLimitExceeded);
    }
    EXPECT_FALSE(profile_active());
    SymbolTable symbols;
    execute_profile_program(parse_profile_program("answer <- 2 + 3\n"), symbols);
    EXPECT_EQ(symbols.get("answer")->as_int(), 5);
}

TEST(ProfileTest, AlgorithmErrorsDoNotDisappearWhenOptimizationsAreDisabled) {
    auto ast = parse_profile_program(
        "Algorithm fail():\n"
        "    missing\n"
        "    return 42\n"
        "answer <- fail()\n");
    ProfileStats stats;
    ProfileSession session(stats);
    SymbolTable symbols;
    Interpreter interpreter(symbols, false);
    ASSERT_NE(interpreter.visit(ast[0])->get_type(), VALUE_ERROR);
    auto result = interpreter.visit(ast[1]);
    EXPECT_EQ(result->get_type(), VALUE_ERROR);
    EXPECT_FALSE(symbols.contains_local("answer"));
    session.check_limits();
}

TEST(ProfileTest, DepthLimitStopsRecursionAndNestedSessionRestoresOuterCounters) {
    auto ast = parse_profile_program(
        "Algorithm forever(n):\n"
        "    return forever(n + 1)\n"
        "forever(0)\n");
    ProfileStats outer;
    {
        ProfileSession outer_session(outer);
        profile_record_node();
        ProfileStats inner;
        {
            ProfileSession inner_session(inner, 10000, 12);
            SymbolTable symbols;
            EXPECT_THROW(execute_profile_program(ast, symbols), ProfileLimitExceeded);
            EXPECT_EQ(inner.max_call_depth, 12);
            EXPECT_EQ(inner.algorithm_calls, 12);
        }
        profile_record_node();
        outer_session.check_limits();
        EXPECT_EQ(outer.ast_evaluations, 2);
        EXPECT_EQ(outer.algorithm_calls, 0);
    }
    EXPECT_FALSE(profile_active());
}

TEST(ProfileTest, DestructorLimitIsReportedWithoutThrowingFromCppCleanup) {
    auto ast = parse_profile_program(
        "Struct Cleanup:\n"
        "    value\n"
        "    Algorithm Cleanup constructor():\n"
        "        self.value <- 0\n"
        "    Algorithm destructor():\n"
        "        while true do 1\n"
        "Algorithm work():\n"
        "    local <- Cleanup()\n"
        "    return 0\n"
        "work()\n");
    ProfileStats stats;
    ProfileSession session(stats, 100);
    SymbolTable symbols;
    execute_profile_program(ast, symbols);
    EXPECT_THROW(session.check_limits(), ProfileLimitExceeded);
    EXPECT_EQ(stats.ast_evaluations + stats.loop_iterations, 100);
}

TEST(ProfileTest, CallArrayAndAssignmentErrorsPropagate) {
    const char* sources[] = {
        "missing()\n",
        "a <- {missing}\n",
        "a <- 1\na()\n",
        "a <- {1}\na[2] <- 4\n",
        "a <- {1}\na[1] <- missing\n",
        "a <- {1}\na[missing] <- 4\n",
        "missing[1] <- 4\n",
    };
    for (const auto* source : sources) {
        SCOPED_TRACE(source);
        auto ast = parse_profile_program(source);
        ProfileStats stats;
        ProfileSession session(stats);
        SymbolTable symbols;
        Interpreter interpreter(symbols, false);
        std::shared_ptr<Value> result;
        for (const auto& node : ast) {
            result = interpreter.visit(node);
            if (result->get_type() == VALUE_ERROR) break;
        }
        ASSERT_EQ(result->get_type(), VALUE_ERROR);
        session.check_limits();
    }
}

TEST(ProfileTest, DestructorRuntimeFailuresAreDeferredAndStopLaterStatements) {
    const char* failures[] = {"missing", "int(\"99999999999999999999999999999\")"};
    for (const auto* failure : failures) {
        SCOPED_TRACE(failure);
        auto ast = parse_profile_program(
            "Struct Cleanup:\n"
            "    value\n"
            "    Algorithm Cleanup constructor():\n"
            "        self.value <- 0\n"
            "    Algorithm destructor():\n"
            "        " +
            std::string(failure) +
            "\n"
            "Algorithm work():\n"
            "    local <- Cleanup()\n"
            "    return 0\n"
            "work()\n");
        ProfileStats stats;
        ProfileSession session(stats);
        SymbolTable symbols;
        execute_profile_program(ast, symbols);
        EXPECT_THROW(session.check_limits(), std::exception);
        auto later = parse_profile_program("after <- 42\n");
        Interpreter interpreter(symbols, false);
        EXPECT_THROW(interpreter.visit(later[0]), std::exception);
        EXPECT_FALSE(symbols.contains_local("after"));
    }
}

TEST(ProfileTest, ArgumentsAreEvaluatedBeforeParameterBinding) {
    auto ast = parse_profile_program(
        "Algorithm second(key, value):\n"
        "    return value\n"
        "Struct Pair:\n"
        "    Algorithm second(key, value):\n"
        "        return value\n"
        "key <- 2\n"
        "plain <- second(key * 16 + 15, key + 100)\n"
        "pair <- Pair()\n"
        "method <- pair.second(key * 16 + 15, key + 100)\n"
        "values <- {}\n"
        "ordered <- second(values.push(3), values.size())\n");
    // The same rule applies to normal and instrumented execution.
    for (bool profiled : {false, true}) {
        SymbolTable symbols;
        if (profiled)
            measure_program(ast, symbols);
        else
            execute_profile_program(ast, symbols);
        EXPECT_EQ(symbols.get("plain")->as_int(), 102);
        EXPECT_EQ(symbols.get("method")->as_int(), 102);
        EXPECT_EQ(symbols.get("ordered")->as_int(), 1);
        EXPECT_EQ(symbols.get("key")->as_int(), 2);
    }
}

TEST(ProfileTest, IndexedAssignmentEvaluatesReceiverOnceAndSurvivesRhsResize) {
    auto ast = parse_profile_program(
        "calls <- {0}\n"
        "values <- {1}\n"
        "Algorithm target():\n"
        "    calls[1] <- calls[1] + 1\n"
        "    return values\n"
        "target()[1] <- values.push(2)\n");
    SymbolTable symbols;
    measure_program(ast, symbols);
    EXPECT_EQ(symbols.get("calls")->get_num(), "{1}");
    EXPECT_EQ(symbols.get("values")->get_num(), "{2, 2}");
}

TEST(ProfileTest, OptimizedForCallsMatchProfiledArgumentScope) {
    auto ast = parse_profile_program(
        "Algorithm second(key, value):\n"
        "    return value\n"
        "key <- 2\n"
        "for i <- 1 to 20 do answer <- second(key * 16 + 15, key + 100)\n"
        "for i <- 1 to 20 do key <- second(0, key + 1)\n");
    for (bool profiled : {false, true}) {
        SymbolTable symbols;
        if (profiled)
            measure_program(ast, symbols);
        else
            execute_profile_program(ast, symbols);
        EXPECT_EQ(symbols.get("answer")->as_int(), 102);
        EXPECT_EQ(symbols.get("key")->as_int(), 22);
    }
}
