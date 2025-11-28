VPATH = src
CC = g++
CPPFLAGS = -std=c++17 -O2
TARGET = shell
SRCS = src/color.cpp src/position.cpp src/token.cpp src/node.cpp src/parser.cpp src/lexer.cpp src/symboltable.cpp src/interpreter.cpp src/pseudo.cpp src/shell.cpp
BUILD_DIR = build
OBJS = $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

# Google Test configuration
GTEST_DIR = googletest/googletest
TEST_CPPFLAGS = $(CPPFLAGS) -isystem $(GTEST_DIR)/include -Isrc -pthread

# Test objects (exclude shell.o which contains main)
TEST_OBJS = $(filter-out $(BUILD_DIR)/shell.o, $(OBJS))
TEST_TARGET = run_tests

$(TARGET): $(BUILD_DIR) $(OBJS)
	$(CC) $(CPPFLAGS) $(OBJS) -o $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/shell.o:	src/shell.cpp src/interpreter.h src/pseudo.h
	$(CC) -c $(CPPFLAGS) src/shell.cpp -o $@

$(BUILD_DIR)/color.o: src/color.cpp src/color.h
	$(CC) -c $(CPPFLAGS) src/color.cpp -o $@

$(BUILD_DIR)/position.o: src/position.cpp src/position.h
	$(CC) -c $(CPPFLAGS) src/position.cpp -o $@

$(BUILD_DIR)/token.o: src/token.cpp src/token.h
	$(CC) -c $(CPPFLAGS) src/token.cpp -o $@

$(BUILD_DIR)/node.o: src/node.cpp src/node.h
	$(CC) -c $(CPPFLAGS) src/node.cpp -o $@

$(BUILD_DIR)/parser.o: src/parser.cpp src/parser.h
	$(CC) -c $(CPPFLAGS) src/parser.cpp -o $@

$(BUILD_DIR)/lexer.o: src/lexer.cpp src/lexer.h
	$(CC) -c $(CPPFLAGS) src/lexer.cpp -o $@

$(BUILD_DIR)/symboltable.o: src/symboltable.cpp src/symboltable.h
	$(CC) -c $(CPPFLAGS) src/symboltable.cpp -o $@

$(BUILD_DIR)/interpreter.o: value.h src/interpreter.cpp src/interpreter.h
	$(CC) -c $(CPPFLAGS) src/interpreter.cpp -o $@

$(BUILD_DIR)/pseudo.o: value.h src/pseudo.cpp src/pseudo.h
	$(CC) -c $(CPPFLAGS) src/pseudo.cpp -o $@

run : $(TARGET)
	./shell

# Google Test rules

GTEST_HEADERS = $(GTEST_DIR)/include/gtest/*.h \
                $(GTEST_DIR)/include/gtest/internal/*.h

$(BUILD_DIR)/gtest-all.o: $(GTEST_DIR)/src/gtest-all.cc $(GTEST_HEADERS)
	$(CC) $(TEST_CPPFLAGS) -I$(GTEST_DIR) -c $(GTEST_DIR)/src/gtest-all.cc -o $@

$(BUILD_DIR)/unittest.o: test/unittest.cpp $(GTEST_HEADERS)
	$(CC) $(TEST_CPPFLAGS) -c test/unittest.cpp -o $@

$(TEST_TARGET): $(BUILD_DIR) $(TEST_OBJS) $(BUILD_DIR)/gtest-all.o $(BUILD_DIR)/unittest.o
	$(CC) $(TEST_CPPFLAGS) $(TEST_OBJS) $(BUILD_DIR)/gtest-all.o $(BUILD_DIR)/unittest.o -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

.PHONY: clean all test
clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TEST_TARGET)
all: clean $(TARGET)
