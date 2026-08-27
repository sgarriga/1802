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
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct BasicLine {
    int old_num;
    std::string text;
};

static int next_boundary(int value, int boundary) {
    if (boundary <= 0)
        return value;
    return ((value + boundary - 1) / boundary) * boundary;
}

static std::string to_upper_ascii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s)
        out.push_back(static_cast<char>(std::toupper(ch)));
    return out;
}

static std::string trim_left(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    return s.substr(i);
}

static bool is_ref_literal(const std::string& s, size_t pos, size_t end) {
    if (pos > 0) {
        unsigned char c = static_cast<unsigned char>(s[pos - 1]);
        if (std::isalnum(c) || c == '_' || c == '$' || c == '%' || c == '!')
            return false;
    }
    if (end < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[end]);
        if (std::isalnum(c) || c == '_' || c == '$' || c == '%' || c == '.')
            return false;
    }
    return true;
}

static std::string rewrite_line_references(
    const std::string& text,
    const std::unordered_map<int, int>& old_to_new)
{
    std::string out;
    bool in_string = false;
    char quote = 0;

    for (size_t i = 0; i < text.size(); ) {
        char ch = text[i];

        if (in_string) {
            out += ch;
            if (ch == quote)
                in_string = false;
            ++i;
            continue;
        }

        if (ch == '"') {
            in_string = true;
            quote = ch;
            out += ch;
            ++i;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(ch))) {
            size_t j = i + 1;
            while (j < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[j])))
                ++j;

            int num = std::stoi(text.substr(i, j - i));

            auto it = old_to_new.find(num);
            if (it != old_to_new.end() && is_ref_literal(text, i, j)) {
                out += std::to_string(it->second);
                i = j;
                continue;
            }

            out += text.substr(i, j - i);
            i = j;
            continue;
        }

        out += ch;
        ++i;
    }

    return out;
}

static void collect_gosub_targets(
    const std::vector<BasicLine>& lines,
    std::unordered_set<int>& gosub_targets)
{
    for (const auto& line : lines) {
        if (line.old_num < 0)
            continue;

        std::string upper = to_upper_ascii(line.text);
        size_t pos = 0;
        while ((pos = upper.find("GOSUB", pos)) != std::string::npos) {
            size_t p = pos + 5;
            while (p < upper.size() && std::isspace(static_cast<unsigned char>(upper[p])))
                ++p;

            size_t q = p;
            while (q < upper.size() && std::isdigit(static_cast<unsigned char>(upper[q])))
                ++q;

            if (q > p) {
                int target = std::stoi(upper.substr(p, q - p));
                if (target > 0)
                    gosub_targets.insert(target);
            }

            pos = p;
        }
    }
}

int main(int argc, char** argv) {
    constexpr int DEFAULT_START = 100;
    constexpr int DEFAULT_STEP  = 10;

    bool round_gosub_targets = false;
    bool round_large_gaps = false;
    int start = DEFAULT_START;
    int step = DEFAULT_STEP;
    int boundary = 1000;

    if (argc < 2) {
        std::cerr
            << "Usage: " << argv[0] << " input.bas [output.bas]\n"
            << "       " << argv[0] << " [options] input.bas\n"
            << "Options:\n"
            << "  -s, --start <number>     Starting line number (default: 100)\n"
            << "  -i, --step <number>      Step between line numbers (default: 10)\n"
            << "  -b, --boundary <number>  Round line numbers to next multiple of this (default: 1000)\n"
            << "  -o, --output <file>      Output file (default: stdout)\n"
            << "  --gosub                  Round GOSUB targets\n"
            << "  --gap                    Round large gaps\n"
            << "Defaults: start=100, step=10, boundary=1000, output=stdout\n";
        return 1;
    }

    std::string input_path;
    std::string output_path;
    bool positional_mode = false;
    int i = 1;

    if (argv[1][0] != '-') {
        positional_mode = true;
        input_path = argv[1];
        i = 2;
        if (i < argc && argv[i][0] != '-') {
            output_path = argv[i];
            ++i;
        }
    }

    for (; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--gosub") {
            round_gosub_targets = true;
            continue;
        }

        if (arg == "--gap") {
            round_large_gaps = true;
            continue;
        }

        if (arg == "-s" || arg == "--start") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --start\n";
                return 1;
            }
            start = std::stoi(argv[++i]);
            continue;
        }

        if (arg == "-i" || arg == "--step") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --step\n";
                return 1;
            }
            step = std::stoi(argv[++i]);
            continue;
        }

        if (arg == "-b" || arg == "--boundary") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --boundary\n";
                return 1;
            }
            boundary = std::stoi(argv[++i]);
            continue;
        }

        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --output\n";
                return 1;
            }
            output_path = argv[++i];
            continue;
        }

        if (input_path.empty()) {
            input_path = arg;
            continue;
        }

        if (positional_mode && output_path.empty()) {
            output_path = arg;
            continue;
        }

        std::cerr << "Unknown option: " << arg << "\n";
        return 1;
    }

    if (input_path.empty()) {
        std::cerr << "Input file is required\n";
        return 1;
    }

    if (step <= 0) {
        std::cerr << "Step must be > 0\n";
        return 2;
    }

    if (boundary <= 0) {
        std::cerr << "Boundary must be > 0\n";
        return 2;
    }

    std::ifstream in(input_path);
    if (!in) {
        std::cerr << "Cannot open input file: " << input_path << "\n";
        return 3;
    }

    std::ostream* out_stream = &std::cout;
    std::ofstream out_file;

    if (!output_path.empty()) {
        out_file.open(output_path);
        if (!out_file) {
            std::cerr << "Cannot open output file: " << output_path << "\n";
            return 4;
        }
        out_stream = &out_file;
    }

    std::vector<BasicLine> lines;
    std::string raw;

    while (std::getline(in, raw)) {
        // Remove CR from CRLF files
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();

        std::string s = trim_left(raw);
        if (s.empty()) {
            continue;
        }

        size_t pos = 0;
        while (pos < s.size() &&
               std::isdigit(static_cast<unsigned char>(s[pos])))
            ++pos;

        if (pos > 0) {
            int old_num = std::stoi(s.substr(0, pos));
            std::string rest = trim_left(s.substr(pos));
            lines.push_back({old_num, rest});
        } else {
            // keep non-numbered lines as-is
            lines.push_back({-1, raw});
        }
    }

    std::unordered_map<int, int> old_to_new;
    std::unordered_set<int> gosub_targets;
    if (round_gosub_targets)
        collect_gosub_targets(lines, gosub_targets);

    int new_num = start;
    int previous_old_num = -1;

    for (auto& line : lines) {
        if (line.old_num >= 0) {
            if (round_large_gaps) {
                if (previous_old_num >= 0 && (line.old_num - previous_old_num > 100))
                    new_num = next_boundary(new_num, boundary);
            }

            if (round_gosub_targets && gosub_targets.count(line.old_num) > 0)
                new_num = next_boundary(new_num, boundary);

            old_to_new[line.old_num] = new_num;
            previous_old_num = line.old_num;
            new_num += step;
        }
    }

    for (const auto& line : lines) {
        if (line.old_num >= 0) {
            std::string rewritten = rewrite_line_references(line.text, old_to_new);
            if (rewritten.empty()) {
                *out_stream << old_to_new[line.old_num] << '\n';
            } else {
                *out_stream << old_to_new[line.old_num] << " " << rewritten << '\n';
            }
        } else {
            *out_stream << line.text << '\n';
        }
    }

    return 0;
}