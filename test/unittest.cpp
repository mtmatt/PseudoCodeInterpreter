#include <cstdio>
#include <gtest/gtest.h>
#include <token.h>
#include <lexer.h>
#include <symboltable.h>
#include <value.h>
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

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
