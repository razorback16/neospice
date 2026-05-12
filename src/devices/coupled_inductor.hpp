#pragma once
#include "devices/device.hpp"
#include "devices/inductor.hpp"

namespace neospice {

/// Coupled inductor (K element): mutual inductance between two inductors.
///
/// SPICE syntax: Kname L1 L2 coupling_coefficient
///
/// MNA: The K element adds cross-coupling terms in the branch equations
/// of the two referenced inductors.  In the frequency domain:
///   L1 branch equation: ... - jωM * I_L2 = 0
///   L2 branch equation: ... - jωM * I_L1 = 0
/// where M = k * sqrt(L1 * L2).
///
/// In the time domain (transient companion model), the K element adds:
///   mat[br1, br2] += -R_eq_m      rhs[br1] -= R_eq_m * I2_prev
///   mat[br2, br1] += -R_eq_m      rhs[br2] -= R_eq_m * I1_prev
/// where R_eq_m = 2*M/dt (trapezoidal) or 1.5*M/dt (Gear-2).
class CoupledInductor : public Device {
public:
    CoupledInductor(std::string name, Inductor* l1, Inductor* l2, double coupling);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    // Transient support — called by the transient solver alongside Inductor
    void set_transient(double dt);
    void clear_transient();
    void set_integration_method(int method);  // 0=trap, 1=gear2
    void set_integrator_order(int order) { integrator_order_ = order; }
    void init_dc_state(const std::vector<double>& sol);
    void accept_step_from_solution(const std::vector<double>& sol);

    double coupling() const { return coupling_; }
    double mutual_inductance() const { return mutual_; }
    Inductor* inductor1() const { return l1_; }
    Inductor* inductor2() const { return l2_; }

private:
    Inductor* l1_;
    Inductor* l2_;
    double coupling_;  // k
    double mutual_;    // M = k * sqrt(L1 * L2)

    // Transient state
    bool transient_ = false;
    double dt_ = 0.0;
    double dt_prev_ = 0.0;       // timestep at previous accepted step
    int integration_method_ = 0;  // 0=trapezoidal, 1=gear2
    int integrator_order_ = 1;   // 1=backward Euler, 2=trapezoidal/gear2
    bool gear_ready_ = false;

    // Previous partner currents (for RHS history terms)
    double i1_prev_ = 0.0;  // L1's branch current at previous step
    double i2_prev_ = 0.0;  // L2's branch current at previous step
    double i1_prev2_ = 0.0; // L1's branch current two steps ago (for Gear-2)
    double i2_prev2_ = 0.0; // L2's branch current two steps ago (for Gear-2)

    MatrixOffset off_br1_br2_ = -1;  // (branch_L1, branch_L2)
    MatrixOffset off_br2_br1_ = -1;  // (branch_L2, branch_L1)
};

} // namespace neospice
