#include <cstdio>
#include <gtest/gtest.h>
#include <token.h>
#include <lexer.h>
#include <symboltable.h>
#include <value.h>
#include <parser.h>
#include <interpreter.h>
#include <pseudo.h>
#include <memory>
#include <stdexcept>

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

TEST(LexerTest, TestLineComments) {
    Lexer lexer("test", "x <- 1 // ignored\n// whole line\nx / 2");
    TokenList tokens = lexer.make_tokens();
    ASSERT_EQ(tokens.size(), 8);
    EXPECT_EQ(tokens[0]->get_type(), TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1]->get_type(), TOKEN_ASSIGN);
    EXPECT_EQ(tokens[2]->get_type(), TOKEN_INT);
    EXPECT_EQ(tokens[3]->get_type(), TOKEN_NEWLINE);
    EXPECT_EQ(tokens[4]->get_type(), TOKEN_NEWLINE);
    EXPECT_EQ(tokens[5]->get_type(), TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[6]->get_type(), TOKEN_DIV);
    EXPECT_EQ(tokens[7]->get_type(), TOKEN_INT);
}

TEST(LexerTest, TestIndentedCommentLinesIgnoreTabSize) {
    Lexer lexer("test", "x <- 1\n                     // 21 spaces before comment\nx <- 2");
    TokenList tokens = lexer.make_tokens();
    ASSERT_FALSE(tokens.empty());
    EXPECT_NE(tokens[0]->get_type(), TOKEN_ERROR);
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

TEST(InterpreterTest, TestHotNumericLoop) {
    check_interpreter(
        "i <- 0\n"
        "acc <- 0\n"
        "while i < 25 do\n"
        "    i <- i + 1\n"
        "    acc <- (acc + (i * 7) % 11) % 1000\n"
        "acc",
        "130");
}

TEST(InterpreterTest, TestControlFlowRepeat) {
    // Repeat Until
    // i <- 0; repeat i <- i + 1 until i = 5
    // Returns {1, 2, 3, 4, 5}
    check_interpreter("i <- 0; repeat i <- i + 1 until i = 5", "{1, 2, 3, 4, 5}", VALUE_ARRAY);
}

TEST(InterpreterTest, TestArray) {
    check_interpreter("a <- []; a.size()", "0");
    check_interpreter("a <- [1, 2, 3]; a[2]", "2");
    check_interpreter("a <- {1, 2, 3}; a[1]", "1");
    check_interpreter("a <- {1, 2, 3}; a[2]", "2");
    check_interpreter("a <- {1, 2, 3}; a.size()", "3");
    // push/pop modify array and return something
    // push returns back() -> 4
    check_interpreter("a <- {1, 2, 3}; a.push(4)", "4");
    // pop returns popped value -> 3
    check_interpreter("a <- {1, 2, 3}; a.pop()", "3");
    // Array method arguments are evaluated in a call-local scope.
    check_interpreter("x <- 1; a <- {}; a.push(x <- 2); x", "1");
    check_interpreter("a <- {}; a.pop()", "NONE", VALUE_NONE);
    check_interpreter("a <- {1, 2}; a.resize(-1); a.size()", "2");
    check_interpreter("a <- {1, 2}; a.resize(\"abc\"); a.size()", "2");
}

TEST(InterpreterTest, TestStringIndexAndSize) {
    check_interpreter("\"abc\".size()", "3");
    check_interpreter("\"abc\"[2]", "b", VALUE_STRING);
    check_interpreter(
        "Algorithm IsValidParentheses(str):\n"
        "    stack <- {}\n"
        "    for i <- 1 to str.size() do\n"
        "        char <- str[i]\n"
        "        if char = \"(\" then\n"
        "            stack.push(char)\n"
        "        else\n"
        "            if stack.size() = 0 then\n"
        "                return false\n"
        "            stack.pop()\n"
        "    return stack.size() = 0\n"
        "IsValidParentheses(\"()\")",
        "1");
}

TEST(InterpreterTest, TestBuiltin) {
    check_interpreter("int(\"123\")", "123", VALUE_INT);
    check_interpreter("float(\"12.34\")", "12.34", VALUE_FLOAT);
    check_interpreter("string(123)", "123", VALUE_STRING);
    check_interpreter("print(\"x\", 1)", "NONE", VALUE_NONE);
}

TEST(InterpreterTest, TestHashTable) {
    check_interpreter("h <- HashTable(); h.size()", "0");
    check_interpreter("h <- HashTable(); h.set(\"a\", 1); h.get(\"a\")", "1");
    check_interpreter("h <- HashTable(); h[\"a\"] <- 1; h[\"a\"] <- h[\"a\"] + 1; h[\"a\"]", "2");
    check_interpreter("h <- HashTable(); h.contains(\"missing\")", "0");
    check_interpreter("h <- HashTable(); h.set(\"a\", 1); h.contains(\"a\")", "1");
    check_interpreter("h <- HashTable(); h.set(\"a\", 1); h.remove(\"a\")", "1");
    check_interpreter("h <- HashTable(); h.get(\"missing\")", "NONE", VALUE_NONE);
    check_interpreter("h <- HashTable(); h[1] <- \"one\"; h[1]", "one", VALUE_STRING);
    check_interpreter("h <- HashTable(); h.is_empty()", "1");
    check_interpreter("h <- HashTable(); h.set(\"a\", 1); h.clear(); h.is_empty()", "1");
}

TEST(InterpreterTest, TestInvalidNumericString) {
    EXPECT_THROW(check_interpreter("\"\" + 1.0", "IGNORE", "ANY"), std::invalid_argument);
}

TEST(InterpreterTest, TestAssignment) {
    check_interpreter("x <- 10", "10");
    check_interpreter("x <- 10; x <- x + 5", "15");
}

TEST(ImportTest, TestImportDsaStack) {
    SymbolTable st;
    std::string result = run(
        "test/import_dsa.ps",
        "import dsa\n"
        "stack <- Stack()\n"
        "stack.push(4)\n"
        "result <- stack.pop()\n",
        st);

    EXPECT_EQ(result, "");
    std::shared_ptr<Value> imported_result = st.get("result");
    ASSERT_NE(imported_result.get(), nullptr);
    EXPECT_EQ(imported_result->get_num(), "4");
}

TEST(ImportTest, TestOptimizedDsaCollections) {
    SymbolTable st;
    std::string result = run(
        "test/import_dsa_optimized.ps",
        "import dsa\n"
        "queue <- Queue()\n"
        "queue.enqueue(1)\n"
        "queue.enqueue(2)\n"
        "first <- queue.dequeue()\n"
        "front <- queue.front()\n"
        "list <- LinkedList()\n"
        "list.append(2)\n"
        "list.prepend(1)\n"
        "list.append(3)\n"
        "second <- list.get(2)\n"
        "list.set(2, 20)\n"
        "updated <- list.get(2)\n"
        "popped <- list.pop_front()\n",
        st);

    EXPECT_EQ(result, "");
    EXPECT_EQ(st.get("first")->get_num(), "1");
    EXPECT_EQ(st.get("front")->get_num(), "2");
    EXPECT_EQ(st.get("second")->get_num(), "2");
    EXPECT_EQ(st.get("updated")->get_num(), "20");
    EXPECT_EQ(st.get("popped")->get_num(), "1");
}

TEST(ImportTest, TestDsaTree) {
    SymbolTable st;
    std::string result = run(
        "test/import_dsa_tree.ps",
        "import dsa\n"
        "tree <- Tree()\n"
        "empty <- tree.is_empty()\n"
        "tree.insert(5)\n"
        "tree.insert(3)\n"
        "tree.insert(7)\n"
        "tree.insert(5)\n"
        "has_three <- tree.contains(3)\n"
        "missing <- tree.contains(4)\n"
        "min_value <- tree.min()\n"
        "max_value <- tree.max()\n"
        "tree_size <- tree.size()\n",
        st);

    EXPECT_EQ(result, "");
    EXPECT_EQ(st.get("empty")->get_num(), "1");
    EXPECT_EQ(st.get("has_three")->get_num(), "1");
    EXPECT_EQ(st.get("missing")->get_num(), "0");
    EXPECT_EQ(st.get("min_value")->get_num(), "3");
    EXPECT_EQ(st.get("max_value")->get_num(), "7");
    EXPECT_EQ(st.get("tree_size")->get_num(), "3");
}

TEST(ImportTest, TestDsaDsu) {
    SymbolTable st;
    std::string result = run(
        "test/import_dsa_dsu.ps",
        "import dsa\n"
        "dsu <- DSU()\n"
        "dsu.make_set(1)\n"
        "dsu.make_set(2)\n"
        "dsu.make_set(3)\n"
        "before <- dsu.connected(1, 3)\n"
        "dsu.merge(1, 2)\n"
        "dsu.merge(2, 3)\n"
        "after <- dsu.connected(1, 3)\n"
        "root <- dsu.find(3)\n"
        "set_count <- dsu.size()\n",
        st);

    EXPECT_EQ(result, "");
    EXPECT_EQ(st.get("before")->get_num(), "0");
    EXPECT_EQ(st.get("after")->get_num(), "1");
    EXPECT_EQ(st.get("root")->get_num(), "1");
    EXPECT_EQ(st.get("set_count")->get_num(), "1");
}

TEST(ParserTest, TestMixedMemberIndexChain) {
    check_interpreter(
        "Struct Box:\n"
        "    items\n"
        "    Algorithm Box constructor():\n"
        "        self.items <- {}\n"
        "box <- Box()\n"
        "box.items.push(Box())\n"
        "box.items[1].items <- {7}\n"
        "box.items[1].items[1]",
        "7",
        VALUE_INT);
}

TEST(InterpreterTest, TestBreakContinueAndShortCircuit) {
    SymbolTable st;
    std::string result = run(
        "test/control_flow.ps",
        "sum <- 0\n"
        "i <- 0\n"
        "while i < 10 do\n"
        "    i <- i + 1\n"
        "    if i = 3 then continue\n"
        "    if i = 6 then break\n"
        "    sum <- sum + i\n"
        "empty <- {}\n"
        "guarded <- false and empty[1]\n",
        st);

    EXPECT_EQ(result, "");
    EXPECT_EQ(st.get("sum")->get_num(), "12");
    EXPECT_EQ(st.get("guarded")->get_num(), "0");
}

TEST(InterpreterTest, TestArrayInsertRemove) {
    SymbolTable st;
    std::string result = run(
        "test/array_insert_remove.ps",
        "arr <- {1, 3}\n"
        "arr.insert(2, 2)\n"
        "removed <- arr.remove(1)\n"
        "first <- arr[1]\n"
        "second <- arr[2]\n",
        st);

    EXPECT_EQ(result, "");
    EXPECT_EQ(st.get("removed")->get_num(), "1");
    EXPECT_EQ(st.get("first")->get_num(), "2");
    EXPECT_EQ(st.get("second")->get_num(), "3");
}

TEST(ImportTest, TestOptimizedRbTree) {
    SymbolTable st;
    std::string result = run(
        "test/import_dsa_rbtree.ps",
        "import dsa\n"
        "tree <- RBTree()\n"
        "tree.insert(10)\n"
        "tree.insert(5)\n"
        "tree.insert(15)\n"
        "tree.insert(3)\n"
        "tree.insert(7)\n"
        "tree.insert(12)\n"
        "tree.insert(18)\n"
        "tree.insert(7)\n"
        "has_seven <- tree.contains(7)\n"
        "missing <- tree.contains(9)\n"
        "min_value <- tree.min()\n"
        "max_value <- tree.max()\n"
        "tree_size <- tree.size()\n"
        "root_color <- tree.root_color()\n",
        st);

    EXPECT_EQ(result, "");
    EXPECT_EQ(st.get("has_seven")->get_num(), "1");
    EXPECT_EQ(st.get("missing")->get_num(), "0");
    EXPECT_EQ(st.get("min_value")->get_num(), "3");
    EXPECT_EQ(st.get("max_value")->get_num(), "18");
    EXPECT_EQ(st.get("tree_size")->get_num(), "7");
    EXPECT_EQ(st.get("root_color")->get_num(), "black");
}

TEST(ImportTest, TestOptimizedBTree) {
    SymbolTable st;
    std::string result = run(
        "test/import_dsa_btree.ps",
        "import dsa\n"
        "tree <- BTree(2)\n"
        "for i <- 1 to 20 do\n"
        "    tree.insert(i)\n"
        "tree.insert(10)\n"
        "has_fifteen <- tree.contains(15)\n"
        "missing <- tree.contains(21)\n"
        "min_value <- tree.min()\n"
        "max_value <- tree.max()\n"
        "tree_size <- tree.size()\n"
        "tree_height <- tree.height()\n",
        st);

    EXPECT_EQ(result, "");
    EXPECT_EQ(st.get("has_fifteen")->get_num(), "1");
    EXPECT_EQ(st.get("missing")->get_num(), "0");
    EXPECT_EQ(st.get("min_value")->get_num(), "1");
    EXPECT_EQ(st.get("max_value")->get_num(), "20");
    EXPECT_EQ(st.get("tree_size")->get_num(), "20");
    EXPECT_EQ(st.get("tree_height")->get_num(), "4");
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
