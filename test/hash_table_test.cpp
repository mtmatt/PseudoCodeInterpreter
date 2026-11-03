#include <gtest/gtest.h>
#include <pseudo.h>
#include <symboltable.h>
#include <value.h>

#include <string>

namespace {

class HashTableLibraryTest : public testing::Test {
   protected:
    std::string Run(const std::string& source) {
        return run("test/hash_table_library.ps", "import dsa\n" + source, symbols);
    }

    std::string ValueOf(const std::string& name) { return symbols.get(name)->get_num(); }

    SymbolTable symbols;
};

TEST_F(HashTableLibraryTest, LinearProbingWrapsUpdatesAndReusesDeletedSlots) {
    ASSERT_EQ(Run(R"(
table <- LinearProbingHashTable(5)
table.set(4, 40)
table.set(9, 90)
table.set(14, 140)
table.set(19, 190)
table.set(24, 240)
removed <- table.remove(9)
after_deleted <- table.get(14)
updated <- table.set(24, 241)
size_after_update <- table.size()
reused <- table.set(29, 290)
full_result <- table.set(34, 340)
full_contains <- table.contains(34)
missing_get <- table.get(39)
missing_remove <- table.remove(39)
last_value <- table.get(24)
reused_value <- table.get(29)
final_size <- table.size()
load <- table.load_factor()
)"),
              "");

    EXPECT_EQ(ValueOf("removed"), "90");
    EXPECT_EQ(ValueOf("after_deleted"), "140");
    EXPECT_EQ(ValueOf("updated"), "241");
    EXPECT_EQ(ValueOf("size_after_update"), "4");
    EXPECT_EQ(ValueOf("reused"), "290");
    EXPECT_EQ(ValueOf("full_result"), "0");
    EXPECT_EQ(ValueOf("full_contains"), "0");
    EXPECT_EQ(ValueOf("missing_get"), "0");
    EXPECT_EQ(ValueOf("missing_remove"), "0");
    EXPECT_EQ(ValueOf("last_value"), "241");
    EXPECT_EQ(ValueOf("reused_value"), "290");
    EXPECT_EQ(ValueOf("final_size"), "5");
    EXPECT_DOUBLE_EQ(symbols.get("load")->as_double(), 1.0);
}

TEST_F(HashTableLibraryTest, LinearProbingTerminatesWithOnlyTombstones) {
    ASSERT_EQ(Run(R"(
table <- LinearProbingHashTable(2)
table.set(0, 10)
table.set(2, 20)
table.remove(0)
table.remove(2)
missing <- table.contains(4)
missing_value <- table.get(4)
missing_remove <- table.remove(4)
empty <- table.is_empty()
table.set(-2, 30)
table.set(4, 40)
negative <- table.get(-2)
positive <- table.get(4)
count <- table.size()
single <- LinearProbingHashTable(1)
single.set(0, 1)
single_update <- single.set(0, 2)
single_full <- single.set(-1, 3)
single.remove(0)
single.set(-1, 4)
single_value <- single.get(-1)
)"),
              "");

    EXPECT_EQ(ValueOf("missing"), "0");
    EXPECT_EQ(ValueOf("missing_value"), "0");
    EXPECT_EQ(ValueOf("missing_remove"), "0");
    EXPECT_EQ(ValueOf("empty"), "1");
    EXPECT_EQ(ValueOf("negative"), "30");
    EXPECT_EQ(ValueOf("positive"), "40");
    EXPECT_EQ(ValueOf("count"), "2");
    EXPECT_EQ(ValueOf("single_update"), "2");
    EXPECT_EQ(ValueOf("single_full"), "0");
    EXPECT_EQ(ValueOf("single_value"), "4");
}

TEST_F(HashTableLibraryTest, ChainingHandlesLongCollisionsAndAllRemovalPositions) {
    ASSERT_EQ(Run(R"(
table <- ChainedHashTable(2)
table.set(-4, "first")
table.set(-2, "second")
table.set(0, "third")
table.set(2, "fourth")
table.set(4, "fifth")
updated <- table.set(0, "updated")
size_after_update <- table.size()
load <- table.load_factor()
removed_first <- table.remove(-4)
removed_middle <- table.remove(0)
removed_last <- table.remove(2)
remaining_negative <- table.get(-2)
remaining_positive <- table.get(4)
removed_contains <- table.contains(0)
missing_remove <- table.remove(100)
table.remove(-2)
table.remove(4)
empty <- table.is_empty()
count <- table.size()
)"),
              "");

    EXPECT_EQ(ValueOf("updated"), "updated");
    EXPECT_EQ(ValueOf("size_after_update"), "5");
    EXPECT_DOUBLE_EQ(symbols.get("load")->as_double(), 2.5);
    EXPECT_EQ(ValueOf("removed_first"), "first");
    EXPECT_EQ(ValueOf("removed_middle"), "updated");
    EXPECT_EQ(ValueOf("removed_last"), "fourth");
    EXPECT_EQ(ValueOf("remaining_negative"), "second");
    EXPECT_EQ(ValueOf("remaining_positive"), "fifth");
    EXPECT_EQ(ValueOf("removed_contains"), "0");
    EXPECT_EQ(ValueOf("missing_remove"), "0");
    EXPECT_EQ(ValueOf("empty"), "1");
    EXPECT_EQ(ValueOf("count"), "0");
}

TEST_F(HashTableLibraryTest, DynamicGrowthPreservesSignedKeysAndValues) {
    ASSERT_EQ(Run(R"(
table <- DynamicHashTable(1)
for key <- -40 to 40 do
    table.set(key, key * key)
all_present <- true
for key <- -40 to 40 do
    if table.contains(key) = false then all_present <- false
    if table.get(key) != key * key then all_present <- false
count <- table.size()
capacity_before_update <- table.capacity()
updated <- table.set(-40, "updated")
capacity_after_update <- table.capacity()
updated_value <- table.get(-40)
count_after_update <- table.size()
load <- table.load_factor()
)"),
              "");

    EXPECT_EQ(ValueOf("all_present"), "1");
    EXPECT_EQ(ValueOf("count"), "81");
    EXPECT_EQ(ValueOf("capacity_before_update"), "256");
    EXPECT_EQ(ValueOf("capacity_after_update"), "256");
    EXPECT_EQ(ValueOf("updated"), "updated");
    EXPECT_EQ(ValueOf("updated_value"), "updated");
    EXPECT_EQ(ValueOf("count_after_update"), "81");
    EXPECT_LE(symbols.get("load")->as_double(), 0.5);
}

TEST_F(HashTableLibraryTest, DynamicDeletionCleanupPreservesCollisionChains) {
    ASSERT_EQ(Run(R"(
table <- DynamicHashTable(16)
for i <- 0 to 7 do
    table.set(i * 16 + 15, i + 100)
removed_values <- 0
for i <- 0 to 3 do
    removed_values <- removed_values + table.remove(i * 16 + 15)
remaining_ok <- true
for i <- 4 to 7 do
    if table.get(i * 16 + 15) != i + 100 then remaining_ok <- false
    if table.contains((i - 4) * 16 + 15) then remaining_ok <- false
for i <- 1 to 100 do
    table.set(-i, i)
    table.remove(-i)
count <- table.size()
capacity <- table.capacity()
last_value <- table.get(127)
missing <- table.remove(10000)
count_after_missing <- table.size()
)"),
              "");

    EXPECT_EQ(ValueOf("removed_values"), "406");
    EXPECT_EQ(ValueOf("remaining_ok"), "1");
    EXPECT_EQ(ValueOf("count"), "4");
    EXPECT_EQ(ValueOf("capacity"), "16");
    EXPECT_EQ(ValueOf("last_value"), "107");
    EXPECT_EQ(ValueOf("missing"), "0");
    EXPECT_EQ(ValueOf("count_after_missing"), "4");
}

TEST_F(HashTableLibraryTest, EmptyZeroValuesAndClearWorkForEveryTable) {
    for (const std::string constructor :
         {"LinearProbingHashTable(3)", "ChainedHashTable(3)", "DynamicHashTable(3)"}) {
        SCOPED_TRACE(constructor);
        ASSERT_EQ(Run("table <- " + constructor + R"(
initial_empty <- table.is_empty()
initial_size <- table.size()
initial_get <- table.get(0)
initial_remove <- table.remove(0)
initial_contains <- table.contains(0)
zero_set <- table.set(0, 0)
zero_contains <- table.contains(0)
zero_get <- table.get(0)
none_set <- table.set(-1, none)
none_contains <- table.contains(-1)
before_clear <- table.capacity()
table.clear()
empty_after_clear <- table.is_empty()
size_after_clear <- table.size()
after_clear <- table.capacity()
contains_after_clear <- table.contains(0)
load_after_clear <- table.load_factor()
table.set(-1, 7)
reused <- table.get(-1)
)"),
                  "");

        EXPECT_EQ(ValueOf("initial_empty"), "1");
        EXPECT_EQ(ValueOf("initial_size"), "0");
        EXPECT_EQ(ValueOf("initial_get"), "0");
        EXPECT_EQ(ValueOf("initial_remove"), "0");
        EXPECT_EQ(ValueOf("initial_contains"), "0");
        EXPECT_EQ(ValueOf("zero_set"), "0");
        EXPECT_EQ(ValueOf("zero_contains"), "1");
        EXPECT_EQ(ValueOf("zero_get"), "0");
        EXPECT_EQ(ValueOf("none_set"), "0");
        EXPECT_EQ(ValueOf("none_contains"), "1");
        EXPECT_EQ(ValueOf("empty_after_clear"), "1");
        EXPECT_EQ(ValueOf("size_after_clear"), "0");
        EXPECT_EQ(ValueOf("before_clear"), ValueOf("after_clear"));
        EXPECT_EQ(ValueOf("contains_after_clear"), "0");
        EXPECT_DOUBLE_EQ(symbols.get("load_after_clear")->as_double(), 0.0);
        EXPECT_EQ(ValueOf("reused"), "7");
    }
}

TEST_F(HashTableLibraryTest, ArbitraryValuesRemainUsableAfterStorage) {
    for (const std::string constructor :
         {"LinearProbingHashTable(5)", "ChainedHashTable(1)", "DynamicHashTable(1)"}) {
        SCOPED_TRACE(constructor);
        ASSERT_EQ(Run("table <- " + constructor + R"(
array <- {1, 2}
table.set(0, array)
table.set(1, "hello")
native <- HashTable()
native.set("name", "nested")
table.set(2, native)
table.get(0).push(3)
array_size <- array.size()
array_value <- table.get(0)[3]
string_value <- table.get(1)
nested_value <- table.get(2).get("name")
removed_array <- table.remove(0)
removed_size <- removed_array.size()
removed_contains <- table.contains(0)
)"),
                  "");

        EXPECT_EQ(ValueOf("array_size"), "3");
        EXPECT_EQ(ValueOf("array_value"), "3");
        EXPECT_EQ(ValueOf("string_value"), "hello");
        EXPECT_EQ(ValueOf("nested_value"), "nested");
        EXPECT_EQ(ValueOf("removed_size"), "3");
        EXPECT_EQ(ValueOf("removed_contains"), "0");
    }
}

TEST_F(HashTableLibraryTest, SignedIntegerExtremesDoNotOverflowHashNormalization) {
    for (const std::string constructor :
         {"LinearProbingHashTable(5)", "ChainedHashTable(5)", "DynamicHashTable(5)"}) {
        SCOPED_TRACE(constructor);
        ASSERT_EQ(Run("table <- " + constructor + R"(
maximum <- 9223372036854775807
minimum <- -9223372036854775807 - 1
table.set(maximum, "maximum")
table.set(minimum, "minimum")
table.set(0, "zero")
maximum_value <- table.get(maximum)
minimum_value <- table.get(minimum)
zero_value <- table.get(0)
removed_minimum <- table.remove(minimum)
contains_maximum <- table.contains(maximum)
contains_minimum <- table.contains(minimum)
)"),
                  "");

        EXPECT_EQ(ValueOf("maximum_value"), "maximum");
        EXPECT_EQ(ValueOf("minimum_value"), "minimum");
        EXPECT_EQ(ValueOf("zero_value"), "zero");
        EXPECT_EQ(ValueOf("removed_minimum"), "minimum");
        EXPECT_EQ(ValueOf("contains_maximum"), "1");
        EXPECT_EQ(ValueOf("contains_minimum"), "0");
    }
}

TEST_F(HashTableLibraryTest, NonpositiveCapacitiesNormalizeToOne) {
    for (const std::string constructor :
         {"LinearProbingHashTable(0)", "LinearProbingHashTable(-3)", "ChainedHashTable(0)",
          "ChainedHashTable(-3)", "DynamicHashTable(0)", "DynamicHashTable(-3)"}) {
        SCOPED_TRACE(constructor);
        ASSERT_EQ(Run("table <- " + constructor + R"(
initial_capacity <- table.capacity()
table.set(-1, 42)
value <- table.get(-1)
count <- table.size()
)"),
                  "");
        EXPECT_EQ(ValueOf("initial_capacity"), "1");
        EXPECT_EQ(ValueOf("value"), "42");
        EXPECT_EQ(ValueOf("count"), "1");
    }
}

TEST_F(HashTableLibraryTest, NativeHashTableStillSupportsStringKeys) {
    ASSERT_EQ(Run(R"(
table <- HashTable()
table.set("name", "native")
table["count"] <- 3
name <- table.get("name")
count <- table["count"]
missing <- table.contains("missing")
removed <- table.remove("name")
table.clear()
empty <- table.is_empty()
)"),
              "");
    EXPECT_EQ(ValueOf("name"), "native");
    EXPECT_EQ(ValueOf("count"), "3");
    EXPECT_EQ(ValueOf("missing"), "0");
    EXPECT_EQ(ValueOf("removed"), "native");
    EXPECT_EQ(ValueOf("empty"), "1");
}

}  // namespace
