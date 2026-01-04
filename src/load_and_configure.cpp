#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <cstdio>
#include <memory>
#include <array>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

/*
 * Execute a shell command and capture stdout
 */
string exec(const char* cmd) {
    array<char, 256> buffer{};
    string result;

    unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw runtime_error("popen() failed");
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

/*
 * Load exclusion regex patterns from JSON config
 */
vector<regex> load_excluded_regex(const string& config_file) {
    ifstream file(config_file);
    vector<regex> patterns;

    if (!file.is_open()) {
        throw runtime_error("Cannot open config file: " + config_file);
    }

    json config;
    file >> config;

    if (!config.contains("excluded_functions")) {
        return patterns;
    }

    for (const auto& p : config["excluded_functions"]) {
        patterns.emplace_back(p.get<string>());
    }

    return patterns;
}

/*
 * Check whether a symbol name matches any exclusion regex
 * NOTE: regex_match is correct because rules use '^'
 */
bool is_excluded(const string& name, const vector<regex>& patterns) {
    for (const auto& r : patterns) {
        if (regex_match(name, r)) {
            return true;
        }
    }
    return false;
}

/*
 * Extract function symbols from nm output and apply regex-based exclusion
 *
 * CRITICAL FIX:
 *  - Use (\S+) to capture the FULL mangled symbol
 *  - Do NOT guess C++ ABI character sets
 */
vector<string> extract_function_names(
    const string& binary_path,
    const vector<regex>& exclude_patterns
) {
    string nm_output = exec(("nm " + binary_path).c_str());
    vector<string> result;

    // address + type(T/W) + full symbol
    //regex nm_regex(R"(^\S+\s+([Tt])\s+(\S+))");
    regex nm_regex(R"(^\S+\s+[T]\s+(\S+))");
    smatch match;

    stringstream ss(nm_output);
    string line;

    while (getline(ss, line)) {
        if (regex_search(line, match, nm_regex)) {
            const string& symbol = match.str(1);
            if (is_excluded(symbol, exclude_patterns)) {
                result.push_back(symbol);
            }
        }
    }

    return result;
}

/*
 * Generate exclusion C++ source
 * (string-based stub; address-based O(1) can be added later)
 */
void generate_exclusion_code(
    const vector<string>& excluded_symbols,
    const string& output_file
) {
    ofstream out(output_file);
    if (!out.is_open()) {
        throw runtime_error("Cannot open output file: " + output_file);
    }

    out << "#include <string>\n";
    out << "#include <unordered_set>\n\n";

    for (const auto& func_name : excluded_symbols) {
	    out << "extern void *" << func_name << ";" << endl;
    }
    // Start generating the function exclusion check code
    out << "extern \"C\" {" << endl;
    out << "bool is_function_excluded(void* this_function) {" << endl;
    out << "    if (this_function == nullptr) return false;" << endl;

    // Create `if` conditions for each excluded function
    for (const auto& func_name : excluded_symbols) {
        out << "    if (this_function == &" << func_name << ") return true;" << endl;
    }

    out << "    return false;" << endl;
    out << "}" << endl;
    out << "}" << endl;
    
}

/*
 * Top-level orchestration
 */
void generate_exclusion_logic(
    const string& binary_path,
    const string& config_file
) {
    auto exclude_patterns = load_excluded_regex(config_file);
    auto filtered_symbols =
        extract_function_names(binary_path, exclude_patterns);

    generate_exclusion_code(filtered_symbols, "generated_exclusion.cpp");

    cout << "Generated exclusion file: generated_exclusion.cpp\n";
    cout << "Excluded symbol count: " << filtered_symbols.size() << endl;
}

int main() {
    string binary_path =
        "extern/llama.cpp/buildinstrument/bin/llama-cli";
    string config_file = "config.json";

    try {
        generate_exclusion_logic(binary_path, config_file);
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
