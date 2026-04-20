#pragma once
// ---------------------------------------------------------------------------
// expression_ast.hpp — Compile-once expression tree with eval + auto-diff
//
// Built for the ASRC (B element) behavioral source.  An expression string
// such as "V(in)*2 + sin(V(ctrl))" is parsed once at setup time into an
// AST.  At each Newton iteration the AST is evaluated, producing:
//   - The function value  f(x0, x1, ...)
//   - Partial derivatives df/dxi  for every referenced circuit variable
//
// Supported variable references:
//   V(node)         — voltage at a node (relative to ground)
//   V(n1, n2)       — differential voltage V(n1) - V(n2)
//   I(Vname)        — branch current through a voltage source
//
// Supported operators:  + - * / ^ (or **)
// Supported functions:  sin cos tan asin acos atan atan2
//                       exp log log10 sqrt abs pow min max
//                       sinh cosh tanh acosh asinh atanh
//                       sgn u ustep uramp ceil floor nint pwr
//                       limit if
// Numeric literals with SPICE suffixes (1k, 2.5m, 100u, etc.)
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace neospice {
namespace asrc {

// ---------------------------------------------------------------------------
// Variable reference — identifies a circuit quantity
// ---------------------------------------------------------------------------
enum class VarKind { NODE_VOLTAGE, DIFF_VOLTAGE, BRANCH_CURRENT };

struct VarRef {
    VarKind kind;
    std::string name1;   // node name (or Vname for I())
    std::string name2;   // second node for V(n1,n2), empty otherwise
};

// ---------------------------------------------------------------------------
// AST node types
// ---------------------------------------------------------------------------
enum class NodeType {
    CONSTANT,
    VARIABLE,       // index into the variable table
    NEGATE,         // unary minus
    ADD, SUB, MUL, DIV, POW,
    // Functions (unary)
    SIN, COS, TAN, ASIN, ACOS, ATAN,
    EXP, LOG, LOG10, SQRT, ABS,
    SINH, COSH, TANH,
    ACOSH, ASINH, ATANH,
    SGN, USTEP, URAMP,
    CEIL, FLOOR, NINT,
    // Functions (binary)
    ATAN2, POW_FN, MIN, MAX, PWR,
    // Ternary
    IF_FN,          // if(cond, then, else) — cond != 0 selects then
    LIMIT,          // limit(x, lo, hi) = min(max(x, lo), hi)
    PWL,            // PWL(x, x1,y1, x2,y2, ...) — piecewise linear
};

struct ASTNode {
    NodeType type;
    double   value = 0.0;      // for CONSTANT
    int      var_idx = -1;     // for VARIABLE — index into var_refs
    std::unique_ptr<ASTNode> left;    // child 0 / operand
    std::unique_ptr<ASTNode> mid;     // child 1 (for ternary)
    std::unique_ptr<ASTNode> right;   // child 1 or 2
    std::vector<std::pair<double,double>> pwl_points;  // for PWL node
};

// ---------------------------------------------------------------------------
// Compiled expression — owns the AST and variable table
// ---------------------------------------------------------------------------
class CompiledExpression {
public:
    /// Parse an expression string and compile into an AST.
    /// Variable references (V(), I()) are collected automatically.
    static CompiledExpression compile(const std::string& expr);

    /// Evaluate the expression given variable values.
    /// `var_values` must be sized to match num_vars().
    /// Returns function value; `derivs` is resized and filled with df/dxi.
    double evaluate(const std::vector<double>& var_values,
                    std::vector<double>& derivs) const;

    /// Evaluate without derivatives (for convergence test etc.)
    double evaluate(const std::vector<double>& var_values) const;

    /// Variable references discovered during parsing.
    const std::vector<VarRef>& var_refs() const { return var_refs_; }
    int num_vars() const { return static_cast<int>(var_refs_.size()); }

    /// String representation for debug.
    const std::string& source() const { return source_; }

    // Default move, no copy
    CompiledExpression() = default;
    CompiledExpression(CompiledExpression&&) = default;
    CompiledExpression& operator=(CompiledExpression&&) = default;

private:
    std::unique_ptr<ASTNode> root_;
    std::vector<VarRef> var_refs_;
    std::string source_;

    // Recursive evaluator with forward-mode AD
    struct DualNumber {
        double val;
        std::vector<double> grad;  // df/dxi for each variable
    };
    DualNumber eval_node(const ASTNode* node,
                         const std::vector<double>& var_values,
                         int num_vars, bool need_grad) const;
};

// ---------------------------------------------------------------------------
// Expression parser (exposed for testing; normally use CompiledExpression::compile)
// ---------------------------------------------------------------------------
class ExpressionParser {
public:
    ExpressionParser(const std::string& expr,
                     std::vector<VarRef>& var_refs);

    std::unique_ptr<ASTNode> parse();

private:
    const std::string& expr_;
    std::vector<VarRef>& var_refs_;
    size_t pos_ = 0;

    // Variable dedup: map from canonical key to var_refs index
    std::unordered_map<std::string, int> var_map_;

    void skip_ws();
    char peek();
    char advance();
    bool match(char c);
    bool match(const std::string& s);

    int get_or_add_var(const VarRef& ref);
    static std::string var_key(const VarRef& ref);

    // Grammar: additive > multiplicative > power > unary > primary
    std::unique_ptr<ASTNode> parse_additive();
    std::unique_ptr<ASTNode> parse_multiplicative();
    std::unique_ptr<ASTNode> parse_power();
    std::unique_ptr<ASTNode> parse_unary();
    std::unique_ptr<ASTNode> parse_primary();
    std::unique_ptr<ASTNode> parse_function(const std::string& name);

    // Parse a V() or I() reference
    std::unique_ptr<ASTNode> parse_voltage_ref();
    std::unique_ptr<ASTNode> parse_current_ref();

    // Parse numeric literal
    std::unique_ptr<ASTNode> parse_number();
};

} // namespace asrc
} // namespace neospice
