# Vion Language

Vion is a small programming language project written in C++17. It is built as a learning project for language implementation concepts: lexer, parser, AST, interpreter, lexical scope, functions, closures, and CLI tooling.

## Project Structure

```text
.
|-- src/
|   |-- lexer/          # Token definitions and scanner
|   |-- parser/         # AST and recursive descent parser
|   |-- runtime/        # Runtime values and environments
|   |-- interpreter/    # Tree-walk interpreter
|   `-- main.cpp        # CLI entry point
|-- examples/           # Example .vion programs
|-- tests/              # CLI integration tests
|-- scripts/            # Install and uninstall scripts
|-- tools/              # Editor/tooling integrations
|-- assets/             # Vion visual assets
|-- CMakeLists.txt      # Build, install, test, and package config
|-- INSTALL.md          # Install instructions
|-- LANGUAGE_SPEC.md    # Current language syntax and semantics
|-- ROADMAP.md          # Planned milestones
|-- LICENSE
|-- .gitattributes
`-- .gitignore
```

## Current Version

Vion is currently at `v0.2.0`.

Supported features include:

- variables and reassignment
- numbers, strings, booleans, and `nil`
- arithmetic, comparison, equality, and logical operators
- lexical block scope
- `if / else`
- `while`
- functions, recursion, closures, and `return`
- CLI commands for tokenizing, AST output, and running programs
- direct file run with `vion main.vion`
- short CLI flags such as `vion -v`, `vion -h`, `vion -e`, and `vion -c`

## Quick Start

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build
.\build\Debug\vion.exe .\examples\features.vion
```

## Install

Online install without cloning:

```powershell
irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/install-online-windows.ps1 | iex
```

The online installer asks once for permission, then downloads the latest GitHub Release, installs the CLI, updates user PATH, installs VS Code support, creates `Documents\Vion\main.vion`, and opens the starter project when the `code` command is available. Users do not need to set environment variables manually.

Local source install:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-windows.ps1
```

Open a new terminal, then run:

```powershell
vion -v
vion main.vion
```

Uninstall:

```powershell
powershell -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\Vion\docs\uninstall-windows.ps1"
```

## Test

```powershell
cmake --build build --target check
ctest --test-dir build -C Debug --output-on-failure
```

## CLI Commands

```powershell
vion main.vion                  # Run a file
vion run main.vion              # Run a file
vion -r main.vion               # Run a file
vion tokens main.vion           # Print tokens
vion -t main.vion               # Print tokens
vion ast main.vion              # Print AST
vion -a main.vion               # Print AST
vion check main.vion            # Parse-check a file
vion -c main.vion               # Parse-check a file
vion eval "print 1 + 2"         # Run inline source
vion -e "print 1 + 2"           # Run inline source
vion repl                       # Start REPL
vion -i                         # Start REPL
vion version                    # Show version
vion -v                         # Show version
vion help                       # Show help
vion -h                         # Show help
```

## Documentation

- [Install guide](INSTALL.md)
- [Language specification](LANGUAGE_SPEC.md)
- [Roadmap](ROADMAP.md)
- [VS Code language support](tools/vscode-vion/README.md)

## License

Vion is released under the [MIT License](LICENSE).
