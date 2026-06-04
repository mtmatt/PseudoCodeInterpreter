# PseudoCodeInterpreter

Transform pseudo-code into an executable programming language.

## Data Types

### Integer
- A standard 64-bit integer.

### Float
- A standard 64-bit floating-point number.

### String
- `"This is a string"`: Strings are wrapped in double quotation marks.
- `"\""`: Escaped double quote (`"`).
- `"\n"`: Newline.
- `"\r"`: Carriage return (moves the cursor to the beginning of the line).
- `"\\"`: Escaped backslash (`\`).

### Array
- **Initialization**: `arr <- {1, 2, 3}`
- Arrays are **1-indexed**.
- Elements of different data types can be stored in the same array.
- Methods: `push(value)`, `pop()`, `insert(index, value)`, `remove(index)`, `resize(size)`, `size()`, `back()`.

## Built-in Functions

- `print(s)` : Prints the data to the console.
- `read()` : Reads a single string separated by space, tab, or newline.
- `read_line()` : Reads an entire line and returns it as a string.
- `clear()` : Clears the terminal screen.
- `quit()` : Exits the interpreter.
- `int(v)`, `float(v)`, `string(v)` : Type conversion functions.

## Imports

Use `import` to load another pseudocode file before the current file is parsed.
Imported definitions share the same global symbol table as the importing file.

```pseudo
import dsa
import "relative/path/to/module.ps"
```

Bare module names resolve from `lib/<name>.ps`. Quoted paths may point to a
specific `.ps` file. Imports are loaded once per run and circular imports are
reported as errors.

## Standard Library

The `dsa` library provides data-structure types:

- `LinkedList`: `append`, `prepend`, `pop_front`, `get`, `set`, `contains`, `size`, `is_empty`
- `Stack`: `push`, `pop`, `peek`, `size`, `is_empty`
- `Queue`: `enqueue`, `dequeue`, `front`, `size`, `is_empty`
- `Tree`: `insert`, `contains`, `min`, `max`, `size`, `is_empty`
- `RBTree`: red-black tree with `insert`, `contains`, `min`, `max`, `size`, `is_empty`, `root_color`
- `BTree`: degree adjustable B-tree with `insert`, `contains`, `min`, `max`, `size`, `is_empty`, `height`
- `DSU`: `make_set`, `find`, `merge`, `connected`, `size`

```pseudo
import dsa

stack <- Stack()
stack.push(10)
print(stack.pop())
```

## Zed Editor Support

This repository includes an unpublished Zed dev extension for `.ps` files. It
provides syntax highlighting through the bundled Tree-sitter grammar and starts
the `pseudo-lsp` language server for diagnostics and completions.

Build the interpreter and language server first:

```sh
make
```

Then install the extension in Zed:

1. Open Zed.
2. Run `zed: install dev extension` from the command palette.
3. Select the `editors/zed` directory in this repository.
4. Open a `.ps` file.

When editing files inside this repository, the extension can launch
`./pseudo-lsp` from the worktree. To use the extension in other projects, put
`pseudo-lsp` on `PATH` before launching Zed:

```sh
make lsp
sudo cp pseudo-lsp /usr/local/bin/pseudo-lsp
```

If syntax highlighting does not appear after reinstalling, run
`zed: open log` and check for grammar compilation errors. The Tree-sitter
grammar lives in the `editors/zed/tree-sitter-pseudocode` submodule and is
pinned in `editors/zed/extension.toml`.

## Expressions & Syntax

### Operators
- `a <- 10` : Assignment (assigns the value `10` to the variable `a`).
- `+` : Addition 
- `-` : Subtraction
- `*` : Multiplication
- `/` : Division
- `%` : Modulo operation
- `^` : Power (exponentiation)
- `=` : Equality comparison
- `!=` : Inequality comparison
- `<` : Less than
- `>` : Greater than
- `<=` : Less than or equal to
- `>=` : Greater than or equal to
- `and` : Logical AND
- `or` : Logical OR
- `not` : Logical NOT

### Control Structures

#### If Statement
- `if condition then expression`
- `if condition then expression else expression`

```pseudo
if a < b and a >= 10 then
    a <- 10
else if a < 20 then
    a <- 1.5
else
    a <- 8
```

#### For Statement
- `for var_name <- start_value to end_value [step value] do expr`

```pseudo
for i <- 1 to 10 do 
    i <- i + 1

for i <- 1 to 100 step 10 do
    i <- i + 1
```

#### While Statement
- `while condition do expr`

```pseudo
while i < 100 and i > 10 do
    i <- i * 2
    i <- i * 3 - 1
```

Loops support `break` and `continue`.

#### Repeat Statement
- `repeat expr until condition`

```pseudo
repeat i <- i * 2 until i > 1000
```

### Functions

Functions begin with the `Algorithm` keyword. An example of adding two numbers:

```pseudo
Algorithm add(a, b):
    return a + b
```

### Structs

You can define custom data structures with properties and methods using the `Struct` keyword. Class attributes are declared at the beginning of the struct, and methods can be defined either inside or outside the structure block. You can use the scope resolution operator `::` to define methods outside the block.

```pseudo
Struct List:
    head
    tail

    Algorithm List constructor(_head, _tail):
        self.head <- _head
        self.tail <- _tail

    Algorithm List destructor():
        print("Destroying List instance")

    Algorithm get_head():
        return self.head

    Algorithm set_head(h):
        self.head <- h

Algorithm List::test():
    print(self.head)
    print(self.tail)
```

**Usage**

You can instantiate a struct by calling its name like a function, passing the necessary arguments to its constructor. Properties and methods are accessed using standard dot (`.`) notation.

```pseudo
Algorithm main():
    l <- List(1, 2)
    print(l.head)
    
    l.set_head(10)
    print(l.get_head())
    
    l.test()
```

## Language Formal Grammar

- **`statement`** :
    - `NEWLINE* expr (NEWLINE TAB_correct_amount expr)* NEWLINE`
    - `SEMICOLON* expr (SEMICOLON expr)* SEMICOLON`

- **`expr`** :
    - `comp-expr ((and|or) comp-expr)*`
- **`comp-expr`** :
    - `not comp-expr`
    - `arith-expr ((EQUAL|NEQ|LESS|GREATER|LEQ|GEQ) arith-expr)*`
- **`arith-expr`** :
    - `term ((ADD|SUB) term)*`
- **`term`** :
    - `factor ((MUL|DIV|MOD) factor)*`
- **`factor`** :
    - `(ADD|SUB) factor`
    - `power`
- **`power`** :
    - `call (POW factor)*`
- **`call`** :
    - `atom (LEFT_PAREN expr (COMMA expr)* RIGHT_PAREN)?`
    - `array-access`
- **`atom`** :
    - `INT | FLOAT | STRING`
    - `IDENTIFIER (ASSIGN expr)?`
    - `LEFT_PAREN expr RIGHT_PAREN`
    - `array-expr`
    - `if-expr`
    - `for-expr`
    - `while-expr`
    - `repeat-expr`
    - `algo-def`
- **`array-access`** :
    - `atom LEFT_SQUARE expr RIGHT_SQUARE`
- **`array-expr`** :
    - `LEFT_BRACE (expr (COMMA expr)*)? RIGHT_BRACE`
- **`if-expr`** :
    - `if expr then expr (else (if-expr|expr))?`
- **`for-expr`** :
    - `for IDENTIFIER ASSIGN expr to expr (step)? expr do expr`    
- **`while-expr`** :
    - `while expr do expr`
- **`repeat-expr`** :
    - `repeat expr until expr`
- **`algo-def`** :
    - `Algorithm IDENTIFIER? LEFT_PAREN (IDENTIFIER (COMMA IDENTIFIER)*)? RIGHT_PAREN COLON expr`
