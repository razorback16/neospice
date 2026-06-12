#include "parser/expression.hpp"
#include "parser/tokenizer.hpp"
#include "core/types.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace neospice {

namespace {

thread_local std::mt19937 tls_rng{std::random_device{}()};
thread_local std::normal_distribution<double> tls_normal{0.0, 1.0};
thread_local std::uniform_real_distribution<double> tls_uniform{-1.0, 1.0};

double gauss0() { return tls_normal(tls_rng); }
double uniform_minus1_plus1() { return tls_uniform(tls_rng); }

// ---------------------------------------------------------------------------
// Recursive-descent expression parser
// ---------------------------------------------------------------------------
// Grammar (lowest to highest precedence):
//   logical_or     : logical_xor ( '|' logical_xor )*
//   logical_xor    : logical_and ( '^' logical_and )*
//   logical_and    : equality ( '&' equality )*
//   equality       : relational ( ('==' | '!=') relational )*
//   relational     : additive ( ('<' | '<=' | '>' | '>=') additive )*
//   additive       : multiplicative ( ('+' | '-') multiplicative )*
//   multiplicative : power ( ('*' | '/') power )*
//   power          : unary ( '**' unary )*   (right-associative via recursion)
//   unary          : ('+' | '-' | '~') unary | primary
//   primary        : '(' logical_or ')'
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
        double result = parse_logical_or();
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

    double parse_logical_or() {
        double left = parse_logical_xor();
        while (peek() == '|') {
            ++pos_;
            double right = parse_logical_xor();
            left = (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
        }
        return left;
    }

    double parse_logical_xor() {
        double left = parse_logical_and();
        while (peek() == '^') {
            ++pos_;
            double right = parse_logical_and();
            left = ((left != 0.0) != (right != 0.0)) ? 1.0 : 0.0;
        }
        return left;
    }

    double parse_logical_and() {
        double left = parse_equality();
        while (peek() == '&') {
            ++pos_;
            double right = parse_equality();
            left = (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
        }
        return left;
    }

    double parse_equality() {
        double left = parse_relational();
        while (true) {
            skip_ws();
            if (pos_ + 1 < expr_.size()) {
                if (expr_[pos_] == '=' && expr_[pos_ + 1] == '=') {
                    pos_ += 2;
                    left = (left == parse_relational()) ? 1.0 : 0.0;
                } else if (expr_[pos_] == '!' && expr_[pos_ + 1] == '=') {
                    pos_ += 2;
                    left = (left != parse_relational()) ? 1.0 : 0.0;
                } else break;
            } else break;
        }
        return left;
    }

    double parse_relational() {
        double left = parse_additive();
        while (true) {
            skip_ws();
            if (pos_ < expr_.size()) {
                if (expr_[pos_] == '<') {
                    if (pos_ + 1 < expr_.size() && expr_[pos_ + 1] == '=') {
                        pos_ += 2;
                        left = (left <= parse_additive()) ? 1.0 : 0.0;
                    } else {
                        ++pos_;
                        left = (left < parse_additive()) ? 1.0 : 0.0;
                    }
                } else if (expr_[pos_] == '>') {
                    if (pos_ + 1 < expr_.size() && expr_[pos_ + 1] == '=') {
                        pos_ += 2;
                        left = (left >= parse_additive()) ? 1.0 : 0.0;
                    } else {
                        ++pos_;
                        left = (left > parse_additive()) ? 1.0 : 0.0;
                    }
                } else break;
            } else break;
        }
        return left;
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
        if (c == '~') { ++pos_; return parse_unary() != 0.0 ? 0.0 : 1.0; }
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
        args.push_back(parse_logical_or());
        while (true) {
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                args.push_back(parse_logical_or());
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
            // SPICE convention varies; we use cond > 0 as true.
            return (args[0] > 0.0) ? args[1] : args[2];
        }
        if (lname == "gauss") {
            require(3);
            double nominal = args[0], rel_var = args[1], sigma = args[2];
            double stdvar = (sigma != 0.0) ? rel_var * nominal / sigma : 0.0;
            return nominal + stdvar * gauss0();
        }
        if (lname == "agauss") {
            require(3);
            double nominal = args[0], abs_var = args[1], sigma = args[2];
            double stdvar = (sigma != 0.0) ? abs_var / sigma : 0.0;
            return nominal + stdvar * gauss0();
        }
        if (lname == "unif") {
            require(2);
            double nominal = args[0], rel_var = args[1];
            return nominal * (1.0 + rel_var * uniform_minus1_plus1());
        }
        if (lname == "aunif") {
            require(2);
            double nominal = args[0], abs_var = args[1];
            return nominal + abs_var * uniform_minus1_plus1();
        }
        // PSpice functions
        if (lname == "limit") {
            if (args.size() != 3)
                throw ParseError("Function limit() requires 3 arguments");
            // min(max(x, lo), hi). std::clamp is UB when lo > hi, which occurs
            // for models passing reversed bounds (e.g. (IMAX, -IMIN)). Using
            // explicit min/max never invokes UB and matches the ASRC runtime
            // LIMIT node (expression_ast.cpp NodeType::LIMIT) exactly; for a
            // well-ordered range it is identical to std::clamp.
            return std::min(std::max(args[0], args[1]), args[2]);
        }
        if (lname == "stp") { require(1); return args[0] > 0.0 ? 1.0 : 0.0; }
        if (lname == "pwr") { require(2); return std::pow(std::abs(args[0]), args[1]); }
        if (lname == "pwrs") {
            require(2);
            return std::copysign(std::pow(std::abs(args[0]), args[1]), args[0]);
        }
        if (lname == "sgn") {
            require(1);
            return (args[0] > 0.0) ? 1.0 : (args[0] < 0.0 ? -1.0 : 0.0);
        }
        if (lname == "atan2") { require(2); return std::atan2(args[0], args[1]); }
        if (lname == "tan") { require(1); return std::tan(args[0]); }
        if (lname == "asin") { require(1); return std::asin(args[0]); }
        if (lname == "acos") { require(1); return std::acos(args[0]); }
        if (lname == "atan") { require(1); return std::atan(args[0]); }
        if (lname == "sinh") { require(1); return std::sinh(args[0]); }
        if (lname == "cosh") { require(1); return std::cosh(args[0]); }
        if (lname == "tanh") { require(1); return std::tanh(args[0]); }
        if (lname == "table") {
            if (args.size() < 3 || (args.size() - 1) % 2 != 0)
                throw ParseError("Function table() requires odd number of args >= 3");
            double x = args[0];
            // Build pairs and do linear interpolation with endpoint clamping
            size_t n = (args.size() - 1) / 2;
            if (x <= args[1]) return args[2];
            if (x >= args[args.size() - 2]) return args[args.size() - 1];
            for (size_t i = 0; i < n - 1; ++i) {
                double x0 = args[1 + 2*i], y0 = args[2 + 2*i];
                double x1 = args[1 + 2*(i+1)], y1 = args[2 + 2*(i+1)];
                if (x >= x0 && x <= x1) {
                    double t = (x - x0) / (x1 - x0);
                    return y0 + t * (y1 - y0);
                }
            }
            return args[args.size() - 1]; // fallback
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
            double val = parse_logical_or();
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

            // Parameter lookup — SPICE is case-insensitive; normalise to lowercase
            std::string lname = name;
            std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
            auto it = params_.find(lname);
            if (it == params_.end()) {
                fprintf(stderr, "Warning: Unknown parameter '%s' — defaulting to 0\n", name.c_str());
                return 0.0;
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
const std::unordered_set<std::string> kBuiltinFunctions = {
    "sqrt", "abs", "log", "log10", "exp", "sin", "cos",
    "min", "max", "pow", "if",
    "gauss", "agauss", "unif", "aunif",
    "limit", "stp", "pwr", "pwrs", "sgn", "atan2",
    "tan", "asin", "acos", "atan",
    "sinh", "cosh", "tanh", "table"
};

// ---------------------------------------------------------------------------
// Strip optional surrounding braces from an expression string.
// ---------------------------------------------------------------------------
std::string strip_braces(const std::string& s) {
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

/// Return the set of parameter names referenced in `expr`.
std::unordered_set<std::string> scan_dependencies(const std::string& expr) {
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
        // Not a builtin function name used without '(' — add as dependency (lowercase for SPICE)
        std::string lname = name;
        std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
        if (kBuiltinFunctions.find(lname) == kBuiltinFunctions.end()) {
            deps.insert(lname);
        }
    }
    return deps;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string expand_funcs(const std::string& expr,
                         const std::unordered_map<std::string, FuncDef>& func_defs) {
    if (func_defs.empty()) return expr;

    // Work on a lowercase copy for matching, but substitute in the original
    std::string result = expr;

    // Iterate until no more expansions happen (handles nested func calls).
    // Each pass strips one nesting level, so deeply-chained .FUNC definitions
    // (e.g. a -> b -> c -> ... 11+ deep in vendor MOSFET models) need more than
    // a handful of passes; the loop terminates early via the `expanded` flag.
    for (int pass = 0; pass < 64; ++pass) {
        bool expanded = false;
        for (const auto& [fname, fdef] : func_defs) {
            size_t pos = 0;
            // Build a lowercase version for case-insensitive matching
            std::string result_lower;
            result_lower.resize(result.size());
            std::transform(result.begin(), result.end(), result_lower.begin(), ::tolower);

            while ((pos = result_lower.find(fname, pos)) != std::string::npos) {
                // Ensure word boundary before the match
                if (pos > 0 && (std::isalnum(static_cast<unsigned char>(result[pos - 1])) ||
                                result[pos - 1] == '_')) {
                    pos += fname.size();
                    continue;
                }
                size_t after = pos + fname.size();
                // Ensure word boundary after the match (should be '(' or whitespace then '(')
                size_t paren_pos = after;
                while (paren_pos < result.size() && std::isspace(static_cast<unsigned char>(result[paren_pos])))
                    ++paren_pos;
                if (paren_pos >= result.size() || result[paren_pos] != '(') {
                    pos = after;
                    continue;
                }

                // Found func_name( — parse arguments respecting nested parens
                int depth = 1;
                size_t p = paren_pos + 1;
                std::vector<std::string> call_args;
                size_t arg_start = p;
                while (p < result.size() && depth > 0) {
                    if (result[p] == '(') ++depth;
                    else if (result[p] == ')') {
                        --depth;
                        if (depth == 0) break;
                    } else if (result[p] == ',' && depth == 1) {
                        call_args.push_back(result.substr(arg_start, p - arg_start));
                        arg_start = p + 1;
                    }
                    ++p;
                }
                if (depth != 0) {
                    // Unmatched parenthesis — skip
                    pos = p;
                    continue;
                }
                call_args.push_back(result.substr(arg_start, p - arg_start));

                if (call_args.size() != fdef.args.size()) {
                    pos = p + 1;
                    continue;
                }

                // Build the substituted body with a SINGLE simultaneous pass
                // over identifiers. Doing per-formal sequential passes is
                // unhygienic: substituting one formal arg's text (e.g. g ->
                // (V(g,s))) inserts characters (here, 's') that a later formal
                // pass would wrongly capture, corrupting the expression. We
                // instead scan the body once, and whenever an identifier
                // matches a formal parameter name, emit that formal's actual
                // argument verbatim (never re-scanning inserted argument text).
                const std::string& body = fdef.body;
                // Prepare trimmed, parenthesized replacements for each formal.
                std::unordered_map<std::string, std::string> arg_map;
                for (size_t i = 0; i < fdef.args.size(); ++i) {
                    std::string arg_val = call_args[i];
                    while (!arg_val.empty() && std::isspace(static_cast<unsigned char>(arg_val.front())))
                        arg_val.erase(0, 1);
                    while (!arg_val.empty() && std::isspace(static_cast<unsigned char>(arg_val.back())))
                        arg_val.pop_back();
                    arg_map[fdef.args[i]] = "(" + arg_val + ")";
                }
                std::string sub_body;
                sub_body.reserve(body.size() * 2);
                size_t bp = 0;
                while (bp < body.size()) {
                    char c = body[bp];
                    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                        size_t id_start = bp;
                        while (bp < body.size() &&
                               (std::isalnum(static_cast<unsigned char>(body[bp])) ||
                                body[bp] == '_'))
                            ++bp;
                        std::string ident = body.substr(id_start, bp - id_start);
                        std::string ident_lower = ident;
                        std::transform(ident_lower.begin(), ident_lower.end(),
                                       ident_lower.begin(), ::tolower);
                        auto it = arg_map.find(ident_lower);
                        if (it != arg_map.end()) {
                            sub_body += it->second;
                        } else {
                            sub_body += ident;
                        }
                    } else {
                        sub_body += c;
                        ++bp;
                    }
                }

                // Replace the function call in result with parenthesized expanded body
                size_t call_len = (p + 1) - pos;
                std::string expanded_str = "(" + sub_body + ")";
                result.replace(pos, call_len, expanded_str);
                expanded = true;
                // Rebuild the lowercase view (result changed) and continue
                // scanning for further occurrences of THIS function from the
                // same position. Restarting from scratch after every single
                // replacement makes a self-referential / heavily-reused .FUNC
                // chain (e.g. vendor MOSFET models where one helper is invoked
                // dozens of times) blow up combinatorially: each pass would
                // expand only one occurrence per function, needing an
                // unbounded number of passes. Re-scanning in place keeps the
                // total work proportional to the (large but finite) fully
                // expanded string. pos is left unchanged so nested calls
                // introduced by this body are expanded too.
                result_lower.resize(result.size());
                std::transform(result.begin(), result.end(),
                               result_lower.begin(), ::tolower);
            }
        }
        if (!expanded) break;
    }
    return result;
}

double eval_expression(const std::string& expr,
                       const std::unordered_map<std::string, double>& params) {
    std::string e = strip_braces(expr);
    ExprParser parser(e, params);
    return parser.parse();
}

std::unordered_map<std::string, double> resolve_params(
    const std::vector<std::pair<std::string, std::string>>& raw_params)
{
    // Index by name for fast lookup — lowercase keys for case-insensitive SPICE semantics
    std::unordered_map<std::string, std::string> expr_of;
    expr_of.reserve(raw_params.size());
    // Keep original order for stable topological sort
    std::vector<std::string> names;
    names.reserve(raw_params.size());
    for (const auto& kv : raw_params) {
        std::string lkey = kv.first;
        std::transform(lkey.begin(), lkey.end(), lkey.begin(), ::tolower);
        if (expr_of.count(lkey) == 0) {
            names.push_back(lkey);
        }
        // Last definition wins (matches SPICE semantics where later .param overrides earlier)
        expr_of[lkey] = strip_braces(kv.second);
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
        fprintf(stderr, "Warning: Circular dependency detected in .param definitions — defaulting unresolved to 0\n");
        // Add unresolved params to order so they default to 0
        for (const auto& name : names) {
            bool found = false;
            for (const auto& o : order) {
                if (o == name) { found = true; break; }
            }
            if (!found) order.push_back(name);
        }
    }

    // Evaluate in topological order
    std::unordered_map<std::string, double> resolved;
    resolved.reserve(names.size());
    for (const auto& name : order) {
        try {
            ExprParser parser(expr_of.at(name), resolved);
            resolved[name] = parser.parse();
        } catch (const ParseError&) {
            fprintf(stderr, "Warning: failed to evaluate .param '%s' — defaulting to 0\n", name.c_str());
            resolved[name] = 0.0;
        }
    }

    return resolved;
}

std::string subst_param_names(
    const std::string& expr,
    const std::unordered_map<std::string, double>& params) {
    std::string result;
    result.reserve(expr.size());
    size_t i = 0;
    while (i < expr.size()) {
        if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
            size_t start = i;
            while (i < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_'))
                ++i;
            std::string name = expr.substr(start, i - start);
            // Function call? (identifier followed by '(') — leave alone.
            size_t tmp = i;
            while (tmp < expr.size() && std::isspace(static_cast<unsigned char>(expr[tmp]))) ++tmp;
            if (tmp < expr.size() && expr[tmp] == '(') {
                result += name;
                continue;
            }
            // Component of a dotted hierarchical name? — leave intact.
            bool dotted = (i < expr.size() && expr[i] == '.') ||
                          (start > 0 && expr[start - 1] == '.');
            if (dotted) {
                result += name;
                continue;
            }
            std::string lname;
            lname.reserve(name.size());
            for (char c : name) lname += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto it = params.find(lname);
            if (it != params.end()) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.15g", it->second);
                result += buf;
            } else {
                result += name;
            }
        } else {
            result += expr[i];
            ++i;
        }
    }
    return result;
}

} // namespace neospice
