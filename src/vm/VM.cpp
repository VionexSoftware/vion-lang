#include "vm/VM.h"
#include <iostream>
#include <iomanip>
#include <cmath>

#include <chrono>
#include <thread>
#include <fstream>
#include <random>
#include <filesystem>
#include <cctype>
#include <array>
#include <memory>
#include <cstdio>
#include <sstream>
#include <functional>
#include <algorithm>
#include <cmath>
#include <regex>

#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "compiler/Compiler.h"

// ── DB helper utilities ───────────────────────────────────────────────────

// Run a shell command and return its combined stdout+stderr
static std::string vion_runSubprocess(const std::string& cmd) {
    std::array<char, 256> buffer;
    std::string result;
#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
    if (!pipe) return "";
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
        result += buffer.data();
    return result;
}

// Escape a SQL string for embedding in a shell-quoted command argument
static std::string vion_escapeSql(const std::string& sql) {
    std::string out;
    out.reserve(sql.size() + 8);
    for (char c : sql) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

// Parse CSV (RFC 4180-ish) — first line is header row
static std::vector<std::unordered_map<std::string,std::string>>
vion_parseCsv(const std::string& text) {
    std::vector<std::unordered_map<std::string,std::string>> result;

    auto parseRow = [](const std::string& row) {
        std::vector<std::string> fields;
        std::string field;
        bool inQ = false;
        for (size_t i = 0; i < row.size(); i++) {
            char c = row[i];
            if (inQ) {
                if (c == '"') {
                    if (i + 1 < row.size() && row[i+1] == '"') { field += '"'; i++; }
                    else inQ = false;
                } else field += c;
            } else {
                if (c == '"') inQ = true;
                else if (c == ',') { fields.push_back(field); field.clear(); }
                else field += c;
            }
        }
        fields.push_back(field);
        return fields;
    };

    std::vector<std::string> lines;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.empty()) return result;

    auto headers = parseRow(lines[0]);
    for (size_t i = 1; i < lines.size(); i++) {
        auto cells = parseRow(lines[i]);
        std::unordered_map<std::string,std::string> row;
        for (size_t j = 0; j < headers.size(); j++)
            row[headers[j]] = (j < cells.size()) ? cells[j] : "";
        result.push_back(row);
    }
    return result;
}

// Parse TSV (mysql --batch --silent output) — first line is header row
static std::vector<std::unordered_map<std::string,std::string>>
vion_parseTsv(const std::string& text) {
    std::vector<std::unordered_map<std::string,std::string>> result;

    auto splitTab = [](const std::string& row) {
        std::vector<std::string> fields;
        std::string field;
        for (char c : row) {
            if (c == '\t') { fields.push_back(field); field.clear(); }
            else field += c;
        }
        fields.push_back(field);
        return fields;
    };

    std::vector<std::string> lines;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.empty()) return result;

    auto headers = splitTab(lines[0]);
    for (size_t i = 1; i < lines.size(); i++) {
        auto cells = splitTab(lines[i]);
        std::unordered_map<std::string,std::string> row;
        for (size_t j = 0; j < headers.size(); j++)
            row[headers[j]] = (j < cells.size()) ? cells[j] : "";
        result.push_back(row);
    }
    return result;
}

// Convert a vector of string-maps (from CSV/TSV) to a Vion array-of-maps Value.
// Numbers are auto-detected; "NULL" / "\N" become nil.
static Value vion_rowsToValue(
        const std::vector<std::unordered_map<std::string,std::string>>& parsed) {
    auto arr = std::make_shared<VionArray>();
    for (const auto& rowData : parsed) {
        auto row = std::make_shared<VionMap>();
        for (const auto& [k, v] : rowData) {
            if (v == "NULL" || v == "\\N") {
                row->entries[k] = Value::nil();
            } else {
                // Try numeric coercion
                try {
                    size_t pos;
                    double d = std::stod(v, &pos);
                    if (pos == v.size())
                        row->entries[k] = Value::number(d);
                    else
                        row->entries[k] = Value::string(v);
                } catch (...) {
                    row->entries[k] = Value::string(v);
                }
            }
        }
        arr->elements.push_back(Value::map(row));
    }
    return Value::array(arr);
}

// Parse mysql://user:pass@host:port/db DSN into VionDB fields
static void vion_parseMysqlDsn(const std::string& dsn, VionDB& conn) {
    // Strip scheme
    size_t schemeEnd = dsn.find("://");
    if (schemeEnd == std::string::npos) return;
    std::string rest = dsn.substr(schemeEnd + 3);

    // user:pass @ host:port/db
    size_t atPos = rest.rfind('@');
    std::string userPass = (atPos != std::string::npos) ? rest.substr(0, atPos) : "";
    std::string hostDb   = (atPos != std::string::npos) ? rest.substr(atPos + 1) : rest;

    // Parse user:pass
    size_t cp = userPass.find(':');
    conn.user     = (cp != std::string::npos) ? userPass.substr(0, cp) : userPass;
    conn.password = (cp != std::string::npos) ? userPass.substr(cp + 1) : "";

    // Parse host:port/db
    size_t slash = hostDb.find('/');
    conn.database = (slash != std::string::npos) ? hostDb.substr(slash + 1) : "";
    std::string hostPort = (slash != std::string::npos) ? hostDb.substr(0, slash) : hostDb;
    size_t colon = hostPort.find(':');
    conn.host = (colon != std::string::npos) ? hostPort.substr(0, colon) : hostPort;
    conn.port = (colon != std::string::npos) ? hostPort.substr(colon + 1) : "3306";
    if (conn.host.empty()) conn.host = "127.0.0.1";
}

VM::VM() {

    defineNative("print", [](int argCount, Value* args) {
        for (int i = 0; i < argCount; ++i) {
            std::cout << args[i].toString();
        }
        std::cout << "\n";
        return Value::nil();
    });

    defineNative("len", [](int argCount, Value* args) {
        if (argCount != 1) return Value::number(0);
        if (args[0].type == ValueType::STRING) {
            return Value::number(std::get<std::string>(args[0].data).length());
        }
        if (args[0].type == ValueType::ARRAY) {
            return Value::number(std::get<std::shared_ptr<VionArray>>(args[0].data)->elements.size());
        }
        if (args[0].type == ValueType::MAP) {
            return Value::number(std::get<std::shared_ptr<VionMap>>(args[0].data)->entries.size());
        }
        return Value::number(0);
    });

    defineNative("time", [](int argCount, Value* args) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return Value::number(std::chrono::duration_cast<std::chrono::milliseconds>(now).count() / 1000.0);
    });

    defineNative("sleep", [](int argCount, Value* args) {
        if (argCount == 1 && args[0].type == ValueType::NUMBER) {
            int ms = static_cast<int>(std::get<double>(args[0].data));
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        return Value::nil();
    });

    defineNative("random", [](int argCount, Value* args) {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value::number(dist(rng));
    });

    defineNative("push", [](int argCount, Value* args) {
        if (argCount == 2 && args[0].type == ValueType::ARRAY) {
            std::get<std::shared_ptr<VionArray>>(args[0].data)->elements.push_back(args[1]);
        }
        return Value::nil();
    });

    defineNative("file_read", [](int argCount, Value* args) {
        if (argCount != 1 || args[0].type != ValueType::STRING) return Value::nil();
        std::ifstream file(std::get<std::string>(args[0].data));
        if (!file.is_open()) return Value::nil();
        std::stringstream buffer;
        buffer << file.rdbuf();
        return Value::string(buffer.str());
    });

    defineNative("file_write", [](int argCount, Value* args) {
        if (argCount != 2 || args[0].type != ValueType::STRING || args[1].type != ValueType::STRING) return Value::boolean(false);
        std::ofstream file(std::get<std::string>(args[0].data));
        if (!file.is_open()) return Value::boolean(false);
        file << std::get<std::string>(args[1].data);
        return Value::boolean(true);
    });

    defineNative("http_get", [](int argCount, Value* args) {
        if (argCount != 1 || args[0].type != ValueType::STRING) return Value::nil();
        std::string url = std::get<std::string>(args[0].data);
        
        std::string command = "curl -s \"" + url + "\"";
        std::array<char, 128> buffer;
        std::string result;
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif
        if (!pipe) {
            return Value::nil();
        }
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return Value::string(result);
    });

    defineNative("http_request", [](int argCount, Value* args) {
        if (argCount != 2 || args[0].type != ValueType::STRING || args[1].type != ValueType::MAP) return Value::nil();
        
        std::string url = std::get<std::string>(args[0].data);
        auto optionsMap = std::get<std::shared_ptr<VionMap>>(args[1].data);
        
        std::string method = "GET";
        if (optionsMap->entries.count("method") && optionsMap->entries["method"].type == ValueType::STRING) {
            method = std::get<std::string>(optionsMap->entries["method"].data);
        }
        
        std::string command = "curl -s -X " + method + " ";
        
        if (optionsMap->entries.count("headers") && optionsMap->entries["headers"].type == ValueType::MAP) {
            auto headersMap = std::get<std::shared_ptr<VionMap>>(optionsMap->entries["headers"].data);
            for (const auto& pair : headersMap->entries) {
                if (pair.second.type == ValueType::STRING) {
                    std::string headerVal = std::get<std::string>(pair.second.data);
                    command += "-H \"" + pair.first + ": " + headerVal + "\" ";
                }
            }
        }
        
        std::string tempFile = ".vion_tmp_body.json";
        bool hasBody = false;
        if (optionsMap->entries.count("body") && optionsMap->entries["body"].type == ValueType::STRING) {
            std::string body = std::get<std::string>(optionsMap->entries["body"].data);
            std::ofstream file(tempFile);
            if (file.is_open()) {
                file << body;
                file.close();
                command += "-d @" + tempFile + " ";
                hasBody = true;
            }
        }
        
        command += "\"" + url + "\"";
        
        std::array<char, 128> buffer;
        std::string result;
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif
        if (pipe) {
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
                result += buffer.data();
            }
        }
        
        if (hasBody) {
            std::filesystem::remove(tempFile);
        }
        
        return Value::string(result);
    });

    defineNative("json_stringify", [](int argCount, Value* args) {
        if (argCount != 1) return Value::nil();
        return Value::string(args[0].toJsonString());
    });

    defineNative("json_parse", [](int argCount, Value* args) {
        if (argCount != 1 || args[0].type != ValueType::STRING) return Value::nil();
        std::string jsonStr = std::get<std::string>(args[0].data);
        std::string source = "let __json = " + jsonStr;
        Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        Compiler compiler(nullptr, FunctionType::TYPE_SCRIPT);
        auto function = compiler.compile(program);
        if (!function) return Value::nil();
        VM jsonVM;
        jsonVM.interpret(function);
        return jsonVM.globals["__json"];
    });

    defineNative("to_upper", [](int argCount, Value* args) {
        if (argCount != 1 || args[0].type != ValueType::STRING) return Value::nil();
        std::string s = std::get<std::string>(args[0].data);
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return Value::string(s);
    });

    defineNative("to_lower", [](int argCount, Value* args) {
        if (argCount != 1 || args[0].type != ValueType::STRING) return Value::nil();
        std::string s = std::get<std::string>(args[0].data);
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return Value::string(s);
    });
    
    defineNative("split", [](int argCount, Value* args) {
        if (argCount != 2 || args[0].type != ValueType::STRING || args[1].type != ValueType::STRING) return Value::nil();
        std::string s = std::get<std::string>(args[0].data);
        std::string delim = std::get<std::string>(args[1].data);
        auto arr = std::make_shared<VionArray>();
        if (delim.empty()) {
            for (char c : s) arr->elements.push_back(Value::string(std::string(1, c)));
            return Value::array(arr);
        }
        size_t start = 0;
        size_t end = s.find(delim);
        while (end != std::string::npos) {
            arr->elements.push_back(Value::string(s.substr(start, end - start)));
            start = end + delim.length();
            end = s.find(delim, start);
        }
        arr->elements.push_back(Value::string(s.substr(start)));
        return Value::array(arr);
    });

    defineNative("pop", [](int argCount, Value* args) {
        if (argCount == 1 && args[0].type == ValueType::ARRAY) {
            auto arr = std::get<std::shared_ptr<VionArray>>(args[0].data);
            if (!arr->elements.empty()) {
                Value last = arr->elements.back();
                arr->elements.pop_back();
                return last;
            }
        }
        return Value::nil();
    });

    defineNative("type", [](int argCount, Value* args) {
        if (argCount == 1) {
            return Value::string(args[0].typeName());
        }
        return Value::nil();
    });

    defineNative("array", [](int argCount, Value* args) {
        if (argCount == 2 && args[0].type == ValueType::NUMBER) {
            int size = static_cast<int>(std::get<double>(args[0].data));
            auto arr = std::make_shared<VionArray>();
            for (int i = 0; i < size; ++i) {
                arr->elements.push_back(args[1]);
            }
            return Value::array(arr);
        }
        return Value::nil();
    });
    defineNative("str", [](int argCount, Value* args) {
        if (argCount == 1) {
            return Value::string(args[0].toString());
        }
        return Value::nil();
    });

    defineNative("upper", [](int argCount, Value* args) {
        if (argCount == 1 && args[0].type == ValueType::STRING) {
            std::string s = std::get<std::string>(args[0].data);
            for (char& c : s) c = std::toupper(c);
            return Value::string(s);
        }
        return Value::nil();
    });

    defineNative("lower", [](int argCount, Value* args) {
        if (argCount == 1 && args[0].type == ValueType::STRING) {
            std::string s = std::get<std::string>(args[0].data);
            for (char& c : s) c = std::tolower(c);
            return Value::string(s);
        }
        return Value::nil();
    });

    // ── Database — multi-driver (SQLite/PostgreSQL/MySQL) ─────────────────

    // db_open(path) — backward-compat SQLite shortcut
    defineNative("db_open", [](int argCount, Value* args) -> Value {
        if (argCount != 1 || args[0].type != ValueType::STRING) {
            std::cerr << "db_open: expected string path\n";
            return Value::nil();
        }
        std::string path = std::get<std::string>(args[0].data);
        auto conn = std::make_shared<VionDB>();
        conn->driver = DBDriver::SQLITE;
        int rc = sqlite3_open(path.c_str(), &conn->db);
        if (rc != SQLITE_OK) {
            std::cerr << "db_open: " << sqlite3_errmsg(conn->db) << "\n";
            sqlite3_close(conn->db);
            conn->db = nullptr; conn->closed = true;
            return Value::nil();
        }
        sqlite3_exec(conn->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        return Value::db(conn);
    });

    // db_connect(dsn) — unified entry point for all drivers
    //   sqlite:./path.db          SQLite file
    //   sqlite::memory:           SQLite in-memory
    //   postgresql://user:pass@host:port/db
    //   mysql://user:pass@host:port/db
    defineNative("db_connect", [](int argCount, Value* args) -> Value {
        if (argCount != 1 || args[0].type != ValueType::STRING) {
            std::cerr << "db_connect: expected DSN string\n";
            return Value::nil();
        }
        std::string dsn = std::get<std::string>(args[0].data);
        auto conn = std::make_shared<VionDB>();

        if (dsn.size() >= 7 && dsn.substr(0, 7) == "sqlite:") {
            // SQLite: sqlite:./path  or  sqlite::memory:
            conn->driver = DBDriver::SQLITE;
            std::string path = dsn.substr(7);
            int rc = sqlite3_open(path.c_str(), &conn->db);
            if (rc != SQLITE_OK) {
                std::cerr << "db_connect (sqlite): " << sqlite3_errmsg(conn->db) << "\n";
                sqlite3_close(conn->db);
                conn->db = nullptr; conn->closed = true;
                return Value::nil();
            }
            sqlite3_exec(conn->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        } else if ((dsn.size() >= 13 && dsn.substr(0, 13) == "postgresql://") ||
                   (dsn.size() >= 11 && dsn.substr(0, 11) == "postgres://")) {
            // PostgreSQL: store full DSN for psql CLI
            conn->driver = DBDriver::POSTGRES;
            conn->connstr = dsn;
        } else if ((dsn.size() >= 8 && dsn.substr(0, 8) == "mysql://") ||
                   (dsn.size() >= 10 && dsn.substr(0, 10) == "mariadb://")) {
            // MySQL / MariaDB
            conn->driver = DBDriver::MYSQL;
            vion_parseMysqlDsn(dsn, *conn);
        } else {
            std::cerr << "db_connect: unknown scheme. Use sqlite:, postgresql://, or mysql://\n";
            return Value::nil();
        }
        return Value::db(conn);
    });

    // db_close(db)
    defineNative("db_close", [](int argCount, Value* args) -> Value {
        if (argCount != 1 || args[0].type != ValueType::DB_CONNECTION) return Value::nil();
        auto conn = std::get<std::shared_ptr<VionDB>>(args[0].data);
        if (conn->closed) return Value::nil();
        if (conn->driver == DBDriver::SQLITE && conn->db) {
            sqlite3_close(conn->db);
            conn->db = nullptr;
        }
        conn->closed = true;
        return Value::nil();
    });

    // db_exec(db, sql) → boolean — for CREATE/INSERT/UPDATE/DELETE
    defineNative("db_exec", [](int argCount, Value* args) -> Value {
        if (argCount < 2 || args[0].type != ValueType::DB_CONNECTION || args[1].type != ValueType::STRING)
            return Value::boolean(false);
        auto conn = std::get<std::shared_ptr<VionDB>>(args[0].data);
        if (conn->closed) { std::cerr << "db_exec: connection is closed\n"; return Value::boolean(false); }
        std::string sql = std::get<std::string>(args[1].data);

        if (conn->driver == DBDriver::SQLITE) {
            if (!conn->db) return Value::boolean(false);
            char* errMsg = nullptr;
            int rc = sqlite3_exec(conn->db, sql.c_str(), nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                std::cerr << "db_exec error: " << errMsg << "\n";
                sqlite3_free(errMsg);
                return Value::boolean(false);
            }
            return Value::boolean(true);

        } else if (conn->driver == DBDriver::POSTGRES) {
            std::string cmd = "psql \"" + conn->connstr + "\" -c \"" + vion_escapeSql(sql) + "\" 2>&1";
            std::string out = vion_runSubprocess(cmd);
            bool ok = out.find("ERROR:") == std::string::npos &&
                      out.find("error:") == std::string::npos &&
                      out.find("FATAL:") == std::string::npos;
            if (!ok) std::cerr << "db_exec (pg): " << out;
            return Value::boolean(ok);

        } else { // MYSQL
            std::string cmd = "mysql --host=" + conn->host +
                              " --port=" + conn->port +
                              " --user=" + conn->user +
                              " --password=" + conn->password +
                              " --database=" + conn->database +
                              " -e \"" + vion_escapeSql(sql) + "\" 2>&1";
            std::string out = vion_runSubprocess(cmd);
            bool ok = out.find("ERROR") == std::string::npos &&
                      out.find("error") == std::string::npos;
            if (!ok) std::cerr << "db_exec (mysql): " << out;
            return Value::boolean(ok);
        }
    });

    // db_query(db, sql) → array of maps
    defineNative("db_query", [](int argCount, Value* args) -> Value {
        if (argCount < 2 || args[0].type != ValueType::DB_CONNECTION || args[1].type != ValueType::STRING)
            return Value::array();
        auto conn = std::get<std::shared_ptr<VionDB>>(args[0].data);
        if (conn->closed) { std::cerr << "db_query: connection is closed\n"; return Value::array(); }
        std::string sql = std::get<std::string>(args[1].data);

        if (conn->driver == DBDriver::SQLITE) {
            if (!conn->db) return Value::array();
            auto rows = std::make_shared<VionArray>();
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(conn->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                std::cerr << "db_query: " << sqlite3_errmsg(conn->db) << "\n";
                return Value::array();
            }
            int colCount = sqlite3_column_count(stmt);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto row = std::make_shared<VionMap>();
                for (int i = 0; i < colCount; i++) {
                    std::string colName = sqlite3_column_name(stmt, i);
                    int colType = sqlite3_column_type(stmt, i);
                    Value cell;
                    switch (colType) {
                        case SQLITE_INTEGER: cell = Value::number(static_cast<double>(sqlite3_column_int64(stmt, i))); break;
                        case SQLITE_FLOAT:   cell = Value::number(sqlite3_column_double(stmt, i)); break;
                        case SQLITE_TEXT: {
                            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                            cell = Value::string(t ? t : "");
                            break;
                        }
                        case SQLITE_BLOB: {
                            int bytes = sqlite3_column_bytes(stmt, i);
                            cell = Value::string("<blob:" + std::to_string(bytes) + "bytes>");
                            break;
                        }
                        case SQLITE_NULL: default: cell = Value::nil(); break;
                    }
                    row->entries[colName] = cell;
                }
                rows->elements.push_back(Value::map(row));
            }
            sqlite3_finalize(stmt);
            return Value::array(rows);

        } else if (conn->driver == DBDriver::POSTGRES) {
            std::string cmd = "psql \"" + conn->connstr + "\" --csv -c \"" + vion_escapeSql(sql) + "\" 2>&1";
            std::string out = vion_runSubprocess(cmd);
            if (out.find("ERROR:") != std::string::npos || out.find("FATAL:") != std::string::npos) {
                std::cerr << "db_query (pg): " << out;
                return Value::array();
            }
            return vion_rowsToValue(vion_parseCsv(out));

        } else { // MYSQL
            std::string cmd = "mysql --host=" + conn->host +
                              " --port=" + conn->port +
                              " --user=" + conn->user +
                              " --password=" + conn->password +
                              " --database=" + conn->database +
                              " --batch --silent -e \"" + vion_escapeSql(sql) + "\" 2>&1";
            std::string out = vion_runSubprocess(cmd);
            if (out.find("ERROR") != std::string::npos) {
                std::cerr << "db_query (mysql): " << out;
                return Value::array();
            }
            return vion_rowsToValue(vion_parseTsv(out));
        }
    });

    // db_query_one(db, sql) → map | nil
    defineNative("db_query_one", [](int argCount, Value* args) -> Value {
        if (argCount < 2 || args[0].type != ValueType::DB_CONNECTION || args[1].type != ValueType::STRING)
            return Value::nil();
        auto conn = std::get<std::shared_ptr<VionDB>>(args[0].data);
        if (conn->closed) { std::cerr << "db_query_one: connection is closed\n"; return Value::nil(); }
        std::string sql = std::get<std::string>(args[1].data);

        if (conn->driver == DBDriver::SQLITE) {
            if (!conn->db) return Value::nil();
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(conn->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                std::cerr << "db_query_one: " << sqlite3_errmsg(conn->db) << "\n";
                return Value::nil();
            }
            int colCount = sqlite3_column_count(stmt);
            Value result = Value::nil();
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                auto row = std::make_shared<VionMap>();
                for (int i = 0; i < colCount; i++) {
                    std::string colName = sqlite3_column_name(stmt, i);
                    int colType = sqlite3_column_type(stmt, i);
                    Value cell;
                    switch (colType) {
                        case SQLITE_INTEGER: cell = Value::number(static_cast<double>(sqlite3_column_int64(stmt, i))); break;
                        case SQLITE_FLOAT:   cell = Value::number(sqlite3_column_double(stmt, i)); break;
                        case SQLITE_TEXT: {
                            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                            cell = Value::string(t ? t : "");
                            break;
                        }
                        case SQLITE_NULL: default: cell = Value::nil(); break;
                    }
                    row->entries[colName] = cell;
                }
                result = Value::map(row);
            }
            sqlite3_finalize(stmt);
            return result;

        } else {
            // For PG/MySQL: reuse db_query and take first row
            // Build a temporary value by reusing the query logic inline
            // We call db_query logic and take first element
            Value arr;
            if (conn->driver == DBDriver::POSTGRES) {
                std::string cmd = "psql \"" + conn->connstr + "\" --csv -c \"" + vion_escapeSql(sql) + "\" 2>&1";
                std::string out = vion_runSubprocess(cmd);
                if (out.find("ERROR:") != std::string::npos || out.find("FATAL:") != std::string::npos) {
                    std::cerr << "db_query_one (pg): " << out;
                    return Value::nil();
                }
                arr = vion_rowsToValue(vion_parseCsv(out));
            } else {
                std::string cmd = "mysql --host=" + conn->host +
                                  " --port=" + conn->port +
                                  " --user=" + conn->user +
                                  " --password=" + conn->password +
                                  " --database=" + conn->database +
                                  " --batch --silent -e \"" + vion_escapeSql(sql) + "\" 2>&1";
                std::string out = vion_runSubprocess(cmd);
                if (out.find("ERROR") != std::string::npos) {
                    std::cerr << "db_query_one (mysql): " << out;
                    return Value::nil();
                }
                arr = vion_rowsToValue(vion_parseTsv(out));
            }
            auto& elems = std::get<std::shared_ptr<VionArray>>(arr.data)->elements;
            return elems.empty() ? Value::nil() : elems[0];
        }
    });

    // db_last_id(db) → number
    defineNative("db_last_id", [](int argCount, Value* args) -> Value {
        if (argCount != 1 || args[0].type != ValueType::DB_CONNECTION) return Value::nil();
        auto conn = std::get<std::shared_ptr<VionDB>>(args[0].data);
        if (conn->closed) return Value::nil();
        if (conn->driver == DBDriver::SQLITE) {
            if (!conn->db) return Value::nil();
            return Value::number(static_cast<double>(sqlite3_last_insert_rowid(conn->db)));
        } else if (conn->driver == DBDriver::POSTGRES) {
            std::string cmd = "psql \"" + conn->connstr + "\" -t -A -c \"SELECT lastval()\" 2>&1";
            std::string out = vion_runSubprocess(cmd);
            out.erase(out.find_last_not_of(" \t\r\n") + 1);
            try { return Value::number(std::stod(out)); } catch (...) { return Value::nil(); }
        } else {
            std::string cmd = "mysql --host=" + conn->host +
                              " --port=" + conn->port +
                              " --user=" + conn->user +
                              " --password=" + conn->password +
                              " --database=" + conn->database +
                              " --batch --silent -e \"SELECT LAST_INSERT_ID()\" 2>&1";
            std::string out = vion_runSubprocess(cmd);
            // Skip header line (column name), get second line
            std::istringstream ss(out);
            std::string line1, line2;
            std::getline(ss, line1); std::getline(ss, line2);
            if (!line2.empty() && line2.back() == '\r') line2.pop_back();
            try { return Value::number(std::stod(line2)); } catch (...) { return Value::nil(); }
        }
    });

    // db_changes(db) → number
    defineNative("db_changes", [](int argCount, Value* args) -> Value {
        if (argCount != 1 || args[0].type != ValueType::DB_CONNECTION) return Value::nil();
        auto conn = std::get<std::shared_ptr<VionDB>>(args[0].data);
        if (conn->closed) return Value::nil();
        if (conn->driver == DBDriver::SQLITE) {
            if (!conn->db) return Value::nil();
            return Value::number(static_cast<double>(sqlite3_changes(conn->db)));
        }
        // PG/MySQL: not directly available via CLI; return -1 as "unknown"
        return Value::number(-1);
    });

    // ── Math ──────────────────────────────────────────────────────────────────
    defineNative("sin",   [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::sin(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("cos",   [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::cos(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("tan",   [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::tan(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("asin",  [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::asin(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("acos",  [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::acos(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("atan",  [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::atan(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("atan2", [](int n, Value* a) { return n==2&&a[0].type==ValueType::NUMBER&&a[1].type==ValueType::NUMBER?Value::number(std::atan2(std::get<double>(a[0].data),std::get<double>(a[1].data))):Value::nil(); });
    defineNative("log",   [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::log(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("log2",  [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::log2(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("log10", [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::log10(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("exp",   [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::exp(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("pow",   [](int n, Value* a) { return n==2&&a[0].type==ValueType::NUMBER&&a[1].type==ValueType::NUMBER?Value::number(std::pow(std::get<double>(a[0].data),std::get<double>(a[1].data))):Value::nil(); });
    defineNative("round", [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::round(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("trunc", [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::trunc(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("floor", [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::floor(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("ceil",  [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::ceil(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("sqrt",  [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::sqrt(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("abs",   [](int n, Value* a) { return n==1&&a[0].type==ValueType::NUMBER?Value::number(std::abs(std::get<double>(a[0].data))):Value::nil(); });
    defineNative("max",   [](int n, Value* a) { return n==2&&a[0].type==ValueType::NUMBER&&a[1].type==ValueType::NUMBER?Value::number(std::max(std::get<double>(a[0].data),std::get<double>(a[1].data))):Value::nil(); });
    defineNative("min",   [](int n, Value* a) { return n==2&&a[0].type==ValueType::NUMBER&&a[1].type==ValueType::NUMBER?Value::number(std::min(std::get<double>(a[0].data),std::get<double>(a[1].data))):Value::nil(); });
    defineNative("pi",    [](int, Value*) { return Value::number(3.14159265358979323846); });
    defineNative("e_val", [](int, Value*) { return Value::number(2.71828182845904523536); });


    // ── String ────────────────────────────────────────────────────────────────
    defineNative("trim", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::STRING) return Value::nil();
        std::string s = std::get<std::string>(a[0].data);
        size_t l = s.find_first_not_of(" \t\n\r");
        size_t r = s.find_last_not_of(" \t\n\r");
        return Value::string(l==std::string::npos?"":s.substr(l,r-l+1));
    });
    defineNative("ltrim", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::STRING) return Value::nil();
        std::string s = std::get<std::string>(a[0].data);
        size_t l = s.find_first_not_of(" \t\n\r");
        return Value::string(l==std::string::npos?"":s.substr(l));
    });
    defineNative("rtrim", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::STRING) return Value::nil();
        std::string s = std::get<std::string>(a[0].data);
        size_t r = s.find_last_not_of(" \t\n\r");
        return Value::string(r==std::string::npos?"":s.substr(0,r+1));
    });
    defineNative("replace", [](int n, Value* a) -> Value {
        if (n!=3||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING||a[2].type!=ValueType::STRING) return Value::nil();
        std::string s=std::get<std::string>(a[0].data), from=std::get<std::string>(a[1].data), to=std::get<std::string>(a[2].data);
        if (from.empty()) return Value::string(s);
        size_t pos=0;
        while((pos=s.find(from,pos))!=std::string::npos){s.replace(pos,from.size(),to);pos+=to.size();}
        return Value::string(s);
    });
    defineNative("starts_with", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING) return Value::boolean(false);
        const std::string& s=std::get<std::string>(a[0].data),&p=std::get<std::string>(a[1].data);
        return Value::boolean(s.size()>=p.size()&&s.substr(0,p.size())==p);
    });
    defineNative("ends_with", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING) return Value::boolean(false);
        const std::string& s=std::get<std::string>(a[0].data),&p=std::get<std::string>(a[1].data);
        return Value::boolean(s.size()>=p.size()&&s.substr(s.size()-p.size())==p);
    });
    defineNative("index_of", [](int n, Value* a) -> Value {
        if (n<2) return Value::number(-1);
        if (a[0].type==ValueType::STRING&&a[1].type==ValueType::STRING) {
            size_t pos=std::get<std::string>(a[0].data).find(std::get<std::string>(a[1].data));
            return Value::number(pos==std::string::npos?-1:(double)pos);
        }
        if (a[0].type==ValueType::ARRAY) {
            auto& elems=std::get<std::shared_ptr<VionArray>>(a[0].data)->elements;
            for (size_t i=0;i<elems.size();i++) if(elems[i]==a[1]) return Value::number((double)i);
            return Value::number(-1);
        }
        return Value::number(-1);
    });
    defineNative("substring", [](int n, Value* a) -> Value {
        if (n<2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::NUMBER) return Value::nil();
        const std::string& s=std::get<std::string>(a[0].data);
        int start=(int)std::get<double>(a[1].data);
        if(start<0)start=0;
        if(start>=(int)s.size()) return Value::string("");
        if(n>=3&&a[2].type==ValueType::NUMBER){
            int len=(int)std::get<double>(a[2].data);
            return Value::string(s.substr(start,len));
        }
        return Value::string(s.substr(start));
    });
    defineNative("char_at", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::NUMBER) return Value::nil();
        const std::string& s=std::get<std::string>(a[0].data);
        int i=(int)std::get<double>(a[1].data);
        if(i<0||i>=(int)s.size()) return Value::nil();
        return Value::string(std::string(1,s[i]));
    });
    defineNative("char_code", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::STRING) return Value::nil();
        const std::string& s=std::get<std::string>(a[0].data);
        return s.empty()?Value::nil():Value::number((double)(unsigned char)s[0]);
    });
    defineNative("from_char_code", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::NUMBER) return Value::nil();
        return Value::string(std::string(1,(char)(int)std::get<double>(a[0].data)));
    });
    defineNative("str_repeat", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::NUMBER) return Value::nil();
        std::string s=std::get<std::string>(a[0].data);
        int times=(int)std::get<double>(a[1].data);
        std::string result; result.reserve(s.size()*std::max(0,times));
        for(int i=0;i<times;i++) result+=s;
        return Value::string(result);
    });
    defineNative("split", [](int n, Value* a) -> Value {
        if (n<1||a[0].type!=ValueType::STRING) return Value::array();
        std::string s=std::get<std::string>(a[0].data);
        std::string delim=(n>=2&&a[1].type==ValueType::STRING)?std::get<std::string>(a[1].data):"";
        auto arr=std::make_shared<VionArray>();
        if(delim.empty()){for(char c:s)arr->elements.push_back(Value::string(std::string(1,c)));return Value::array(arr);}
        size_t pos=0,found;
        while((found=s.find(delim,pos))!=std::string::npos){arr->elements.push_back(Value::string(s.substr(pos,found-pos)));pos=found+delim.size();}
        arr->elements.push_back(Value::string(s.substr(pos)));
        return Value::array(arr);
    });
    defineNative("count", [](int n, Value* a) -> Value {
        if (n!=2) return Value::number(0);
        if (a[0].type==ValueType::STRING&&a[1].type==ValueType::STRING) {
            const std::string& s=std::get<std::string>(a[0].data),&sub=std::get<std::string>(a[1].data);
            if(sub.empty()) return Value::number(0);
            int cnt=0; size_t pos=0;
            while((pos=s.find(sub,pos))!=std::string::npos){cnt++;pos+=sub.size();}
            return Value::number(cnt);
        }
        return Value::number(0);
    });

    // ── Array ─────────────────────────────────────────────────────────────────
    defineNative("pop", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::ARRAY) return Value::nil();
        auto arr=std::get<std::shared_ptr<VionArray>>(a[0].data);
        if(arr->elements.empty()) return Value::nil();
        Value v=arr->elements.back(); arr->elements.pop_back(); return v;
    });
    defineNative("sort", [](int n, Value* a) -> Value {
        if (n<1||a[0].type!=ValueType::ARRAY) return Value::nil();
        auto arr=std::get<std::shared_ptr<VionArray>>(a[0].data);
        if (n>=2&&a[1].type==ValueType::BYTECODE_FUNCTION) {
            // sort with comparator — skip for now, do default
        }
        std::stable_sort(arr->elements.begin(),arr->elements.end(),[](const Value& x,const Value& y){
            if(x.type==ValueType::NUMBER&&y.type==ValueType::NUMBER)
                return std::get<double>(x.data)<std::get<double>(y.data);
            return x.toString()<y.toString();
        });
        return a[0];
    });
    defineNative("reverse", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::ARRAY) return Value::nil();
        auto arr=std::get<std::shared_ptr<VionArray>>(a[0].data);
        std::reverse(arr->elements.begin(),arr->elements.end());
        return a[0];
    });
    defineNative("join", [](int n, Value* a) -> Value {
        if (n<1||a[0].type!=ValueType::ARRAY) return Value::nil();
        std::string sep=(n>=2&&a[1].type==ValueType::STRING)?std::get<std::string>(a[1].data):",";
        auto& elems=std::get<std::shared_ptr<VionArray>>(a[0].data)->elements;
        std::string result;
        for(size_t i=0;i<elems.size();i++){if(i>0)result+=sep;result+=elems[i].toString();}
        return Value::string(result);
    });
    defineNative("slice", [](int n, Value* a) -> Value {
        if (n<2||a[0].type!=ValueType::ARRAY||a[1].type!=ValueType::NUMBER) return Value::array();
        auto& elems=std::get<std::shared_ptr<VionArray>>(a[0].data)->elements;
        int start=(int)std::get<double>(a[1].data);
        int end=(n>=3&&a[2].type==ValueType::NUMBER)?(int)std::get<double>(a[2].data):(int)elems.size();
        if(start<0)start=(int)elems.size()+start;
        if(end<0)end=(int)elems.size()+end;
        start=std::max(0,std::min(start,(int)elems.size()));
        end=std::max(start,std::min(end,(int)elems.size()));
        auto result=std::make_shared<VionArray>();
        result->elements=std::vector<Value>(elems.begin()+start,elems.begin()+end);
        return Value::array(result);
    });
    defineNative("contains", [](int n, Value* a) -> Value {
        if (n!=2) return Value::boolean(false);
        if(a[0].type==ValueType::ARRAY){
            auto& elems=std::get<std::shared_ptr<VionArray>>(a[0].data)->elements;
            for(auto& e:elems) if(e==a[1]) return Value::boolean(true);
            return Value::boolean(false);
        }
        if(a[0].type==ValueType::STRING&&a[1].type==ValueType::STRING)
            return Value::boolean(std::get<std::string>(a[0].data).find(std::get<std::string>(a[1].data))!=std::string::npos);
        return Value::boolean(false);
    });
    defineNative("unique", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::ARRAY) return Value::array();
        auto& elems=std::get<std::shared_ptr<VionArray>>(a[0].data)->elements;
        auto result=std::make_shared<VionArray>();
        for(auto& e:elems){
            bool found=false;
            for(auto& r:result->elements) if(r==e){found=true;break;}
            if(!found) result->elements.push_back(e);
        }
        return Value::array(result);
    });
    defineNative("flatten", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::ARRAY) return Value::array();
        auto result=std::make_shared<VionArray>();
        std::function<void(const Value&)> flat=[&](const Value& v){
            if(v.type==ValueType::ARRAY){
                for(auto& e:std::get<std::shared_ptr<VionArray>>(v.data)->elements) flat(e);
            } else result->elements.push_back(v);
        };
        for(auto& e:std::get<std::shared_ptr<VionArray>>(a[0].data)->elements) flat(e);
        return Value::array(result);
    });
    defineNative("sum", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::ARRAY) return Value::number(0);
        double s=0;
        for(auto& e:std::get<std::shared_ptr<VionArray>>(a[0].data)->elements)
            if(e.type==ValueType::NUMBER) s+=std::get<double>(e.data);
        return Value::number(s);
    });
    defineNative("avg", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::ARRAY) return Value::nil();
        auto& elems=std::get<std::shared_ptr<VionArray>>(a[0].data)->elements;
        if(elems.empty()) return Value::nil();
        double s=0; int cnt=0;
        for(auto& e:elems) if(e.type==ValueType::NUMBER){s+=std::get<double>(e.data);cnt++;}
        return cnt>0?Value::number(s/cnt):Value::nil();
    });
    defineNative("insert", [](int n, Value* a) -> Value {
        if (n!=3||a[0].type!=ValueType::ARRAY||a[1].type!=ValueType::NUMBER) return Value::nil();
        auto arr=std::get<std::shared_ptr<VionArray>>(a[0].data);
        int idx=(int)std::get<double>(a[1].data);
        if(idx<0)idx=0; if(idx>(int)arr->elements.size())idx=(int)arr->elements.size();
        arr->elements.insert(arr->elements.begin()+idx,a[2]);
        return a[0];
    });
    defineNative("remove", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::ARRAY||a[1].type!=ValueType::NUMBER) return Value::nil();
        auto arr=std::get<std::shared_ptr<VionArray>>(a[0].data);
        int idx=(int)std::get<double>(a[1].data);
        if(idx<0||idx>=(int)arr->elements.size()) return Value::nil();
        Value removed=arr->elements[idx];
        arr->elements.erase(arr->elements.begin()+idx);
        return removed;
    });

    // ── Map ───────────────────────────────────────────────────────────────────
    defineNative("keys", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::MAP) return Value::array();
        auto& entries=std::get<std::shared_ptr<VionMap>>(a[0].data)->entries;
        auto arr=std::make_shared<VionArray>();
        for(auto& [k,v]:entries) arr->elements.push_back(Value::string(k));
        return Value::array(arr);
    });
    defineNative("values", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::MAP) return Value::array();
        auto& entries=std::get<std::shared_ptr<VionMap>>(a[0].data)->entries;
        auto arr=std::make_shared<VionArray>();
        for(auto& [k,v]:entries) arr->elements.push_back(v);
        return Value::array(arr);
    });
    defineNative("has_key", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::MAP||a[1].type!=ValueType::STRING) return Value::boolean(false);
        auto& entries=std::get<std::shared_ptr<VionMap>>(a[0].data)->entries;
        return Value::boolean(entries.find(std::get<std::string>(a[1].data))!=entries.end());
    });
    defineNative("delete_key", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::MAP||a[1].type!=ValueType::STRING) return Value::boolean(false);
        auto& entries=std::get<std::shared_ptr<VionMap>>(a[0].data)->entries;
        return Value::boolean(entries.erase(std::get<std::string>(a[1].data))>0);
    });
    defineNative("entries", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::MAP) return Value::array();
        auto& ents=std::get<std::shared_ptr<VionMap>>(a[0].data)->entries;
        auto arr=std::make_shared<VionArray>();
        for(auto& [k,v]:ents){
            auto pair=std::make_shared<VionArray>();
            pair->elements.push_back(Value::string(k));
            pair->elements.push_back(v);
            arr->elements.push_back(Value::array(pair));
        }
        return Value::array(arr);
    });
    defineNative("map_merge", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::MAP||a[1].type!=ValueType::MAP) return Value::nil();
        auto result=std::make_shared<VionMap>();
        result->entries=std::get<std::shared_ptr<VionMap>>(a[0].data)->entries;
        for(auto& [k,v]:std::get<std::shared_ptr<VionMap>>(a[1].data)->entries) result->entries[k]=v;
        return Value::map(result);
    });

    // ── OS / System ───────────────────────────────────────────────────────────
    defineNative("env", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::STRING) return Value::nil();
        const char* val=std::getenv(std::get<std::string>(a[0].data).c_str());
        return val?Value::string(val):Value::nil();
    });
    defineNative("exit", [](int n, Value* a) -> Value {
        int code=(n>=1&&a[0].type==ValueType::NUMBER)?(int)std::get<double>(a[0].data):0;
        std::exit(code); return Value::nil();
    });
    defineNative("cwd", [](int, Value*) -> Value {
        try { return Value::string(std::filesystem::current_path().string()); }
        catch(...) { return Value::nil(); }
    });
    defineNative("file_append", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING) return Value::boolean(false);
        std::ofstream f(std::get<std::string>(a[0].data),std::ios::app);
        if(!f.is_open()) return Value::boolean(false);
        f<<std::get<std::string>(a[1].data);
        return Value::boolean(true);
    });
    defineNative("file_exists", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::STRING) return Value::boolean(false);
        return Value::boolean(std::filesystem::exists(std::get<std::string>(a[0].data)));
    });
    defineNative("file_delete", [](int n, Value* a) -> Value {
        if (n!=1||a[0].type!=ValueType::STRING) return Value::boolean(false);
        try { return Value::boolean(std::filesystem::remove(std::get<std::string>(a[0].data))); }
        catch(...) { return Value::boolean(false); }
    });

    // ── range(n) / range(a,b) / range(a,b,step) ────────────────────────────
    defineNative("range", [](int n, Value* a) -> Value {
        if (n<1||a[0].type!=ValueType::NUMBER) return Value::array();
        double start=0,end=std::get<double>(a[0].data),step=1;
        if(n>=2&&a[1].type==ValueType::NUMBER){start=end;end=std::get<double>(a[1].data);}
        if(n>=3&&a[2].type==ValueType::NUMBER) step=std::get<double>(a[2].data);
        if(step==0||step>0&&start>=end||step<0&&start<=end) return Value::array();
        auto arr=std::make_shared<VionArray>();
        for(double v=start;step>0?v<end:v>end;v+=step) arr->elements.push_back(Value::number(v));
        return Value::array(arr);
    });

    // ── Regex ────────────────────────────────────────────────────────────────
    auto makeRegex = [](const std::string& pattern) {
        return std::regex(pattern, std::regex_constants::ECMAScript);
    };
    defineNative("regex_match", [makeRegex](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING) return Value::boolean(false);
        try {
            auto re = makeRegex(std::get<std::string>(a[1].data));
            return Value::boolean(std::regex_match(std::get<std::string>(a[0].data), re));
        } catch(...) { return Value::boolean(false); }
    });
    defineNative("regex_search", [makeRegex](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING) return Value::boolean(false);
        try {
            auto re = makeRegex(std::get<std::string>(a[1].data));
            return Value::boolean(std::regex_search(std::get<std::string>(a[0].data), re));
        } catch(...) { return Value::boolean(false); }
    });
    defineNative("regex_find", [makeRegex](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING) return Value::array();
        try {
            std::string s=std::get<std::string>(a[0].data);
            auto re = makeRegex(std::get<std::string>(a[1].data));
            auto arr=std::make_shared<VionArray>();
            std::sregex_iterator it(s.begin(),s.end(),re), end;
            for(;it!=end;++it) arr->elements.push_back(Value::string((*it)[0].str()));
            return Value::array(arr);
        } catch(...) { return Value::array(); }
    });
    defineNative("regex_replace", [makeRegex](int n, Value* a) -> Value {
        if (n!=3||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING||a[2].type!=ValueType::STRING) return Value::nil();
        try {
            auto re = makeRegex(std::get<std::string>(a[1].data));
            return Value::string(std::regex_replace(std::get<std::string>(a[0].data),re,std::get<std::string>(a[2].data)));
        } catch(...) { return a[0]; }
    });
    defineNative("regex_split", [makeRegex](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::STRING||a[1].type!=ValueType::STRING) return Value::array();
        try {
            std::string s=std::get<std::string>(a[0].data);
            auto re = makeRegex(std::get<std::string>(a[1].data));
            auto arr=std::make_shared<VionArray>();
            std::sregex_token_iterator it(s.begin(),s.end(),re,-1), end;
            for(;it!=end;++it) arr->elements.push_back(Value::string(it->str()));
            return Value::array(arr);
        } catch(...) { return Value::array(); }
    });

    // ── instanceof / class utilities ──────────────────────────────────────────
    defineNative("isinstance", [](int n, Value* a) -> Value {
        if (n!=2||a[0].type!=ValueType::INSTANCE||a[1].type!=ValueType::CLASS) return Value::boolean(false);
        auto inst = std::get<std::shared_ptr<VionInstance>>(a[0].data);
        auto targetClass = std::get<std::shared_ptr<VionClass>>(a[1].data);
        auto* klass = inst->klass.get();
        while (klass) {
            if (klass == targetClass.get()) return Value::boolean(true);
            klass = klass->superclass.get();
        }
        return Value::boolean(false);
    });
    defineNative("classname", [](int n, Value* a) -> Value {
        if (n!=1) return Value::nil();
        if (a[0].type==ValueType::INSTANCE)
            return Value::string(std::get<std::shared_ptr<VionInstance>>(a[0].data)->klass->name);
        if (a[0].type==ValueType::CLASS)
            return Value::string(std::get<std::shared_ptr<VionClass>>(a[0].data)->name);
        return Value::nil();
    });
}


VM::~VM() {}

void VM::push(Value value) {
    stack.push_back(std::move(value));
}

Value VM::pop() {
    Value value = std::move(stack.back());
    stack.pop_back();
    return value;
}

bool VM::handleError(const std::string& message) {
    if (tryHandlers.empty()) {
        std::cerr << "Runtime Error: " << message << "\n";
        return false;
    }
    
    TryHandler handler = tryHandlers.back();
    tryHandlers.pop_back();
    
    frames.resize(handler.frameIndex + 1);
    stack.resize(handler.stackSize);
    frames.back().ip = handler.catchIp;
    
    push(Value::string(message));
    return true;
}

uint8_t VM::readByte() {
    return *frames.back().ip++;
}

uint16_t VM::readShort() {
    frames.back().ip += 2;
    return static_cast<uint16_t>((frames.back().ip[-2] << 8) | frames.back().ip[-1]);
}

Value VM::readConstant() {
    return frames.back().function->chunk->constants[readByte()];
}

InterpretResult VM::interpret(std::shared_ptr<BytecodeFunction> function, const std::string& scriptPath) {
    currentScriptPath = scriptPath;
    frames.clear();
    stack.clear();
    
    push(Value::bytecodeFunction(function));
    CallFrame frame;
    frame.function = function;
    frame.ip = function->chunk->code.data();
    frame.slots_base = 0;
    frames.push_back(frame);
    return run();
}

void VM::defineNative(const std::string& name, NativeFn function) {
    auto native = std::make_shared<VMNativeFunction>();
    native->name = name;
    native->function = function;
    globals[name] = Value::nativeFunction(native);
}

InterpretResult VM::run() {
    for (;;) {
        uint8_t instruction;
        switch (instruction = readByte()) {
            case static_cast<uint8_t>(OpCode::OP_CONSTANT): {
                Value constant = readConstant();
                push(constant);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_NIL): push(Value::nil()); break;
            case static_cast<uint8_t>(OpCode::OP_TRUE): push(Value::boolean(true)); break;
            case static_cast<uint8_t>(OpCode::OP_FALSE): push(Value::boolean(false)); break;
            case static_cast<uint8_t>(OpCode::OP_EQUAL): {
                Value b = pop();
                Value a = pop();
                push(Value::boolean(a == b));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_GREATER): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::boolean(std::get<double>(a.data) > std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_LESS): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::boolean(std::get<double>(a.data) < std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_ADD): {
                Value b = pop();
                Value a = pop();
                if (a.type == ValueType::NUMBER && b.type == ValueType::NUMBER) {
                    push(Value::number(std::get<double>(a.data) + std::get<double>(b.data)));
                } else if (a.type == ValueType::STRING || b.type == ValueType::STRING) {
                    push(Value::string(a.toString() + b.toString()));
                } else if (a.type == ValueType::ARRAY && b.type == ValueType::ARRAY) {
                    auto arrA = std::get<std::shared_ptr<VionArray>>(a.data);
                    auto arrB = std::get<std::shared_ptr<VionArray>>(b.data);
                    auto newArr = std::make_shared<VionArray>();
                    newArr->elements = arrA->elements;
                    newArr->elements.insert(newArr->elements.end(), arrB->elements.begin(), arrB->elements.end());
                    push(Value::array(newArr));
                } else {
                    std::cerr << "Runtime Error: operands must be numbers or arrays, or at least one must be a string.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SUBTRACT): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number(std::get<double>(a.data) - std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_MULTIPLY): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number(std::get<double>(a.data) * std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_DIVIDE): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                double denominator = std::get<double>(b.data);
                if (denominator == 0) {
                    if (!handleError("division by zero.")) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                    break;
                }
                push(Value::number(std::get<double>(a.data) / denominator));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_MODULO): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                double denominator = std::get<double>(b.data);
                if (denominator == 0) {
                    std::cerr << "Runtime Error: modulo by zero.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                push(Value::number(std::fmod(std::get<double>(a.data), denominator)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_NOT): {
                Value a = pop();
                bool isFalse = (a.type == ValueType::NIL) || 
                               (a.type == ValueType::BOOLEAN && !std::get<bool>(a.data));
                push(Value::boolean(isFalse));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_NEGATE): {
                Value a = pop();
                if (a.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number(-std::get<double>(a.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_PRINT): {
                std::cout << pop().toString() << "\n";
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_POP): {
                pop();
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_DUP): {
                push(stack.back());
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL): {
                Value name = readConstant();
                globals[name.toString()] = pop();
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_GET_GLOBAL): {
                Value name = readConstant();
                std::string key = name.toString();
                auto it = globals.find(key);
                if (it == globals.end()) {
                    std::cerr << "Runtime Error: Undefined variable '" << key << "'.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                push(it->second);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SET_GLOBAL): {
                Value name = readConstant();
                std::string key = name.toString();
                if (globals.find(key) == globals.end()) {
                    std::cerr << "Runtime Error: Undefined variable '" << key << "'.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                globals[key] = stack.back(); // peek, don't pop for assignment
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_GET_LOCAL): {
                uint8_t slot = readByte();
                push(stack[frames.back().slots_base + slot]);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SET_LOCAL): {
                uint8_t slot = readByte();
                stack[frames.back().slots_base + slot] = stack.back(); // peek
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE): {
                uint16_t offset = readShort();
                Value condition = stack.back(); // peek
                bool isFalse = (condition.type == ValueType::NIL) || 
                               (condition.type == ValueType::BOOLEAN && !std::get<bool>(condition.data));
                if (isFalse) frames.back().ip += offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_JUMP): {
                uint16_t offset = readShort();
                frames.back().ip += offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_LOOP): {
                uint16_t offset = readShort();
                frames.back().ip -= offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_TRY_BEGIN): {
                uint16_t catchOffset = readShort();
                TryHandler handler;
                handler.frameIndex = frames.size() - 1;
                handler.stackSize = stack.size();
                handler.catchIp = frames.back().ip + catchOffset;
                tryHandlers.push_back(handler);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_TRY_END): {
                tryHandlers.pop_back();
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_CLOSURE): {
                Value constant = readConstant();
                auto function = std::get<std::shared_ptr<BytecodeFunction>>(constant.data);
                push(Value::bytecodeFunction(function));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_CALL): {
                int argCount = readByte();
                Value callee = stack[stack.size() - 1 - argCount];
                if (callee.type == ValueType::BYTECODE_FUNCTION) {
                    auto function = std::get<std::shared_ptr<BytecodeFunction>>(callee.data);
                    int required = function->requiredArity > 0 ? function->requiredArity : function->arity;
                    if (argCount < required || argCount > function->arity) {
                        std::string msg = "Expected ";
                        if (required == function->arity) msg += std::to_string(function->arity);
                        else msg += std::to_string(required) + "-" + std::to_string(function->arity);
                        msg += " arguments but got " + std::to_string(argCount) + ".";
                        if (!handleError(msg)) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                        break;
                    }
                    for (int i = argCount; i < function->arity; i++) push(Value::nil());
                    CallFrame frame;
                    frame.function = function;
                    frame.ip = function->chunk->code.data();
                    frame.slots_base = stack.size() - function->arity - 1;
                    frames.push_back(frame);
                } else if (callee.type == ValueType::CLASS) {
                    // Instantiate class: replace CLASS value on stack with new INSTANCE
                    auto klass = std::get<std::shared_ptr<VionClass>>(callee.data);
                    auto inst = std::make_shared<VionInstance>();
                    inst->klass = klass;
                    Value instanceVal = Value::instance(inst);
                    // Replace class on stack with instance (self = slot 0, the callee slot)
                    stack[stack.size() - 1 - argCount] = instanceVal;
                    // If init method exists, call it like a regular function
                    auto initIt = klass->methods.find("init");
                    if (initIt != klass->methods.end()) {
                        auto initFn = std::get<std::shared_ptr<BytecodeFunction>>(initIt->second.data);
                        int initArity = initFn->arity; // arity = declared params, NOT including self
                        if (argCount < initArity || argCount > initArity) {
                            if (!handleError("init() expects " + std::to_string(initArity) + " arguments, got " + std::to_string(argCount) + "."))
                                return InterpretResult::INTERPRET_RUNTIME_ERROR;
                            break;
                        }
                        for (int i = argCount; i < initArity; i++) push(Value::nil());
                        CallFrame frame;
                        frame.function = initFn;
                        frame.ip = initFn->chunk->code.data();
                        // Same convention as regular functions: slots_base = N - arity - 1
                        // slot[slots_base + 0] = instance (self), slot[slots_base + 1] = arg1, ...
                        frame.slots_base = stack.size() - initFn->arity - 1;
                        frames.push_back(frame);
                    } else {
                        // No init: instance is already on stack, no call needed
                        if (argCount > 0) {
                            if (!handleError("Class has no init() but arguments were passed."))
                                return InterpretResult::INTERPRET_RUNTIME_ERROR;
                            break;
                        }
                        // Instance already at stack[size - 1], no frame needed
                    }
                } else if (callee.type == ValueType::NATIVE_FUNCTION) {
                    auto native = std::get<std::shared_ptr<VMNativeFunction>>(callee.data);
                    Value result = native->function(argCount, stack.data() + stack.size() - argCount);
                    stack.erase(stack.begin() + stack.size() - argCount - 1, stack.end());
                    push(result);
                } else {
                    std::cerr << "Runtime Error: Can only call functions.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_RETURN): {
                Value result = pop();
                CallFrame frame = frames.back();
                frames.pop_back();
                if (frames.empty()) {
                    pop(); // pop the script function
                    return InterpretResult::INTERPRET_OK;
                }
                // If returning from 'init', always return self (instance at slot 0)
                if (frame.function->name == "init" && frame.slots_base < stack.size()) {
                    Value selfVal = stack[frame.slots_base];
                    if (selfVal.type == ValueType::INSTANCE) {
                        stack.erase(stack.begin() + frame.slots_base, stack.end());
                        push(selfVal);
                        break;
                    }
                }
                stack.erase(stack.begin() + frame.slots_base, stack.end());
                push(result);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_BUILD_ARRAY): {
                int count = readByte();
                auto arr = std::make_shared<VionArray>();
                for (int i = 0; i < count; ++i) {
                    arr->elements.insert(arr->elements.begin(), pop());
                }
                push(Value::array(arr));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_BUILD_MAP): {
                int count = readByte();
                auto map = std::make_shared<VionMap>();
                for (int i = 0; i < count; ++i) {
                    Value value = pop();
                    Value key = pop();
                    map->entries[key.toString()] = value;
                }
                push(Value::map(map));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_INDEX_GET): {
                Value index = pop();
                Value collection = pop();
                if (collection.type == ValueType::ARRAY && index.type == ValueType::NUMBER) {
                    auto arr = std::get<std::shared_ptr<VionArray>>(collection.data);
                    int idx = static_cast<int>(std::get<double>(index.data));
                    if (idx < 0) idx += arr->elements.size();
                    if (idx >= 0 && idx < arr->elements.size()) {
                        push(arr->elements[idx]);
                    } else {
                        push(Value::nil());
                    }
                } else if (collection.type == ValueType::MAP && index.type == ValueType::STRING) {
                    auto map = std::get<std::shared_ptr<VionMap>>(collection.data);
                    auto it = map->entries.find(index.toString());
                    if (it != map->entries.end()) {
                        push(it->second);
                    } else {
                        push(Value::nil());
                    }
                } else {
                    std::cerr << "Runtime Error: Invalid index get operation.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_INDEX_SET): {
                Value value = pop();
                Value index = pop();
                Value obj = pop();
                if (obj.type == ValueType::ARRAY && index.type == ValueType::NUMBER) {
                    auto arr = std::get<std::shared_ptr<VionArray>>(obj.data);
                    int idx = static_cast<int>(std::get<double>(index.data));
                    if (idx >= 0 && idx < arr->elements.size()) {
                        arr->elements[idx] = value;
                        push(value);
                    } else {
                        std::cerr << "Runtime Error: Array index out of bounds.\n";
                        return InterpretResult::INTERPRET_RUNTIME_ERROR;
                    }
                } else if (obj.type == ValueType::MAP && index.type == ValueType::STRING) {
                    auto map = std::get<std::shared_ptr<VionMap>>(obj.data);
                    map->entries[index.toString()] = value;
                    push(value);
                } else {
                    std::cerr << "Runtime Error: Invalid index set operation.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_GET_PROPERTY): {
                Value name = readConstant();
                Value receiver = pop();
                std::string propName = name.toString();
                
                if (receiver.type == ValueType::INSTANCE) {
                    auto inst = std::get<std::shared_ptr<VionInstance>>(receiver.data);
                    // Check instance fields first
                    auto fit = inst->fields.find(propName);
                    if (fit != inst->fields.end()) { push(fit->second); break; }
                    // Then class methods
                    auto* klass = inst->klass.get();
                    while (klass) {
                        auto mit = klass->methods.find(propName);
                        if (mit != klass->methods.end()) { push(mit->second); break; }
                        klass = klass->superclass.get();
                    }
                    if (!klass) push(Value::nil());
                } else if (receiver.type == ValueType::MAP) {
                    auto map = std::get<std::shared_ptr<VionMap>>(receiver.data);
                    auto it = map->entries.find(propName);
                    push(it != map->entries.end() ? it->second : Value::nil());
                } else if (receiver.type == ValueType::CLASS) {
                    auto klass = std::get<std::shared_ptr<VionClass>>(receiver.data);
                    auto it = klass->statics.find(propName);
                    push(it != klass->statics.end() ? it->second : Value::nil());
                } else {
                    std::cerr << "Runtime Error: Only instances, maps, and classes support property access.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SET_PROPERTY): {
                Value name = readConstant();
                Value value = pop();
                Value receiver = pop(); // receiver is under value on stack
                std::string propName = name.toString();
                
                if (receiver.type == ValueType::INSTANCE) {
                    auto inst = std::get<std::shared_ptr<VionInstance>>(receiver.data);
                    inst->fields[propName] = value;
                    push(value);
                } else if (receiver.type == ValueType::MAP) {
                    auto map = std::get<std::shared_ptr<VionMap>>(receiver.data);
                    map->entries[propName] = value;
                    push(value);
                } else {
                    std::cerr << "Runtime Error: Only instances and maps support property assignment.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_INVOKE): {
                Value methodName = readConstant();
                int argCount = readByte();
                std::string mName = methodName.toString();
                Value receiver = stack[stack.size() - argCount - 1];
                
                if (receiver.type == ValueType::INSTANCE) {
                    auto inst = std::get<std::shared_ptr<VionInstance>>(receiver.data);
                    // Look up method in class hierarchy
                    Value methodVal;
                    bool found = false;
                    auto* klass = inst->klass.get();
                    while (klass && !found) {
                        auto mit = klass->methods.find(mName);
                        if (mit != klass->methods.end()) { methodVal = mit->second; found = true; }
                        else klass = klass->superclass.get();
                    }
                    if (!found) {
                        if (!handleError("Undefined method '" + mName + "' on " + inst->klass->name + "."))
                            return InterpretResult::INTERPRET_RUNTIME_ERROR;
                        break;
                    }
                    // receiver is already at stack[size - argCount - 1] (self = slot 0)
                    auto function = std::get<std::shared_ptr<BytecodeFunction>>(methodVal.data);
                    for (int i = argCount; i < function->arity; i++) push(Value::nil()); // pad optional args
                    CallFrame frame;
                    frame.function = function;
                    frame.ip = function->chunk->code.data();
                    frame.slots_base = stack.size() - function->arity - 1;
                    frames.push_back(frame);
                } else if (receiver.type == ValueType::MAP) {
                    auto map = std::get<std::shared_ptr<VionMap>>(receiver.data);
                    auto it = map->entries.find(mName);
                    if (it == map->entries.end()) {
                        if (!handleError("Undefined method '" + mName + "'.")) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                        break;
                    }
                    Value methodVal = it->second;
                    stack[stack.size() - argCount - 1] = methodVal;
                    if (methodVal.type == ValueType::BYTECODE_FUNCTION) {
                        auto function = std::get<std::shared_ptr<BytecodeFunction>>(methodVal.data);
                        CallFrame frame;
                        frame.function = function;
                        frame.ip = function->chunk->code.data();
                        frame.slots_base = stack.size() - argCount - 1;
                        frames.push_back(frame);
                    } else if (methodVal.type == ValueType::NATIVE_FUNCTION) {
                        auto native = std::get<std::shared_ptr<VMNativeFunction>>(methodVal.data);
                        Value result = native->function(argCount, stack.data() + stack.size() - argCount);
                        stack.erase(stack.begin() + stack.size() - argCount - 1, stack.end());
                        push(result);
                    }
                } else {
                    // Fallback to global method lookup
                    auto git = globals.find(mName);
                    if (git == globals.end()) {
                        if (!handleError("Undefined method '" + mName + "'.")) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                        break;
                    }
                    Value methodVal = git->second;
                    stack[stack.size() - argCount - 1] = methodVal;
                    if (methodVal.type == ValueType::NATIVE_FUNCTION) {
                        auto native = std::get<std::shared_ptr<VMNativeFunction>>(methodVal.data);
                        Value result = native->function(argCount, stack.data() + stack.size() - argCount);
                        stack.erase(stack.begin() + stack.size() - argCount - 1, stack.end());
                        push(result);
                    }
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_IMPORT): {
                Value modulePath = pop();
                if (modulePath.type != ValueType::STRING) {
                    if (!handleError("import path must be a string.")) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                    break;
                }
                std::string path = std::get<std::string>(modulePath.data);
                
                std::filesystem::path importPath(path);
                if (importPath.is_relative() && !currentScriptPath.empty()) {
                    importPath = std::filesystem::path(currentScriptPath).parent_path() / importPath;
                }
                std::string absPath = std::filesystem::absolute(importPath).string();
                
                std::ifstream file(absPath);
                if (!file.is_open()) {
                    if (!handleError("Cannot open module file '" + absPath + "'.")) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                    break;
                }
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string source = buffer.str();
                
                Lexer lexer(source);
                std::vector<Token> tokens = lexer.scanTokens();
                Parser parser(std::move(tokens));
                Program program = parser.parse();
                
                Compiler compiler(nullptr, FunctionType::TYPE_SCRIPT);
                auto function = compiler.compile(program);
                
                if (!function) {
                    if (!handleError("Failed to compile module '" + absPath + "'.")) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                    break;
                }
                
                VM moduleVM;
                InterpretResult result = moduleVM.interpret(function, absPath);
                if (result != InterpretResult::INTERPRET_OK) {
                    if (!handleError("Module execution failed '" + absPath + "'.")) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                    break;
                }
                
                auto moduleExports = std::make_shared<VionMap>();
                for (const auto& pair : moduleVM.globals) {
                    if (pair.second.type != ValueType::NATIVE_FUNCTION) {
                        moduleExports->entries[pair.first] = pair.second;
                    }
                }
                push(Value::map(moduleExports));
                break;
            }
            default:
                std::cerr << "Runtime Error: Unknown OpCode: " << static_cast<int>(instruction) << "\n";
                return InterpretResult::INTERPRET_RUNTIME_ERROR;
            // ── Class System ──────────────────────────────────────────────────
            case static_cast<uint8_t>(OpCode::OP_CLASS): {
                Value nameVal = readConstant();
                auto klass = std::make_shared<VionClass>();
                klass->name = nameVal.toString();
                push(Value::vionClass(klass));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_METHOD): {
                Value nameVal = readConstant();
                uint8_t isStatic = readByte();
                Value method = pop(); // the function
                Value classVal = stack.back(); // peek
                if (classVal.type != ValueType::CLASS) {
                    std::cerr << "Runtime Error: OP_METHOD on non-class.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                auto klass = std::get<std::shared_ptr<VionClass>>(classVal.data);
                if (isStatic) klass->statics[nameVal.toString()] = method;
                else          klass->methods[nameVal.toString()] = method;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_INHERIT): {
                // TOS = subclass, TOS-1 = superclass
                Value subclassVal = pop();
                Value superclassVal = stack.back(); // peek super
                if (superclassVal.type != ValueType::CLASS || subclassVal.type != ValueType::CLASS) {
                    std::cerr << "Runtime Error: Superclass must be a class.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                auto super = std::get<std::shared_ptr<VionClass>>(superclassVal.data);
                auto sub = std::get<std::shared_ptr<VionClass>>(subclassVal.data);
                sub->superclass = super;
                // Copy inherited methods (shallow)
                for (auto& [k, v] : super->methods) {
                    if (sub->methods.find(k) == sub->methods.end())
                        sub->methods[k] = v;
                }
                pop(); // pop superclass
                push(subclassVal); // push subclass back
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_GET_SUPER): {
                Value nameVal = readConstant();
                Value self = pop();
                if (self.type != ValueType::INSTANCE) {
                    push(Value::nil()); break;
                }
                auto inst = std::get<std::shared_ptr<VionInstance>>(self.data);
                std::string mName = nameVal.toString();
                auto* klass = inst->klass->superclass.get();
                while (klass) {
                    auto it = klass->methods.find(mName);
                    if (it != klass->methods.end()) { push(it->second); break; }
                    klass = klass->superclass.get();
                }
                if (!klass) push(Value::nil());
                break;
            }
            // ── Bitwise ──────────────────────────────────────────────────────
            case static_cast<uint8_t>(OpCode::OP_BITWISE_AND): {
                Value b = pop(); Value a = pop();
                if (a.type!=ValueType::NUMBER||b.type!=ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number((double)((long long)std::get<double>(a.data) & (long long)std::get<double>(b.data))));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_BITWISE_OR): {
                Value b = pop(); Value a = pop();
                if (a.type!=ValueType::NUMBER||b.type!=ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number((double)((long long)std::get<double>(a.data) | (long long)std::get<double>(b.data))));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_BITWISE_XOR): {
                Value b = pop(); Value a = pop();
                if (a.type!=ValueType::NUMBER||b.type!=ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number((double)((long long)std::get<double>(a.data) ^ (long long)std::get<double>(b.data))));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_BITWISE_NOT): {
                Value a = pop();
                if (a.type!=ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number((double)(~(long long)std::get<double>(a.data))));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SHIFT_LEFT): {
                Value b = pop(); Value a = pop();
                if (a.type!=ValueType::NUMBER||b.type!=ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number((double)((long long)std::get<double>(a.data) << (int)std::get<double>(b.data))));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SHIFT_RIGHT): {
                Value b = pop(); Value a = pop();
                if (a.type!=ValueType::NUMBER||b.type!=ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number((double)((long long)std::get<double>(a.data) >> (int)std::get<double>(b.data))));
                break;
            }
        }
    }
}
