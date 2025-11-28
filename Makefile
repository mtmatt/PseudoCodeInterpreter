VPATH = src
CC = g++
CPPFLAGS = -std=c++17 -O2
TARGET = shell
SRCS = src/color.cpp src/position.cpp src/token.cpp src/node.cpp src/parser.cpp src/lexer.cpp src/symboltable.cpp src/interpreter.cpp src/pseudo.cpp src/shell.cpp
BUILD_DIR = build
OBJS = $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

# Google Test configuration
GTEST_DIR = googletest/googletest
TEST_CPPFLAGS = $(CPPFLAGS) -isystem $(GTEST_DIR)/include -Isrc -pthread --coverage

# Coverage build directory
BUILD_COV_DIR = build_cov
# Test objects (exclude shell.o which contains main)
TEST_OBJS = $(filter-out $(BUILD_COV_DIR)/shell.o, $(SRCS:src/%.cpp=$(BUILD_COV_DIR)/%.o))
TEST_TARGET = run_tests

$(TARGET): $(BUILD_DIR) $(OBJS)
	$(CC) $(CPPFLAGS) $(OBJS) -o $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_COV_DIR):
	mkdir -p $(BUILD_COV_DIR)

# Normal build rules
$(BUILD_DIR)/shell.o:	src/shell.cpp src/interpreter.h src/pseudo.h
	$(CC) -c $(CPPFLAGS) src/shell.cpp -o $@

$(BUILD_DIR)/%.o: src/%.cpp
	$(CC) -c $(CPPFLAGS) $< -o $@

# Coverage build rules
$(BUILD_COV_DIR)/%.o: src/%.cpp | $(BUILD_COV_DIR)
	$(CC) -c $(TEST_CPPFLAGS) $< -o $@

run : $(TARGET)
	./shell

# Google Test rules

GTEST_HEADERS = $(GTEST_DIR)/include/gtest/*.h \
                $(GTEST_DIR)/include/gtest/internal/*.h

$(BUILD_DIR)/gtest-all.o: $(GTEST_DIR)/src/gtest-all.cc $(GTEST_HEADERS) | $(BUILD_DIR)
	$(CC) $(TEST_CPPFLAGS) -I$(GTEST_DIR) -c $(GTEST_DIR)/src/gtest-all.cc -o $@

$(BUILD_DIR)/unittest.o: test/unittest.cpp $(GTEST_HEADERS) | $(BUILD_DIR)
	$(CC) $(TEST_CPPFLAGS) -c test/unittest.cpp -o $@

$(TEST_TARGET): $(BUILD_COV_DIR) $(TEST_OBJS) $(BUILD_DIR)/gtest-all.o $(BUILD_DIR)/unittest.o
	$(CC) $(TEST_CPPFLAGS) $(TEST_OBJS) $(BUILD_DIR)/gtest-all.o $(BUILD_DIR)/unittest.o -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

coverage: test
	gcov -r -o $(BUILD_COV_DIR) src/*.cpp

.PHONY: clean all test coverage
clean:
	rm -rf $(BUILD_DIR) $(BUILD_COV_DIR) $(TARGET) $(TEST_TARGET) *.gcov
all: clean $(TARGET)
