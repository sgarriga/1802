/*
MIT License

Copyright (c) 2026 Stephen Garriga

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class VariableKind {
    Numeric,
    String,
    Array
};

struct VariableOccurrence {
    std::string name;
    VariableKind kind;
    size_t start;
    size_t end;
};

struct SourceLine {
    int number;
    std::string text;
    std::vector<VariableOccurrence> variables;
};

static std::string to_upper_ascii(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char ch : text)
        result.push_back(static_cast<char>(std::toupper(ch)));
    return result;
}

static bool is_keyword_or_function(const std::string& name) {
    static const std::unordered_set<std::string> names = {
        "ABS", "AND", "ASC", "ATN", "CHR$", "CLD", "CLR", "CLS", "COS",
        "DATA", "DEF", "DIM", "END", "EXP", "FOR", "GET", "GOSUB", "GOTO",
        "IF", "INPUT", "INT", "LET", "LOG", "NEXT", "NOT", "ON",
        "OR", "PEEK", "POKE", "PRINT", "READ", "REM", "RETURN",
        "RND", "SGN", "SIN", "SQR", "STEP", "STOP", "TAB", "TAN", "THEN",
        "TO", "VAL", "WAIT", "WEND", "WHILE"
    };
    return names.count(name) != 0;
}

static bool valid_string_name(const std::string& name) {
    return name.size() == 2 && std::isalpha(static_cast<unsigned char>(name[0])) &&
           name[1] == '$';
}

static bool valid_numeric_name(const std::string& name) {
    if (name.size() == 1)
        return std::isalpha(static_cast<unsigned char>(name[0]));
    return name.size() == 2 &&
           std::isalpha(static_cast<unsigned char>(name[0])) &&
           std::isdigit(static_cast<unsigned char>(name[1]));
}

static bool valid_array_name(const std::string& name) {
    return name.size() == 1 &&
           std::isalpha(static_cast<unsigned char>(name[0]));
}

static bool is_valid_name(const std::string& name, VariableKind kind) {
    switch (kind) {
    case VariableKind::Numeric:
        return valid_numeric_name(name);
    case VariableKind::String:
        return valid_string_name(name);
    case VariableKind::Array:
        return valid_array_name(name);
    }
    return false;
}

static std::string symbol_key(const std::string& name, VariableKind kind) {
    return std::to_string(static_cast<int>(kind)) + ":" + name;
}

static std::string next_available_numeric(
    const std::unordered_set<std::string>& used)
{
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        std::string candidate(1, letter);
        if (!used.count(candidate))
            return candidate;
    }
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        for (char digit = '0'; digit <= '9'; ++digit) {
            std::string candidate;
            candidate += letter;
            candidate += digit;
            if (!used.count(candidate))
                return candidate;
        }
    }
    return {};
}

static std::string next_available_string(
    const std::unordered_set<std::string>& used)
{
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        std::string candidate(1, letter);
        candidate += '$';
        if (!used.count(candidate))
            return candidate;
    }
    return {};
}

static std::string next_available_array(
    const std::unordered_set<std::string>& used)
{
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        std::string candidate(1, letter);
        if (!used.count(candidate))
            return candidate;
    }
    return {};
}

static std::string replacement_for(
    VariableKind kind,
    const std::unordered_set<std::string>& used)
{
    switch (kind) {
    case VariableKind::Numeric:
        return next_available_numeric(used);
    case VariableKind::String:
        return next_available_string(used);
    case VariableKind::Array:
        return next_available_array(used);
    }
    return {};
}

int main(int argc, char** argv) {
    std::string input_path;
    std::string output_path;
    bool help = false;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "-o" || argument == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << argument << "\n";
                return 2;
            }
            output_path = argv[++i];
        } else if (argument == "-h" || argument == "--help") {
            help = true;
        } else if (argument[0] == '-') {
            std::cerr << "Unknown option: " << argument << "\n";
            return 2;
        } else if (input_path.empty()) {
            input_path = argument;
        } else {
            std::cerr << "Only one input file is allowed\n";
            return 2;
        }
    }

    if (help || input_path.empty()) {
        std::cerr
            << "Usage: " << argv[0] << " [options] input.bas\n"
            << "Options:\n"
            << "  -o, --output <file>  Specify output file\n"
            << "  -h, --help           Show this help message\n";
        return 2;
    }

    std::ifstream input(input_path);
    if (!input) {
        std::cerr << "Cannot open input file: " << input_path << "\n";
        return 2;
    }

    int line_number = 0;
    std::string line;
    std::vector<SourceLine> source;
    std::unordered_set<std::string> used_numeric;
    std::unordered_set<std::string> used_string;
    std::unordered_set<std::string> used_array;

    while (std::getline(input, line)) {
        ++line_number;
        bool in_string = false;
        SourceLine source_line{line_number, line, {}};

        for (size_t i = 0; i < line.size();) {
            if (line[i] == '"') {
                in_string = !in_string;
                ++i;
                continue;
            }

            if (in_string)
                ++i;
            else if (line[i] == '\'')
                break;
            else if (std::isalpha(static_cast<unsigned char>(line[i]))) {
                size_t start = i++;
                while (i < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[i])) ||
                        line[i] == '$'))
                    ++i;

                std::string name = to_upper_ascii(line.substr(start, i - start));
                if (is_keyword_or_function(name)) {
                    if (name == "REM")
                        break;
                    continue;
                }

                size_t next = i;
                while (next < line.size() &&
                       std::isspace(static_cast<unsigned char>(line[next])))
                    ++next;
                bool is_array = next < line.size() && line[next] == '(';
                VariableKind kind = is_array ? VariableKind::Array :
                    (name.back() == '$' ? VariableKind::String :
                                          VariableKind::Numeric);
                source_line.variables.push_back({name, kind, start, i});
                if (kind == VariableKind::String)
                    used_string.insert(name);
                else if (kind == VariableKind::Array)
                    used_array.insert(name);
                else
                    used_numeric.insert(name);
            } else {
                ++i;
            }
        }
        source.push_back(std::move(source_line));
    }

    std::unordered_map<std::string, std::string> replacements;
    std::unordered_set<std::string> reserved_numeric = used_numeric;
    reserved_numeric.insert(used_array.begin(), used_array.end());
    std::unordered_set<std::string> reserved_array = used_array;
    reserved_array.insert(used_numeric.begin(), used_numeric.end());

    for (const auto& source_line : source) {
        for (const auto& variable : source_line.variables) {
            if (is_valid_name(variable.name, variable.kind))
                continue;

            std::string key = symbol_key(variable.name, variable.kind);
            if (replacements.count(key))
                continue;

            const auto& used = variable.kind == VariableKind::String ? used_string :
                (variable.kind == VariableKind::Array ? reserved_array :
                                                        reserved_numeric);
            std::string replacement = replacement_for(variable.kind, used);
            if (replacement.empty()) {
                std::cerr << "No available variable name for '"
                          << variable.name << "'\n";
                return 1;
            }
            replacements[key] = replacement;
            if (variable.kind == VariableKind::String)
                used_string.insert(replacement);
            else if (variable.kind == VariableKind::Array) {
                used_array.insert(replacement);
                reserved_array.insert(replacement);
                reserved_numeric.insert(replacement);
            } else {
                used_numeric.insert(replacement);
                reserved_numeric.insert(replacement);
                reserved_array.insert(replacement);
            }
        }
    }

    std::ostream* output = &std::cout;
    std::ofstream output_file;
    if (!output_path.empty()) {
        output_file.open(output_path);
        if (!output_file) {
            std::cerr << "Cannot open output file: " << output_path << "\n";
            return 2;
        }
        output = &output_file;
    }

    for (const auto& source_line : source) {
        size_t position = 0;
        for (const auto& variable : source_line.variables) {
            *output << source_line.text.substr(position, variable.start - position);
            auto replacement = replacements.find(symbol_key(variable.name, variable.kind));
            if (replacement == replacements.end())
                *output << source_line.text.substr(variable.start,
                                                   variable.end - variable.start);
            else
                *output << replacement->second;
            position = variable.end;
        }
        *output << source_line.text.substr(position) << '\n';
    }

    return 0;
}