#include <cstdio>
#include <gtest/gtest.h>
#include <token.h>
#include <lexer.h>
#include <symboltable.h>
#include <value.h>
#include <parser.h>
#include <interpreter.h>
#include <memory>

// Token Tests
TEST(TokenTest, EmptyTokenInTypeNone) {
  Token token;
  EXPECT_EQ(token.get_type(), TOKEN_NONE);
}

// Lexer Tests
TEST(LexerTest, TestIntegerToken) {
    Lexer lexer("test", "123");
    TokenList tokens = lexer.make_tokens();
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->get_type(), TOKEN_INT);

    // Check value
    auto intToken = std::dynamic_pointer_cast<TypedToken<int64_t>>(tokens[0]);
    // Use .get() to avoid overloaded operator! on shared_ptr<Value> (or related types)
    ASSERT_NE(intToken.get(), nullptr);
    // Note: get_value() returns std::string representation of the value in this codebase
    EXPECT_EQ(intToken->get_value(), "123");
}

TEST(LexerTest, TestFloatToken) {
    Lexer lexer("test", "123.456");
    TokenList tokens = lexer.make_tokens();
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->get_type(), TOKEN_FLOAT);

    auto floatToken = std::dynamic_pointer_cast<TypedToken<double>>(tokens[0]);
    ASSERT_NE(floatToken.get(), nullptr);
    EXPECT_EQ(floatToken->get_value(), "123.456");
}

TEST(LexerTest, TestIdentifierToken) {
    Lexer lexer("test", "myVar");
    TokenList tokens = lexer.make_tokens();
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->get_type(), TOKEN_IDENTIFIER);

    auto idToken = std::dynamic_pointer_cast<TypedToken<std::string>>(tokens[0]);
    ASSERT_NE(idToken.get(), nullptr);
    EXPECT_EQ(idToken->get_value(), "myVar");
}

TEST(LexerTest, TestKeywordToken) {
    Lexer lexer("test", "if");
    TokenList tokens = lexer.make_tokens();
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->get_type(), TOKEN_KEYWORD);

    auto keywordToken = std::dynamic_pointer_cast<TypedToken<std::string>>(tokens[0]);
    ASSERT_NE(keywordToken.get(), nullptr);
    EXPECT_EQ(keywordToken->get_value(), "if");
}

TEST(LexerTest, TestOperators) {
    Lexer lexer("test", "+ - * /");
    TokenList tokens = lexer.make_tokens();
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0]->get_type(), TOKEN_ADD);
    EXPECT_EQ(tokens[1]->get_type(), TOKEN_SUB);
    EXPECT_EQ(tokens[2]->get_type(), TOKEN_MUL);
    EXPECT_EQ(tokens[3]->get_type(), TOKEN_DIV);
}

// SymbolTable Tests
TEST(SymbolTableTest, TestSetAndGet) {
    SymbolTable st;
    auto val = std::make_shared<TypedValue<int64_t>>(VALUE_INT, 42);
    st.set("x", val);

    auto retrieved = st.get("x");
    ASSERT_NE(retrieved.get(), nullptr);

    auto intVal = std::dynamic_pointer_cast<TypedValue<int64_t>>(retrieved);
    ASSERT_NE(intVal.get(), nullptr);
    EXPECT_EQ(intVal->get_num(), "42");
}

TEST(SymbolTableTest, TestGetNonExistent) {
    SymbolTable st;
    auto retrieved = st.get("y");
    ASSERT_NE(retrieved.get(), nullptr);
    EXPECT_EQ(retrieved->get_type(), VALUE_ERROR);
}

TEST(SymbolTableTest, TestScope) {
    SymbolTable parent;
    auto valParent = std::make_shared<TypedValue<int64_t>>(VALUE_INT, 100);
    parent.set("x", valParent);

    SymbolTable child(&parent);

    // Should be able to access parent's variable
    auto retrieved = child.get("x");
    ASSERT_NE(retrieved.get(), nullptr);
    EXPECT_EQ(retrieved->get_num(), "100");

    // Shadowing
    auto valChild = std::make_shared<TypedValue<int64_t>>(VALUE_INT, 200);
    child.set("x", valChild);

    retrieved = child.get("x");
    EXPECT_EQ(retrieved->get_num(), "200");

    // Parent should remain unchanged
    auto parentRetrieved = parent.get("x");
    EXPECT_EQ(parentRetrieved->get_num(), "100");
}

TEST(SymbolTableTest, TestErase) {
    SymbolTable st;
    auto val = std::make_shared<TypedValue<int64_t>>(VALUE_INT, 42);
    st.set("x", val);

    st.erase("x");

    // After erase, it should not be found (return ERROR)
    auto retrieved = st.get("x");
    ASSERT_NE(retrieved.get(), nullptr);
    EXPECT_EQ(retrieved->get_type(), VALUE_ERROR);
}

// Interpreter and Parser Tests - Expanded

void check_interpreter(std::string text, std::string expected_val, std::string expected_type = VALUE_INT) {
    Lexer lexer("test", text);
    TokenList tokens = lexer.make_tokens();
    Parser parser(tokens);
    NodeList ast = parser.parse();
    ASSERT_FALSE(ast.empty()) << "Parsing failed for: " << text;

    SymbolTable st;
    Interpreter interpreter(st);

    std::shared_ptr<Value> result;
    for(auto node : ast) {
        result = interpreter.visit(node);
    }

    ASSERT_NE(result.get(), nullptr);
    if (expected_type != "ANY")
        EXPECT_EQ(result->get_type(), expected_type) << "Type mismatch for: " << text;
    if (expected_val != "IGNORE")
        EXPECT_EQ(result->get_num(), expected_val) << "Value mismatch for: " << text;
}

TEST(InterpreterTest, TestArithmeticFull) {
    check_interpreter("1 + 2", "3");
    check_interpreter("10 - 4", "6");
    check_interpreter("3 * 5", "15");
    check_interpreter("20 / 4", "5");
    check_interpreter("10 % 3", "1");
    check_interpreter("2 ^ 3", "8");
    check_interpreter("(2 + 3) * 4", "20");
    check_interpreter("2 + 3 * 4", "14");
    check_interpreter("-5", "-5");
    check_interpreter("--5", "5");
}

TEST(InterpreterTest, TestLogicAndComparison) {
    // Note: Equality uses '=', Assignment uses '<-'
    check_interpreter("10 > 5", "1");
    check_interpreter("10 < 5", "0");
    check_interpreter("10 >= 10", "1");
    check_interpreter("10 <= 5", "0");
    check_interpreter("10 = 10", "1");
    check_interpreter("10 != 5", "1");
    check_interpreter("1 and 1", "1");
    check_interpreter("1 and 0", "0");
    check_interpreter("1 or 0", "1");
    check_interpreter("0 or 0", "0");
    check_interpreter("not 0", "1");
    check_interpreter("not 1", "0");
}

TEST(InterpreterTest, TestControlFlowIf) {
    // If Then
    // Returns result of body execution
    check_interpreter("a <- 0; if 1 then a <- 5", "5");
    // Returns 0 if condition false and no else
    check_interpreter("a <- 0; if 0 then a <- 5", "0");

    // If Then Else
    check_interpreter("a <- 0; if 1 then a <- 5 else a <- 10", "5");
    check_interpreter("a <- 0; if 0 then a <- 5 else a <- 10", "10");

    // Else If (Nested)
    check_interpreter("a <- 0; if 0 then a <- 5 else if 1 then a <- 10 else a <- 15", "10");
}

TEST(InterpreterTest, TestControlFlowFor) {
    // For loop returns ArrayValue of results
    // for i <- 1 to 5 step 1 do s <- s + i
    // s starts 0. s becomes 1, 3, 6, 10, 15.
    // Returns {1, 3, 6, 10, 15}
    check_interpreter("s <- 0; for i <- 1 to 5 step 1 do s <- s + i", "{1, 3, 6, 10, 15}", VALUE_ARRAY);

    // Default step
    check_interpreter("s <- 0; for i <- 1 to 5 do s <- s + i", "{1, 3, 6, 10, 15}", VALUE_ARRAY);
}

TEST(InterpreterTest, TestControlFlowWhile) {
    // While loop
    // i <- 0; while i < 5 do i <- i + 1
    // Returns {1, 2, 3, 4, 5}
    check_interpreter("i <- 0; while i < 5 do i <- i + 1", "{1, 2, 3, 4, 5}", VALUE_ARRAY);
}

TEST(InterpreterTest, TestControlFlowRepeat) {
    // Repeat Until
    // i <- 0; repeat i <- i + 1 until i = 5
    // Returns {1, 2, 3, 4, 5}
    check_interpreter("i <- 0; repeat i <- i + 1 until i = 5", "{1, 2, 3, 4, 5}", VALUE_ARRAY);
}

TEST(InterpreterTest, TestArray) {
    check_interpreter("a <- {1, 2, 3}; a[1]", "1");
    check_interpreter("a <- {1, 2, 3}; a[2]", "2");
    check_interpreter("a <- {1, 2, 3}; a.size()", "3");
    // push/pop modify array and return something
    // push returns back() -> 4
    check_interpreter("a <- {1, 2, 3}; a.push(4)", "4");
    // pop returns popped value -> 3
    check_interpreter("a <- {1, 2, 3}; a.pop()", "3");
}

TEST(InterpreterTest, TestBuiltin) {
    check_interpreter("int(\"123\")", "123", VALUE_INT);
    check_interpreter("float(\"12.34\")", "12.34", VALUE_FLOAT);
    check_interpreter("string(123)", "123", VALUE_STRING);
}

TEST(InterpreterTest, TestAssignment) {
    check_interpreter("x <- 10", "10");
    check_interpreter("x <- 10; x <- x + 5", "15");
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
