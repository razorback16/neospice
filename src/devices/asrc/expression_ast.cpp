#include "devices/asrc/expression_ast.hpp"
#include "core/types.hpp"       // ParseError
#include "parser/tokenizer.hpp" // parse_spice_number
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace neospice {
namespace asrc {

// ===========================================================================
// ExpressionParser
// ===========================================================================

ExpressionParser::ExpressionParser(const std::string& expr,
                                   std::vector<VarRef>& var_refs)
    : expr_(expr), var_refs_(var_refs)
{
    // Build initial var_map from any pre-existing var_refs
    for (size_t i = 0; i < var_refs_.size(); ++i) {
        var_map_[var_key(var_refs_[i])] = static_cast<int>(i);
    }
}

void ExpressionParser::skip_ws() {
    while (pos_ < expr_.size() &&
           std::isspace(static_cast<unsigned char>(expr_[pos_])))
        ++pos_;
}

char ExpressionParser::peek() {
    skip_ws();
    return pos_ < expr_.size() ? expr_[pos_] : '\0';
}

char ExpressionParser::advance() {
    skip_ws();
    return pos_ < expr_.size() ? expr_[pos_++] : '\0';
}

bool ExpressionParser::match(char c) {
    if (peek() == c) { ++pos_; return true; }
    return false;
}

bool ExpressionParser::match(const std::string& s) {
    skip_ws();
    if (pos_ + s.size() <= expr_.size()) {
        for (size_t i = 0; i < s.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(expr_[pos_ + i])) !=
                std::tolower(static_cast<unsigned char>(s[i])))
                return false;
        }
        pos_ += s.size();
        return true;
    }
    return false;
}

std::string ExpressionParser::var_key(const VarRef& ref) {
    std::string k;
    switch (ref.kind) {
    case VarKind::NODE_VOLTAGE:
        k = "V(" + ref.name1 + ")";
        break;
    case VarKind::DIFF_VOLTAGE:
        k = "V(" + ref.name1 + "," + ref.name2 + ")";
        break;
    case VarKind::BRANCH_CURRENT:
        k = "I(" + ref.name1 + ")";
        break;
    }
    // Lowercase for case-insensitive matching
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
    return k;
}

int ExpressionParser::get_or_add_var(const VarRef& ref) {
    std::string k = var_key(ref);
    auto it = var_map_.find(k);
    if (it != var_map_.end()) return it->second;
    int idx = static_cast<int>(var_refs_.size());
    var_refs_.push_back(ref);
    var_map_[k] = idx;
    return idx;
}

std::unique_ptr<ASTNode> ExpressionParser::parse() {
    skip_ws();
    auto root = parse_additive();
    skip_ws();
    if (pos_ < expr_.size()) {
        throw ParseError("ASRC expression: unexpected character '" +
                         std::string(1, expr_[pos_]) + "' at position " +
                         std::to_string(pos_));
    }
    return root;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_additive() {
    auto left = parse_multiplicative();
    while (true) {
        char c = peek();
        if (c == '+') {
            ++pos_;
            auto right = parse_multiplicative();
            auto node = std::make_unique<ASTNode>();
            node->type = NodeType::ADD;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        } else if (c == '-') {
            ++pos_;
            auto right = parse_multiplicative();
            auto node = std::make_unique<ASTNode>();
            node->type = NodeType::SUB;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_multiplicative() {
    auto left = parse_power();
    while (true) {
        char c = peek();
        if (c == '*') {
            // Check for ** (power)
            if (pos_ + 1 < expr_.size() && expr_[pos_ + 1] == '*') break;
            ++pos_;
            auto right = parse_power();
            auto node = std::make_unique<ASTNode>();
            node->type = NodeType::MUL;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        } else if (c == '/') {
            ++pos_;
            auto right = parse_power();
            auto node = std::make_unique<ASTNode>();
            node->type = NodeType::DIV;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_power() {
    auto base = parse_unary();
    skip_ws();
    if (pos_ + 1 < expr_.size() && expr_[pos_] == '*' && expr_[pos_ + 1] == '*') {
        pos_ += 2;
        auto exp = parse_power();  // right-associative
        auto node = std::make_unique<ASTNode>();
        node->type = NodeType::POW;
        node->left = std::move(base);
        node->right = std::move(exp);
        return node;
    }
    // Also support ^ for power
    if (peek() == '^') {
        ++pos_;
        auto exp = parse_power();  // right-associative
        auto node = std::make_unique<ASTNode>();
        node->type = NodeType::POW;
        node->left = std::move(base);
        node->right = std::move(exp);
        return node;
    }
    return base;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_unary() {
    char c = peek();
    if (c == '+') { ++pos_; return parse_unary(); }
    if (c == '-') {
        ++pos_;
        auto operand = parse_unary();
        auto node = std::make_unique<ASTNode>();
        node->type = NodeType::NEGATE;
        node->left = std::move(operand);
        return node;
    }
    return parse_primary();
}

std::unique_ptr<ASTNode> ExpressionParser::parse_primary() {
    skip_ws();
    if (pos_ >= expr_.size())
        throw ParseError("ASRC expression: unexpected end of expression");

    char c = expr_[pos_];

    // Parenthesized sub-expression
    if (c == '(') {
        ++pos_;
        auto inner = parse_additive();
        skip_ws();
        if (pos_ >= expr_.size() || expr_[pos_] != ')')
            throw ParseError("ASRC expression: missing closing parenthesis");
        ++pos_;
        return inner;
    }

    // Number
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
        return parse_number();
    }

    // Identifier: V(), I(), function name, or named constant
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        size_t start = pos_;
        while (pos_ < expr_.size() &&
               (std::isalnum(static_cast<unsigned char>(expr_[pos_])) ||
                expr_[pos_] == '_'))
            ++pos_;
        std::string name = expr_.substr(start, pos_ - start);
        std::string lname = name;
        std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);

        // V(node) or V(n1,n2) reference
        if (lname == "v") {
            return parse_voltage_ref();
        }
        // I(Vname) reference
        if (lname == "i") {
            return parse_current_ref();
        }

        // Named constants
        if (lname == "pi") {
            auto node = std::make_unique<ASTNode>();
            node->type = NodeType::CONSTANT;
            node->value = M_PI;
            return node;
        }
        if (lname == "e") {
            // Only treat as constant if NOT followed by '(' (function call)
            skip_ws();
            if (pos_ >= expr_.size() || expr_[pos_] != '(') {
                auto node = std::make_unique<ASTNode>();
                node->type = NodeType::CONSTANT;
                node->value = M_E;
                return node;
            }
            // Fall through to function parsing below
        }

        // TIME or time (simulation time variable) - treated as constant during
        // each evaluate call. We model it as variable index for now.
        if (lname == "time") {
            VarRef ref;
            ref.kind = VarKind::NODE_VOLTAGE;
            ref.name1 = "__time__";
            int idx = get_or_add_var(ref);
            auto node = std::make_unique<ASTNode>();
            node->type = NodeType::VARIABLE;
            node->var_idx = idx;
            return node;
        }

        // Function call
        skip_ws();
        if (pos_ < expr_.size() && expr_[pos_] == '(') {
            ++pos_;  // consume '('
            return parse_function(lname);
        }

        throw ParseError("ASRC expression: unknown identifier '" + name + "'");
    }

    throw ParseError("ASRC expression: unexpected character '" +
                     std::string(1, c) + "'");
}

std::unique_ptr<ASTNode> ExpressionParser::parse_voltage_ref() {
    skip_ws();
    if (pos_ >= expr_.size() || expr_[pos_] != '(')
        throw ParseError("ASRC expression: expected '(' after V");
    ++pos_;

    // Parse node name(s): V(node) or V(n1, n2)
    skip_ws();
    size_t start = pos_;
    // Node names can contain alphanumeric, underscore, period
    while (pos_ < expr_.size() &&
           (std::isalnum(static_cast<unsigned char>(expr_[pos_])) ||
            expr_[pos_] == '_' || expr_[pos_] == '.'))
        ++pos_;
    std::string node1 = expr_.substr(start, pos_ - start);
    if (node1.empty())
        throw ParseError("ASRC expression: empty node name in V()");

    // Lowercase for case-insensitive node matching
    std::transform(node1.begin(), node1.end(), node1.begin(), ::tolower);

    skip_ws();
    if (pos_ < expr_.size() && expr_[pos_] == ',') {
        // Differential: V(n1, n2)
        ++pos_;
        skip_ws();
        start = pos_;
        while (pos_ < expr_.size() &&
               (std::isalnum(static_cast<unsigned char>(expr_[pos_])) ||
                expr_[pos_] == '_' || expr_[pos_] == '.'))
            ++pos_;
        std::string node2 = expr_.substr(start, pos_ - start);
        std::transform(node2.begin(), node2.end(), node2.begin(), ::tolower);

        skip_ws();
        if (pos_ >= expr_.size() || expr_[pos_] != ')')
            throw ParseError("ASRC expression: missing ')' in V(n1,n2)");
        ++pos_;

        VarRef ref;
        ref.kind = VarKind::DIFF_VOLTAGE;
        ref.name1 = node1;
        ref.name2 = node2;
        int idx = get_or_add_var(ref);
        auto node = std::make_unique<ASTNode>();
        node->type = NodeType::VARIABLE;
        node->var_idx = idx;
        return node;
    }

    if (pos_ >= expr_.size() || expr_[pos_] != ')')
        throw ParseError("ASRC expression: missing ')' in V()");
    ++pos_;

    VarRef ref;
    ref.kind = VarKind::NODE_VOLTAGE;
    ref.name1 = node1;
    int idx = get_or_add_var(ref);
    auto node = std::make_unique<ASTNode>();
    node->type = NodeType::VARIABLE;
    node->var_idx = idx;
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_current_ref() {
    skip_ws();
    if (pos_ >= expr_.size() || expr_[pos_] != '(')
        throw ParseError("ASRC expression: expected '(' after I");
    ++pos_;

    skip_ws();
    size_t start = pos_;
    while (pos_ < expr_.size() &&
           (std::isalnum(static_cast<unsigned char>(expr_[pos_])) ||
            expr_[pos_] == '_' || expr_[pos_] == '.'))
        ++pos_;
    std::string vsrc_name = expr_.substr(start, pos_ - start);
    if (vsrc_name.empty())
        throw ParseError("ASRC expression: empty source name in I()");

    // Lowercase
    std::transform(vsrc_name.begin(), vsrc_name.end(), vsrc_name.begin(), ::tolower);

    skip_ws();
    if (pos_ >= expr_.size() || expr_[pos_] != ')')
        throw ParseError("ASRC expression: missing ')' in I()");
    ++pos_;

    VarRef ref;
    ref.kind = VarKind::BRANCH_CURRENT;
    ref.name1 = vsrc_name;
    int idx = get_or_add_var(ref);
    auto node = std::make_unique<ASTNode>();
    node->type = NodeType::VARIABLE;
    node->var_idx = idx;
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_number() {
    size_t start = pos_;
    // Consume digits, decimal point, and exponent
    while (pos_ < expr_.size()) {
        char ch = expr_[pos_];
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.') {
            ++pos_;
        } else if ((ch == 'e' || ch == 'E') && pos_ > start) {
            ++pos_;
            // Optional sign after exponent
            if (pos_ < expr_.size() && (expr_[pos_] == '+' || expr_[pos_] == '-'))
                ++pos_;
        } else {
            break;
        }
    }

    // Consume SPICE suffix letters: t, g, meg, k, m, u, n, p, f, a
    // A suffix is consumed if:
    //   1. It's a recognized suffix letter/sequence
    //   2. It's NOT followed by another alpha char (which would make it an identifier)
    if (pos_ < expr_.size()) {
        char ch = std::tolower(static_cast<unsigned char>(expr_[pos_]));

        // Check for "meg" (3-letter suffix) first
        bool consumed = false;
        if (ch == 'm' && pos_ + 2 < expr_.size()) {
            std::string three = expr_.substr(pos_, 3);
            std::transform(three.begin(), three.end(), three.begin(), ::tolower);
            if (three == "meg" &&
                (pos_ + 3 >= expr_.size() ||
                 !std::isalpha(static_cast<unsigned char>(expr_[pos_ + 3])))) {
                pos_ += 3;
                consumed = true;
            }
        }

        // Single-letter suffixes
        if (!consumed) {
            if (ch == 't' || ch == 'g' || ch == 'k' || ch == 'm' ||
                ch == 'u' || ch == 'n' || ch == 'p' || ch == 'f' || ch == 'a') {
                // Only consume if NOT followed by another alpha char
                if (pos_ + 1 >= expr_.size() ||
                    !std::isalpha(static_cast<unsigned char>(expr_[pos_ + 1]))) {
                    ++pos_;
                }
            }
        }
    }

    std::string numstr = expr_.substr(start, pos_ - start);
    double val = parse_spice_number(numstr);

    auto node = std::make_unique<ASTNode>();
    node->type = NodeType::CONSTANT;
    node->value = val;
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_function(const std::string& name) {
    // Parse argument list (already consumed '(')
    auto make_unary = [&](NodeType type) -> std::unique_ptr<ASTNode> {
        auto arg = parse_additive();
        skip_ws();
        if (pos_ >= expr_.size() || expr_[pos_] != ')')
            throw ParseError("ASRC expression: missing ')' for function " + name);
        ++pos_;
        auto node = std::make_unique<ASTNode>();
        node->type = type;
        node->left = std::move(arg);
        return node;
    };

    auto make_binary = [&](NodeType type) -> std::unique_ptr<ASTNode> {
        auto arg1 = parse_additive();
        skip_ws();
        if (pos_ >= expr_.size() || expr_[pos_] != ',')
            throw ParseError("ASRC expression: expected ',' in function " + name);
        ++pos_;
        auto arg2 = parse_additive();
        skip_ws();
        if (pos_ >= expr_.size() || expr_[pos_] != ')')
            throw ParseError("ASRC expression: missing ')' for function " + name);
        ++pos_;
        auto node = std::make_unique<ASTNode>();
        node->type = type;
        node->left = std::move(arg1);
        node->right = std::move(arg2);
        return node;
    };

    auto make_ternary = [&](NodeType type) -> std::unique_ptr<ASTNode> {
        auto arg1 = parse_additive();
        skip_ws();
        if (pos_ >= expr_.size() || expr_[pos_] != ',')
            throw ParseError("ASRC expression: expected ',' in function " + name);
        ++pos_;
        auto arg2 = parse_additive();
        skip_ws();
        if (pos_ >= expr_.size() || expr_[pos_] != ',')
            throw ParseError("ASRC expression: expected second ',' in function " + name);
        ++pos_;
        auto arg3 = parse_additive();
        skip_ws();
        if (pos_ >= expr_.size() || expr_[pos_] != ')')
            throw ParseError("ASRC expression: missing ')' for function " + name);
        ++pos_;
        auto node = std::make_unique<ASTNode>();
        node->type = type;
        node->left = std::move(arg1);
        node->mid = std::move(arg2);
        node->right = std::move(arg3);
        return node;
    };

    // Unary functions
    if (name == "sin")   return make_unary(NodeType::SIN);
    if (name == "cos")   return make_unary(NodeType::COS);
    if (name == "tan")   return make_unary(NodeType::TAN);
    if (name == "asin")  return make_unary(NodeType::ASIN);
    if (name == "acos")  return make_unary(NodeType::ACOS);
    if (name == "atan")  return make_unary(NodeType::ATAN);
    if (name == "exp")   return make_unary(NodeType::EXP);
    if (name == "log" || name == "ln")
                         return make_unary(NodeType::LOG);
    if (name == "log10") return make_unary(NodeType::LOG10);
    if (name == "sqrt")  return make_unary(NodeType::SQRT);
    if (name == "abs")   return make_unary(NodeType::ABS);
    if (name == "sinh")  return make_unary(NodeType::SINH);
    if (name == "cosh")  return make_unary(NodeType::COSH);
    if (name == "tanh")  return make_unary(NodeType::TANH);

    // Binary functions
    if (name == "atan2") return make_binary(NodeType::ATAN2);
    if (name == "pow")   return make_binary(NodeType::POW_FN);
    if (name == "min")   return make_binary(NodeType::MIN);
    if (name == "max")   return make_binary(NodeType::MAX);

    // Ternary functions
    if (name == "if" || name == "ternary_fcn")
                         return make_ternary(NodeType::IF_FN);
    if (name == "limit") return make_ternary(NodeType::LIMIT);

    throw ParseError("ASRC expression: unknown function '" + name + "'");
}

// ===========================================================================
// CompiledExpression — compile + evaluate
// ===========================================================================

CompiledExpression CompiledExpression::compile(const std::string& expr) {
    CompiledExpression result;
    result.source_ = expr;

    // Strip optional surrounding braces
    std::string e = expr;
    // Trim whitespace
    while (!e.empty() && std::isspace(static_cast<unsigned char>(e.front()))) e.erase(0, 1);
    while (!e.empty() && std::isspace(static_cast<unsigned char>(e.back()))) e.pop_back();
    if (e.size() >= 2 && e.front() == '{' && e.back() == '}') {
        e = e.substr(1, e.size() - 2);
    }

    ExpressionParser parser(e, result.var_refs_);
    result.root_ = parser.parse();
    return result;
}

double CompiledExpression::evaluate(const std::vector<double>& var_values,
                                    std::vector<double>& derivs) const {
    assert(static_cast<int>(var_values.size()) >= num_vars());
    auto dn = eval_node(root_.get(), var_values, num_vars(), true);
    derivs = std::move(dn.grad);
    return dn.val;
}

double CompiledExpression::evaluate(const std::vector<double>& var_values) const {
    assert(static_cast<int>(var_values.size()) >= num_vars());
    auto dn = eval_node(root_.get(), var_values, num_vars(), false);
    return dn.val;
}

// ---------------------------------------------------------------------------
// Forward-mode automatic differentiation via dual numbers
// ---------------------------------------------------------------------------

CompiledExpression::DualNumber
CompiledExpression::eval_node(const ASTNode* node,
                              const std::vector<double>& var_values,
                              int nv, bool need_grad) const {
    DualNumber result;
    if (need_grad) result.grad.assign(nv, 0.0);

    if (!node) {
        result.val = 0.0;
        return result;
    }

    switch (node->type) {

    case NodeType::CONSTANT:
        result.val = node->value;
        return result;

    case NodeType::VARIABLE: {
        int idx = node->var_idx;
        result.val = var_values[idx];
        if (need_grad) result.grad[idx] = 1.0;
        return result;
    }

    case NodeType::NEGATE: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = -a.val;
        if (need_grad) {
            for (int i = 0; i < nv; ++i) result.grad[i] = -a.grad[i];
        }
        return result;
    }

    case NodeType::ADD: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        auto b = eval_node(node->right.get(), var_values, nv, need_grad);
        result.val = a.val + b.val;
        if (need_grad) {
            for (int i = 0; i < nv; ++i) result.grad[i] = a.grad[i] + b.grad[i];
        }
        return result;
    }

    case NodeType::SUB: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        auto b = eval_node(node->right.get(), var_values, nv, need_grad);
        result.val = a.val - b.val;
        if (need_grad) {
            for (int i = 0; i < nv; ++i) result.grad[i] = a.grad[i] - b.grad[i];
        }
        return result;
    }

    case NodeType::MUL: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        auto b = eval_node(node->right.get(), var_values, nv, need_grad);
        result.val = a.val * b.val;
        if (need_grad) {
            // d(ab)/dx = a'b + ab'
            for (int i = 0; i < nv; ++i)
                result.grad[i] = a.grad[i] * b.val + a.val * b.grad[i];
        }
        return result;
    }

    case NodeType::DIV: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        auto b = eval_node(node->right.get(), var_values, nv, need_grad);
        result.val = a.val / b.val;
        if (need_grad) {
            // d(a/b)/dx = (a'b - ab') / b^2
            double b2 = b.val * b.val;
            for (int i = 0; i < nv; ++i)
                result.grad[i] = (a.grad[i] * b.val - a.val * b.grad[i]) / b2;
        }
        return result;
    }

    case NodeType::POW:
    case NodeType::POW_FN: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        auto b = eval_node(node->right.get(), var_values, nv, need_grad);
        result.val = std::pow(a.val, b.val);
        if (need_grad) {
            // d(a^b)/dx = a^b * (b * a'/a + b' * ln(a))
            double da_coeff = b.val * std::pow(a.val, b.val - 1.0);
            double db_coeff = (a.val > 0.0) ? result.val * std::log(a.val) : 0.0;
            for (int i = 0; i < nv; ++i)
                result.grad[i] = da_coeff * a.grad[i] + db_coeff * b.grad[i];
        }
        return result;
    }

    // --- Unary functions ---

    case NodeType::SIN: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::sin(a.val);
        if (need_grad) {
            double d = std::cos(a.val);
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::COS: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::cos(a.val);
        if (need_grad) {
            double d = -std::sin(a.val);
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::TAN: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::tan(a.val);
        if (need_grad) {
            double c = std::cos(a.val);
            double d = 1.0 / (c * c);  // sec^2
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::ASIN: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::asin(a.val);
        if (need_grad) {
            double d = 1.0 / std::sqrt(1.0 - a.val * a.val);
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::ACOS: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::acos(a.val);
        if (need_grad) {
            double d = -1.0 / std::sqrt(1.0 - a.val * a.val);
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::ATAN: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::atan(a.val);
        if (need_grad) {
            double d = 1.0 / (1.0 + a.val * a.val);
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::EXP: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::exp(a.val);
        if (need_grad) {
            for (int i = 0; i < nv; ++i) result.grad[i] = result.val * a.grad[i];
        }
        return result;
    }

    case NodeType::LOG: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::log(a.val);
        if (need_grad) {
            double d = 1.0 / a.val;
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::LOG10: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::log10(a.val);
        if (need_grad) {
            double d = 1.0 / (a.val * std::log(10.0));
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::SQRT: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::sqrt(a.val);
        if (need_grad) {
            double d = 0.5 / result.val;
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::ABS: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::abs(a.val);
        if (need_grad) {
            // Subgradient: sign(a); 0 at a=0
            double d = (a.val > 0.0) ? 1.0 : (a.val < 0.0) ? -1.0 : 0.0;
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::SINH: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::sinh(a.val);
        if (need_grad) {
            double d = std::cosh(a.val);
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::COSH: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::cosh(a.val);
        if (need_grad) {
            double d = std::sinh(a.val);
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    case NodeType::TANH: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        result.val = std::tanh(a.val);
        if (need_grad) {
            double d = 1.0 - result.val * result.val;  // sech^2
            for (int i = 0; i < nv; ++i) result.grad[i] = d * a.grad[i];
        }
        return result;
    }

    // --- Binary functions ---

    case NodeType::ATAN2: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        auto b = eval_node(node->right.get(), var_values, nv, need_grad);
        result.val = std::atan2(a.val, b.val);
        if (need_grad) {
            double r2 = a.val * a.val + b.val * b.val;
            double da = b.val / r2;
            double db = -a.val / r2;
            for (int i = 0; i < nv; ++i)
                result.grad[i] = da * a.grad[i] + db * b.grad[i];
        }
        return result;
    }

    case NodeType::MIN: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        auto b = eval_node(node->right.get(), var_values, nv, need_grad);
        if (a.val <= b.val) {
            result.val = a.val;
            if (need_grad) result.grad = std::move(a.grad);
        } else {
            result.val = b.val;
            if (need_grad) result.grad = std::move(b.grad);
        }
        return result;
    }

    case NodeType::MAX: {
        auto a = eval_node(node->left.get(), var_values, nv, need_grad);
        auto b = eval_node(node->right.get(), var_values, nv, need_grad);
        if (a.val >= b.val) {
            result.val = a.val;
            if (need_grad) result.grad = std::move(a.grad);
        } else {
            result.val = b.val;
            if (need_grad) result.grad = std::move(b.grad);
        }
        return result;
    }

    // --- Ternary ---

    case NodeType::IF_FN: {
        auto cond = eval_node(node->left.get(), var_values, nv, false);
        if (cond.val > 0.0) {
            auto then_val = eval_node(node->mid.get(), var_values, nv, need_grad);
            result.val = then_val.val;
            if (need_grad) result.grad = std::move(then_val.grad);
        } else {
            auto else_val = eval_node(node->right.get(), var_values, nv, need_grad);
            result.val = else_val.val;
            if (need_grad) result.grad = std::move(else_val.grad);
        }
        return result;
    }

    case NodeType::LIMIT: {
        // limit(x, lo, hi) = min(max(x, lo), hi)
        auto x = eval_node(node->left.get(), var_values, nv, need_grad);
        auto lo = eval_node(node->mid.get(), var_values, nv, false);
        auto hi = eval_node(node->right.get(), var_values, nv, false);
        if (x.val < lo.val) {
            result.val = lo.val;
            // Gradient is zero (clamped at low)
        } else if (x.val > hi.val) {
            result.val = hi.val;
            // Gradient is zero (clamped at high)
        } else {
            result.val = x.val;
            if (need_grad) result.grad = std::move(x.grad);
        }
        return result;
    }

    } // switch

    // Should not reach here
    result.val = 0.0;
    return result;
}

} // namespace asrc
} // namespace neospice
