#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "lexer/Lexer.h"
#include "lexer/Token.h"
#include "parser/Parser.h"

static std::string readFile(const std::string& filePath) {
    std::ifstream file(filePath);

    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filePath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static bool endsWith(const std::string& value, const std::string& suffix) {
    if (suffix.length() > value.length()) {
        return false;
    }

    return value.compare(value.length() - suffix.length(), suffix.length(), suffix) == 0;
}

static bool isVionFilePath(const std::string& value) {
    return endsWith(value, ".vion");
}

static bool isOneOf(const std::string& value, const std::vector<std::string>& options) {
    for (const std::string& option : options) {
        if (value == option) {
            return true;
        }
    }

    return false;
}

static std::vector<Token> tokenize(const std::string& source) {
    Lexer lexer(source);
    return lexer.scanTokens();
}

static Program parseProgram(const std::string& source) {
    std::vector<Token> tokens = tokenize(source);
    Parser parser(std::move(tokens));
    return parser.parse();
}

static void printHelp() {
    std::cout << "Vion Language CLI\n";
    std::cout << "Usage:\n";
    std::cout << "  vion <file.vion>                 Run Vion program\n";
    std::cout << "  vion run <file.vion>             Run Vion program\n";
    std::cout << "  vion -r, --run <file.vion>       Run Vion program\n";
    std::cout << "  vion tokens <file.vion>          Print lexer tokens\n";
    std::cout << "  vion -t, --tokens <file.vion>    Print lexer tokens\n";
    std::cout << "  vion ast <file.vion>             Print parsed AST\n";
    std::cout << "  vion -a, --ast <file.vion>       Print parsed AST\n";
    std::cout << "  vion check <file.vion>           Parse-check a Vion program\n";
    std::cout << "  vion -c, --check <file.vion>     Parse-check a Vion program\n";
    std::cout << "  vion eval \"print 1 + 2\"          Run inline Vion source\n";
    std::cout << "  vion -e, --eval \"print 1 + 2\"    Run inline Vion source\n";
    std::cout << "  vion repl                        Start interactive prompt\n";
    std::cout << "  vion -i, --interactive           Start interactive prompt\n";
    std::cout << "  vion version                     Show version\n";
    std::cout << "  vion -v, --version               Show version\n";
    std::cout << "  vion help                        Show help\n";
    std::cout << "  vion -h, --help                  Show help\n";
    std::cout << "  vion build <file.vion>           Build command placeholder\n";
    std::cout << "  vion update                       Update Vion to the latest version\n";
}

static void printTokens(const std::string& source) {
    std::vector<Token> tokens = tokenize(source);

    std::cout << "Tokens:\n";
    std::cout << "------------------------\n";

    for (const Token& token : tokens) {
        std::cout
            << tokenTypeToString(token.type)
            << " | "
            << token.lexeme
            << " | line "
            << token.line
            << ", column "
            << token.column
            << "\n";
    }
}

static void printAst(const std::string& source) {
    Program program = parseProgram(source);

    std::cout << "AST:\n";
    std::cout << "------------------------\n";
    std::cout << program.toString();
}

#include "vm/VM.h"
#include "compiler/Compiler.h"

static void runProgram(const std::string& source, const std::string& dir = "") {
    Program program = parseProgram(source);
    
    Compiler compiler(nullptr, FunctionType::TYPE_SCRIPT);
    auto function = compiler.compile(program);
    if (function) {
        VM vm;
        InterpretResult result = vm.interpret(function, dir);
        if (result != InterpretResult::INTERPRET_OK) {
            exit(1);
        }
    } else {
        std::cerr << "Compile Error\n";
    }
}

static void checkProgram(const std::string& source) {
    parseProgram(source);
    std::cout << "OK\n";
}

static void startRepl() {
    VM vm;
    std::string line;

    std::cout << "Vion REPL v0.5.0\n";
    std::cout << "Type 'exit' or press Ctrl+Z then Enter to quit.\n";

    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line) || line == "exit") {
            break;
        }

        if (line.empty()) {
            continue;
        }

        try {
            Program program = parseProgram(line);
            Compiler compiler(nullptr, FunctionType::TYPE_SCRIPT);
            auto function = compiler.compile(program);
            if (function) {
                vm.interpret(function);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printHelp();
        return 0;
    }

    std::string command = argv[1];

    try {
        if (isOneOf(command, {"help", "--help", "-h"})) {
            printHelp();
            return 0;
        }

        if (isOneOf(command, {"version", "--version", "-v"})) {
            std::cout << "Vion v0.4.0\n";
            return 0;
        }

        if (isOneOf(command, {"repl", "--interactive", "-i"})) {
            startRepl();
            return 0;
        }

        if (isOneOf(command, {"update", "--update", "-u"})) {
#ifdef _WIN32
            std::cout << "Checking for updates...\n";
            int ret = std::system(
                "powershell -ExecutionPolicy Bypass -Command "
                "\"irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/install-online-windows.ps1 | iex\""
            );
            if (ret != 0) {
                std::cerr << "Update failed. Check your internet connection or visit:\n";
                std::cerr << "  https://github.com/AlexanderPhan04/vion-lang/releases\n";
                return 1;
            }
#else
            std::cout << "Auto-update is currently only supported on Windows.\n";
            std::cout << "Visit https://github.com/AlexanderPhan04/vion-lang/releases to download manually.\n";
#endif
            return 0;
        }

        if (isOneOf(command, {"eval", "--eval", "-e"})) {
            if (argc < 3) {
                std::cerr << "Error: missing inline source.\n";
                printHelp();
                return 1;
            }

            runProgram(argv[2]);
            return 0;
        }

        if (isVionFilePath(command)) {
            std::string source = readFile(command);
            std::string absPath = std::filesystem::absolute(command).string();
            runProgram(source, absPath);
            return 0;
        }

        if (argc < 3) {
            std::cerr << "Error: missing file path.\n";
            printHelp();
            return 1;
        }

        std::string filePath = argv[2];
        std::string source = readFile(filePath);

        if (isOneOf(command, {"tokens", "--tokens", "-t"})) {
            printTokens(source);
            return 0;
        }

        if (isOneOf(command, {"ast", "--ast", "-a"})) {
            printAst(source);
            return 0;
        }

        if (isOneOf(command, {"run", "--run", "-r"})) {
            std::string absPath = std::filesystem::absolute(filePath).string();
            runProgram(source, absPath);
            return 0;
        }

        if (isOneOf(command, {"check", "--check", "-c"})) {
            checkProgram(source);
            return 0;
        }

        if (isOneOf(command, {"build", "--build", "-b"})) {
            std::cout << "Vion build is not implemented yet.\n";
            std::cout << "Current version is an interpreter. Use: vion run <file.vion>\n";
            return 0;
        }

        std::cerr << "Error: unknown command: " << command << "\n";
        printHelp();
        return 1;

    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
