#include "parser/expression.hpp"
#include "parser/tokenizer.hpp"
#include "core/types.hpp"
#include <cctype>
#include <stdexcept>

namespace neospice {

namespace {

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
        double left = parse_unary();
        while (true) {
            char c = peek();
            if (c == '*') { ++pos_; left *= parse_unary(); }
            else if (c == '/') { ++pos_; left /= parse_unary(); }
            else break;
        }
        return left;
    }

    double parse_unary() {
        char c = peek();
        if (c == '+') { ++pos_; return parse_unary(); }
        if (c == '-') { ++pos_; return -parse_unary(); }
        return parse_primary();
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
            // Consume digits, dots, exponents, and suffix characters
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

        // Parameter name (identifier)
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = pos_;
            while (pos_ < expr_.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr_[pos_])) || expr_[pos_] == '_'))
                ++pos_;
            std::string name = expr_.substr(start, pos_ - start);
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

} // anonymous namespace

double eval_expression(const std::string& expr,
                       const std::unordered_map<std::string, double>& params) {
    ExprParser parser(expr, params);
    return parser.parse();
}

} // namespace neospice
