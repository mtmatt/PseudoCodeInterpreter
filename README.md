# PseudoCodeInterpreter

Let's make the pseudo code an actual programming language.

## Data Type

### Int

- A common 64 bit integer.

### Float

- A common 64 bit floating point number.

### String

- `"This is the string"` : We use the quoatation mark to wrap a string.
- `"\""` : This would be considered as `"`
- `"\n"` : Newline
- `"\r"` : Back to first place of the current line
- `"\\"` : This would be considered as a `\`

### Array

- `Initialize by` : `arr <- {1, 2, 3}`
- `Index count from 1`
- `You can put whatever data type you want into array`

## Built in Functions

- `print(s)` : print the data
- `read()` : read one string saperate by space, tab, and newline
- `read_line()` : read one line and return string
- `clear()` : clear the screen
- `quit()` : quit the interpreter
- `int/flaot/string(v)` : data convertion

## Expersion For User

### Operators and Operations

- `a <- 10` : move the value 10 to the variable `a`
- `+` : addition 
- `-` : subtraction
- `*` : multiplication
- `/` : division
- `%` : mod operation
- `^` : power operation

### if statement

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

### for statement

- `for var_name <- start_value to end_value do expr`

```pseudo
for i <- 1 to 10 do 
    i <- i + 1
for i <- 1 to 100 step 10 do
    i <- i + 1
```

### while statement

- `while condition do expr`

```pseudo
while i < 100 and i > 10
    i <- i * 2
    i <- i * 3 - 1
```

### repeat statement

- `repeat expr until condition`

```pseudo
repeat i <- i * 2 until i > 1000
```

### Function

Function begin with `Algorithm`, an example for adding two number would be

```pseudo
Algorithm add(a, b):
    return a + b
```

### Expression Rule

- `statement :`
    - NEWLINE* expr (NEWLINE (TAB)_correct_amount expr)* NEWLINE
    - SEMICOLON* expr (SEMICOLON expr)* SEMICOLON

- `expr :`
    - `comp-expr ((and|or) comp-expr)*`
- `comp-expr :`
    - `not comp-expr`
    - `arith-expr ((EQUAL|NEQ|LESS|GREATER|LEQ|GEQ) arith-expr)*`
- `arith-expr :`
    - `term ((ADD|SUB) term)*`
- `term :`
    - `factor ((MUL|DIV|MOD) factor)*`
- `factor :`
    - `(ADD|SUB) factor`
    - `power`
- `power :`
    - `call (POW factor)*`
- `call :`
    - `atom (LEFT_PAREN expr (COMMA expr)* RIGHT_PAREN)?`
    - `array-access`
- `atom :`
    - `INT|FLOAT|STRING`
    - `IDENTIFIER (ASSIGN expr)?`
    - `LEFT_PAREN expr RIGHT_PAREN`
    - `array-expr`
    - `if-expr`
    - `for-expr`
    - `while-expr`
    - `repeat-expr`
    - `algo-def`
- `array-access :`
    - `atom LEFT_SQUARE expr RIGHT_SQUARE`
- `array-expr :`
    - LEFT_BRACE (expr (COMMA expr)*)? RIGHT_BRACE
- `if-expr :`
    - `if expr then expr (else (if-expr|expr))?`
- `for-expr :`
    - `for IDENTIFIER ASSIGN expr to expr (step)? expr do expr`    
- `while-expr :`
    - `while expr do expr`
- `repeat-expr :`
    - `repeat expr until expr`
- `algo-def`
    - `Algorithm IDENTIFIER? LEFT_PAREN (IDENTIFIER (COMMA IDENTIFIER)*)?  RIGHT_PAREN COLON expr`
