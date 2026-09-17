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
//                       sgn u ustep stp uramp ceil floor nint pwr pwrs
//                       limit if ddt idt db pwl table
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
    ATAN2, POW_FN, MIN, MAX, PWR, PWRS,
    // Relational operators (return 1.0 for true, 0.0 for false)
    GT, GE, LT, LE, EQ, NE,
    // Logical operators (treat nonzero as true)
    LOGICAL_AND, LOGICAL_OR, LOGICAL_XOR,
    LOGICAL_NOT,  // unary
    // Ternary
    IF_FN,          // if(cond, then, else) — cond != 0 selects then
    LIMIT,          // limit(x, lo, hi) = min(max(x, lo), hi)
    PWL,            // PWL(x, x1,y1, x2,y2, ...) — piecewise linear
    DB,             // DB(x) = 20 * log10(|x|) — decibel
    // Time-domain functions (stateful)
    DDT,            // DDT(expr) — ngspice transient history derivative
    IDT,            // IDT(expr [, ic]) — time integral via trapezoidal accumulation
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

    /// Set the current timestep for IDT evaluation.
    void set_dt(double dt) const { current_dt_ = dt; }

    /// Supply analysis time and mode for ngspice-compatible DDT evaluation.
    void set_time(double time, bool transient) const {
        current_time_ = time;
        transient_ = transient;
        if (time == 0.0) ddt_history_.clear();
    }

    /// Set the device gmin used by ngspice's parse-tree PTfudge_factor.
    void set_gmin(double gmin) const { current_gmin_ = gmin; }

    /// Accept the current IDT integral values after an accepted timestep.
    /// Commits the tentative accumulator and previous-argument state.
    void accept_idt() const {
        idt_committed_ = idt_accumulators_;
        idt_committed_prev_arg_ = idt_prev_arg_values_;
        idt_committed_has_prev_ = idt_has_prev_arg_;
    }

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

    // DDT samples the first evaluation at each increasing transient time,
    // rather than committing at accepted steps (ngspice parser/PTddt).
    struct DdtHistory {
        int samples = 0;
        double time = 0.0;
        double value = 0.0;
        double previous_time = 0.0;
        double derivative = 0.0;
    };
    mutable double current_dt_ = 0.0;
    mutable double current_time_ = 0.0;
    mutable bool transient_ = false;
    mutable double current_gmin_ = 1e-12;
    mutable std::unordered_map<const ASTNode*, DdtHistory> ddt_history_;

    // IDT state — tentative and committed buffers
    mutable std::vector<double> idt_accumulators_;       // tentative integral values
    mutable std::vector<double> idt_prev_arg_values_;    // tentative previous argument values
    mutable std::vector<bool>   idt_has_prev_arg_;       // tentative has-previous flags
    mutable std::vector<double> idt_committed_;           // committed integral values (after accept)
    mutable std::vector<double> idt_committed_prev_arg_; // committed previous argument values
    mutable std::vector<bool>   idt_committed_has_prev_; // committed has-previous flags
    mutable std::vector<bool>   idt_initialized_;         // whether accumulator was initialized with IC
    mutable int idt_eval_idx_ = 0;                        // counter reset each evaluate call
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
    std::string expr_;
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

    // Grammar: logical_or > logical_xor > logical_and > equality > relational
    //        > additive > multiplicative > power > unary > primary
    std::unique_ptr<ASTNode> parse_logical_or();
    std::unique_ptr<ASTNode> parse_logical_xor();
    std::unique_ptr<ASTNode> parse_logical_and();
    std::unique_ptr<ASTNode> parse_equality();
    std::unique_ptr<ASTNode> parse_relational();
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
