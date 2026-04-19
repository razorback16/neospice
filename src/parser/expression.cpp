#include "parser/expression.hpp"
#include "parser/tokenizer.hpp"
#include "core/types.hpp"
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace neospice {

namespace {

// ---------------------------------------------------------------------------
// Recursive-descent expression parser
// ---------------------------------------------------------------------------
// Grammar (highest to lowest precedence):
//   additive       : multiplicative ( ('+' | '-') multiplicative )*
//   multiplicative : power ( ('*' | '/') power )*
//   power          : unary ( '**' unary )*   (right-associative via recursion)
//   unary          : ('+' | '-') unary | primary
//   primary        : '(' additive ')'
//                  | number
//                  | identifier '(' arg_list ')'    -- function call
//                  | identifier                      -- parameter lookup

class ExprParser {
public:
    ExprParser(const std::string& expr,
               const std::unordered_map<std::string, double>& params)
        : expr_(expr), params_(params), pos_(0) {}

    double parse() {
        skip_ws();
        double result = parse_additive();
        skip_ws();
        if (pos_ < expr_.size()) {
            throw ParseError("Unexpected character in expression: '" +
                             std::string(1, expr_[pos_]) + "'");
        }
        return result;
    }

private:
    const std::string& expr_;
    const std::unordered_map<std::string, double>& params_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < expr_.size() && std::isspace(static_cast<unsigned char>(expr_[pos_])))
            ++pos_;
    }

    char peek() {
        skip_ws();
        return pos_ < expr_.size() ? expr_[pos_] : '\0';
    }

    // Consume the next character (after whitespace)
    char consume() {
        skip_ws();
        return pos_ < expr_.size() ? expr_[pos_++] : '\0';
    }

    double parse_additive() {
        double left = parse_multiplicative();
        while (true) {
            char c = peek();
            if (c == '+') { ++pos_; left += parse_multiplicative(); }
            else if (c == '-') { ++pos_; left -= parse_multiplicative(); }
            else break;
        }
        return left;
    }

    double parse_multiplicative() {
        double left = parse_power();
        while (true) {
            char c = peek();
            if (c == '*') {
                // Peek ahead for '**' — if so, stop (handled by parse_power)
                if (pos_ + 1 < expr_.size() && expr_[pos_ + 1] == '*') break;
                ++pos_;
                left *= parse_power();
            } else if (c == '/') {
                ++pos_;
                left /= parse_power();
            } else {
                break;
            }
        }
        return left;
    }

    // '**' is right-associative: a**b**c == a**(b**c)
    double parse_power() {
        double base = parse_unary();
        skip_ws();
        if (pos_ + 1 < expr_.size() && expr_[pos_] == '*' && expr_[pos_ + 1] == '*') {
            pos_ += 2;
            double exp = parse_power();  // right-associative recursion
            return std::pow(base, exp);
        }
        return base;
    }

    double parse_unary() {
        char c = peek();
        if (c == '+') { ++pos_; return parse_unary(); }
        if (c == '-') { ++pos_; return -parse_unary(); }
        return parse_primary();
    }

    // Parse a comma-separated argument list and return the values.
    // Assumes we are positioned after the opening '('.
    std::vector<double> parse_arg_list() {
        std::vector<double> args;
        skip_ws();
        if (peek() == ')') {
            ++pos_;  // empty arg list
            return args;
        }
        args.push_back(parse_additive());
        while (true) {
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                args.push_back(parse_additive());
            } else {
                break;
            }
        }
        skip_ws();
        if (peek() != ')') {
            throw ParseError("Missing closing parenthesis in function call");
        }
        ++pos_;
        return args;
    }

    double call_function(const std::string& name, const std::vector<double>& args) {
        auto require = [&](size_t n) {
            if (args.size() != n)
                throw ParseError("Function " + name + " requires " +
                                 std::to_string(n) + " argument(s)");
        };
        // Normalise to lower-case for matching
        std::string lname = name;
        std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);

        if (lname == "sqrt")   { require(1); return std::sqrt(args[0]); }
        if (lname == "abs")    { require(1); return std::abs(args[0]); }
        if (lname == "log")    { require(1); return std::log(args[0]); }
        if (lname == "log10")  { require(1); return std::log10(args[0]); }
        if (lname == "exp")    { require(1); return std::exp(args[0]); }
        if (lname == "sin")    { require(1); return std::sin(args[0]); }
        if (lname == "cos")    { require(1); return std::cos(args[0]); }
        if (lname == "min")    { require(2); return std::min(args[0], args[1]); }
        if (lname == "max")    { require(2); return std::max(args[0], args[1]); }
        if (lname == "pow")    { require(2); return std::pow(args[0], args[1]); }
        if (lname == "if") {
            if (args.size() != 3)
                throw ParseError("Function if() requires 3 arguments");
            return (args[0] > 0.0) ? args[1] : args[2];
        }
        throw ParseError("Unknown function: " + name);
    }

    double parse_primary() {
        skip_ws();
        if (pos_ >= expr_.size()) {
            throw ParseError("Unexpected end of expression");
        }

        char c = expr_[pos_];

        // Parenthesized sub-expression
        if (c == '(') {
            ++pos_;
            double val = parse_additive();
            skip_ws();
            if (pos_ >= expr_.size() || expr_[pos_] != ')') {
                throw ParseError("Missing closing parenthesis");
            }
            ++pos_;
            return val;
        }

        // Number (digit or '.')
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t start = pos_;
            while (pos_ < expr_.size()) {
                char ch = expr_[pos_];
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' ||
                    ch == '+' || ch == '-') {
                    // '+' or '-' only valid after 'e'/'E'
                    if ((ch == '+' || ch == '-') && pos_ > start) {
                        char prev = expr_[pos_ - 1];
                        if (prev != 'e' && prev != 'E') break;
                    }
                    ++pos_;
                } else {
                    break;
                }
            }
            std::string numstr = expr_.substr(start, pos_ - start);
            return parse_spice_number(numstr);
        }

        // Identifier: parameter name or function call
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = pos_;
            while (pos_ < expr_.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr_[pos_])) || expr_[pos_] == '_'))
                ++pos_;
            std::string name = expr_.substr(start, pos_ - start);

            skip_ws();
            // Check for function call
            if (pos_ < expr_.size() && expr_[pos_] == '(') {
                ++pos_;  // consume '('
                auto args = parse_arg_list();
                return call_function(name, args);
            }

            // Parameter lookup
            auto it = params_.find(name);
            if (it == params_.end()) {
                throw ParseError("Unknown parameter: " + name);
            }
            return it->second;
        }

        throw ParseError("Unexpected character in expression: '" +
                         std::string(1, c) + "'");
    }
};

// ---------------------------------------------------------------------------
// Dependency scanner — collects identifiers referenced in an expression
// that are NOT function names.
// ---------------------------------------------------------------------------
static const std::unordered_set<std::string> kBuiltinFunctions = {
    "sqrt", "abs", "log", "log10", "exp", "sin", "cos",
    "min", "max", "pow", "if"
};

/// Return the set of parameter names referenced in `expr`.
static std::unordered_set<std::string> scan_dependencies(const std::string& expr) {
    std::unordered_set<std::string> deps;
    size_t pos = 0;
    while (pos < expr.size()) {
        char c = expr[pos];
        // Skip whitespace and non-identifier chars
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
            ++pos;
            continue;
        }
        // Collect identifier
        size_t start = pos;
        while (pos < expr.size() &&
               (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_'))
            ++pos;
        std::string name = expr.substr(start, pos - start);
        // Skip whitespace after identifier
        size_t tmp = pos;
        while (tmp < expr.size() && std::isspace(static_cast<unsigned char>(expr[tmp]))) ++tmp;
        // If followed by '(' it's a function call — not a parameter reference
        if (tmp < expr.size() && expr[tmp] == '(') {
            pos = tmp;
            continue;
        }
        // Not a builtin function name used without '(' — add as dependency
        std::string lname = name;
        std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
        if (kBuiltinFunctions.find(lname) == kBuiltinFunctions.end()) {
            deps.insert(name);
        }
    }
    return deps;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Strip optional surrounding braces from an expression string.
// ---------------------------------------------------------------------------
static std::string strip_braces(const std::string& s) {
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

double eval_expression(const std::string& expr,
                       const std::unordered_map<std::string, double>& params) {
    std::string e = strip_braces(expr);
    ExprParser parser(e, params);
    return parser.parse();
}

std::unordered_map<std::string, double> resolve_params(
    const std::vector<std::pair<std::string, std::string>>& raw_params)
{
    // Index by name for fast lookup
    std::unordered_map<std::string, std::string> expr_of;
    expr_of.reserve(raw_params.size());
    // Keep original order for stable topological sort
    std::vector<std::string> names;
    names.reserve(raw_params.size());
    for (const auto& kv : raw_params) {
        if (expr_of.count(kv.first) == 0) {
            names.push_back(kv.first);
        }
        // Last definition wins (matches SPICE semantics where later .param overrides earlier)
        expr_of[kv.first] = strip_braces(kv.second);
    }

    // Build adjacency list (who does each param depend on?)
    std::unordered_map<std::string, std::unordered_set<std::string>> deps_of;
    for (const auto& name : names) {
        auto raw_deps = scan_dependencies(expr_of.at(name));
        // Only keep deps that are themselves declared params
        std::unordered_set<std::string> filtered;
        for (const auto& d : raw_deps) {
            if (expr_of.count(d)) filtered.insert(d);
        }
        deps_of[name] = std::move(filtered);
    }

    // Kahn's algorithm for topological sort
    // Build in-degree map and adjacency (reverse: who needs this param)
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> dependents;  // param -> [params that depend on it]
    for (const auto& name : names) {
        if (!in_degree.count(name)) in_degree[name] = 0;
        for (const auto& dep : deps_of.at(name)) {
            in_degree[name]++;  // this param depends on dep => dep must come first
            dependents[dep].push_back(name);
        }
    }

    // Start with params that have no dependencies
    std::vector<std::string> queue;
    for (const auto& name : names) {
        if (in_degree.at(name) == 0) queue.push_back(name);
    }

    std::vector<std::string> order;
    order.reserve(names.size());
    size_t qi = 0;
    while (qi < queue.size()) {
        const std::string& cur = queue[qi++];
        order.push_back(cur);
        if (dependents.count(cur)) {
            for (const auto& dep_name : dependents.at(cur)) {
                if (--in_degree[dep_name] == 0) {
                    queue.push_back(dep_name);
                }
            }
        }
    }

    if (order.size() != names.size()) {
        throw ParseError("Circular dependency detected in .param definitions");
    }

    // Evaluate in topological order
    std::unordered_map<std::string, double> resolved;
    resolved.reserve(names.size());
    for (const auto& name : order) {
        ExprParser parser(expr_of.at(name), resolved);
        resolved[name] = parser.parse();
    }

    return resolved;
}

} // namespace neospice
