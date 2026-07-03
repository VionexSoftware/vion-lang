# Vion Language Specification v0.2

## Goal

Vion is a small toy programming language for learning how lexer, parser, AST, interpreter, and CLI work.

## Supported syntax

### Variables

```vion
let x = 10
let name = "Vion"
let active = true
x = x + 1
```

### Print

```vion
print x
print "hello"
print x + 20
```

### Blocks and Scope

Blocks create lexical scope:

```vion
let name = "global"

{
  let name = "block"
  print name
}

print name
```

### If / else

```vion
if x >= 10 {
  print "large"
} else {
  print "small"
}
```

### While

```vion
let index = 0

while index < 3 {
  print index
  index = index + 1
}
```

### Functions

```vion
fn add(a, b) {
  return a + b
}

print add(10, 20)
```

Functions are values and close over their parent scope:

```vion
fn makeAdder(x) {
  fn addInner(y) {
    return x + y
  }

  return addInner
}

let addTwo = makeAdder(2)
print addTwo(5)
```

### Comments

```vion
// This is a comment
```

## Supported value types

- number
- string
- boolean
- function
- nil

## Supported operators

- arithmetic: `+ - * /`
- comparison: `> >= < <=`
- equality: `== !=`
- logical: `and or !`
- grouping: `( )`
- call: `name(arguments)`

## Not supported yet

- arrays
- objects
- imports/modules
- type checker
- bytecode
- native compilation

## CLI

Vion programs can be run directly:

```powershell
vion main.vion
```

Useful command aliases:

- `vion -v` / `vion --version`
- `vion -h` / `vion --help`
- `vion -r main.vion` / `vion run main.vion`
- `vion -t main.vion` / `vion tokens main.vion`
- `vion -a main.vion` / `vion ast main.vion`
- `vion -c main.vion` / `vion check main.vion`
- `vion -e "print 1 + 2"` / `vion eval "print 1 + 2"`
- `vion -i` / `vion repl`
