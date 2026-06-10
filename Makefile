VPATH = src
CC = g++
CPPFLAGS = -std=c++17 -O2
TARGET = pseudo
SRCS = src/color.cpp src/position.cpp src/token.cpp src/node.cpp src/parser.cpp src/lexer.cpp src/symboltable.cpp src/jit.cpp src/interpreter.cpp src/pseudo.cpp src/shell.cpp src/error.cpp
LSP_TARGET = pseudo-lsp
LSP_SRCS = src/color.cpp src/position.cpp src/token.cpp src/node.cpp src/parser.cpp src/lexer.cpp src/lsp.cpp
BUILD_DIR = build
OBJS = $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)
LSP_OBJS = $(LSP_SRCS:src/%.cpp=$(BUILD_DIR)/%.o)
HEADERS = $(wildcard src/*.h)

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

$(LSP_TARGET): $(BUILD_DIR) $(LSP_OBJS)
	$(CC) $(CPPFLAGS) $(LSP_OBJS) -o $(LSP_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_COV_DIR):
	mkdir -p $(BUILD_COV_DIR)

# Normal build rules
$(BUILD_DIR)/shell.o:	src/shell.cpp $(HEADERS)
	$(CC) -c $(CPPFLAGS) src/shell.cpp -o $@

$(BUILD_DIR)/%.o: src/%.cpp $(HEADERS)
	$(CC) -c $(CPPFLAGS) $< -o $@

# Coverage build rules
$(BUILD_COV_DIR)/%.o: src/%.cpp $(HEADERS) | $(BUILD_COV_DIR)
	$(CC) -c $(TEST_CPPFLAGS) $< -o $@

run : $(TARGET)
	./shell

# Runtime library for compiled programs (interpreter core + rt_* shims)
# Prefer the system ar: GNU binutils ar produces archives Apple's ld rejects.
AR := $(shell test -x /usr/bin/ar && echo /usr/bin/ar || echo ar)
RT_LIB = $(BUILD_DIR)/libpseudort.a
RT_OBJS = $(filter-out $(BUILD_DIR)/shell.o,$(OBJS)) $(BUILD_DIR)/runtime.o

$(RT_LIB): $(RT_OBJS)
	$(AR) rcs $@ $(RT_OBJS)

runtime: $(BUILD_DIR) $(RT_LIB)

# LLVM AOT compiler (optional; requires LLVM, e.g. `brew install llvm`)
LLVM_CONFIG ?= $(shell command -v llvm-config 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/llvm-config)
LLVM_INCLUDEDIR = $(shell $(LLVM_CONFIG) --includedir)
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir)
LLVM_COMPILE_FLAGS = -I$(LLVM_INCLUDEDIR) -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS
PSEUDOC_TARGET = pseudoc
PSEUDOC_OBJS = $(filter-out $(BUILD_DIR)/shell.o,$(OBJS)) $(BUILD_DIR)/compiler.o $(BUILD_DIR)/pseudoc.o

$(BUILD_DIR)/compiler.o: src/compiler.cpp $(HEADERS) | $(BUILD_DIR)
	$(CC) -c $(CPPFLAGS) $(LLVM_COMPILE_FLAGS) src/compiler.cpp -o $@

$(BUILD_DIR)/pseudoc.o: src/pseudoc.cpp $(HEADERS) | $(BUILD_DIR)
	$(CC) -c $(CPPFLAGS) $(LLVM_COMPILE_FLAGS) src/pseudoc.cpp -o $@

$(PSEUDOC_TARGET): $(BUILD_DIR) $(PSEUDOC_OBJS) $(RT_LIB)
	$(CC) $(CPPFLAGS) $(PSEUDOC_OBJS) -L$(LLVM_LIBDIR) -lLLVM -Wl,-rpath,$(LLVM_LIBDIR) -o $(PSEUDOC_TARGET)

compiler: $(PSEUDOC_TARGET) runtime

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
	gcov -r -o $(BUILD_COV_DIR) $(filter-out src/shell.cpp, $(SRCS))

test-compiler: $(TARGET)
	bash test/compiler_tests.sh

.PHONY: clean all test coverage lsp test-compiler runtime compiler
lsp: $(LSP_TARGET)

clean:
	rm -rf $(BUILD_DIR) $(BUILD_COV_DIR) $(TARGET) $(LSP_TARGET) $(TEST_TARGET) *.gcov
all: clean $(TARGET) $(LSP_TARGET)
install:
	cp $(TARGET) /usr/local/bin/
	cp $(LSP_TARGET) /usr/local/bin/
