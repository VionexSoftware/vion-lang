<div align="center">
  <img src="https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/assets/vion-file-icon.svg" alt="Vion Language Logo" width="150" />
  <h1>Vion Programming Language</h1>
  <p><strong>Modern. Simple. Fast.</strong></p>

  <p>
    <a href="https://vion.vionex.software">Website</a> •
    <a href="https://vion.vionex.software/docs">Documentation</a> •
    <a href="https://github.com/AlexanderPhan04/vion-lang/releases">Releases</a>
  </p>
</div>

Vion is an open-source scripting language created by Phan Nhật Quân and maintained by Vionex Software. It is a dynamically typed, experimental language built entirely from scratch in modern C++17. Originally started as a learning project for language implementation (Lexer, Parser, AST), Vion has evolved into a fully-fledged language running on a custom **Bytecode Virtual Machine**.

## 🚀 Features (v1.0.0)

- **Bytecode Virtual Machine:** Vion compiles source code into custom bytecode and executes it on a fast, stack-based VM.
- **Garbage Collection:** Tracing mark-and-sweep garbage collector for automatic memory management of arrays, maps, and closures.
- **Exception Handling:** Stack-unwinding `try / catch` blocks to gracefully recover from runtime errors.
- **Module System:** Organize your code with isolated file imports (`let math = import "math.vion"`).
- **First-class Functions & Closures:** True lexical scoping. Pass functions as arguments or return them.
- **Data Structures:** Native support for Arrays and Hash Maps with dot-notation method access.
- **Native Standard Library:** Includes File I/O (`file_read`, `file_write`), blazing fast JSON parsing (`json_parse`, `json_stringify`), and string manipulation (`split`, `to_upper`, `to_lower`).

---

## ⚡ Quick Start

### Installation (Windows)
Open PowerShell (**no administrator privileges required**) and run the quick installer:
```powershell
irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/install-online-windows.ps1 | iex
```

*(For better security, you can download and review the script before running it):*
```powershell
irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/install-online-windows.ps1 -OutFile install-vion.ps1
notepad .\install-vion.ps1
powershell -ExecutionPolicy Bypass -File .\install-vion.ps1
```

This will download the latest `vion.exe`, add it to your User PATH, and install the VS Code syntax highlighting extension.

#### Uninstallation
To completely remove Vion and its VS Code extension, run:
```powershell
irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/uninstall-windows.ps1 | iex
```

### Hello World
```javascript
// main.vion
print "Hello, World!"
```
Run it:
```bash
vion run main.vion
```

---

## 📖 Language Tour

### Variables & Data Types
```javascript
let name = "Vion"          // String
let version = 1.0          // Number
let isAwesome = true       // Boolean
let empty = nil            // Nil

// String Interpolation
print "Welcome to {name} v{version}!"
```

### Data Structures & JSON
```javascript
let config = {
    "host": "localhost",
    "port": 8080
}
config.environment = "production"

let jsonStr = json_stringify(config)
print jsonStr
```

### File I/O
```javascript
let content = "Log started at " + os_env("USERNAME")
write_file("server.log", content)

let readBack = read_file("server.log")
print readBack
```

### Functions & Closures
```javascript
fn makeAdder(x) {
    fn add(y) {
        return x + y
    }
    return add
}

let add5 = makeAdder(5)
print add5(10) // 15
```

### Try / Catch
```javascript
try {
    let result = 10 / 0
    print result
} catch err {
    print "Error caught: ", err
}
```

### Modules
**math.vion**
```javascript
let PI = 3.14159
fn add(a, b) { return a + b }
```

**main.vion**
```javascript
let math = import "math.vion"
print math.PI        // 3.14159
print math.add(5, 3) // 8
```

---

## 🛠️ Building from Source

Requirements:
- C++17 compiler (GCC, Clang, or MSVC)
- CMake 3.20+

```bash
git clone https://github.com/AlexanderPhan04/vion-lang.git
cd vion-lang
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

## 📝 CLI Commands

Vion ships with a helpful CLI for debugging and running code:

- `vion run <file>`: Execute a Vion script.
- `vion repl`: Start the interactive REPL.
- `vion ast <file>`: Print the Abstract Syntax Tree of a file.
- `vion tokens <file>`: Print the Lexer tokens.
- `vion eval "print 1 + 1"`: Execute inline code.

---

## 📄 License
MIT License. Created by Phan Nhật Quân and maintained by Vionex Software.
