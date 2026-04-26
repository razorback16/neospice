#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/complex.h>
#include "api/neospice.hpp"
#include "api/circuit_builder.hpp"
#include <cstring>

namespace nb = nanobind;
using namespace neospice;

// Helper: copy std::vector<double> into a heap-allocated NumPy array
static nb::ndarray<nb::numpy, double, nb::shape<-1>>
make_owned_double_array(const std::vector<double>& vec) {
    size_t n = vec.size();
    double* data = new double[n];
    std::memcpy(data, vec.data(), n * sizeof(double));
    nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    return nb::ndarray<nb::numpy, double, nb::shape<-1>>(data, {n}, owner);
}

// Helper: copy std::vector<std::complex<double>> into a heap-allocated NumPy array
static nb::ndarray<nb::numpy, std::complex<double>, nb::shape<-1>>
make_owned_complex_array(const std::vector<std::complex<double>>& vec) {
    size_t n = vec.size();
    auto* data = new std::complex<double>[n];
    std::memcpy(data, vec.data(), n * sizeof(std::complex<double>));
    nb::capsule owner(data, [](void* p) noexcept {
        delete[] static_cast<std::complex<double>*>(p);
    });
    return nb::ndarray<nb::numpy, std::complex<double>, nb::shape<-1>>(data, {n}, owner);
}

NB_MODULE(_core, m) {
    m.doc() = "neospice: fast SPICE circuit simulator";

    // --- Enums ---
    nb::enum_<ACMode>(m, "ACMode")
        .value("DEC", ACMode::DEC)
        .value("OCT", ACMode::OCT)
        .value("LIN", ACMode::LIN);

    nb::enum_<IntegrationMethod>(m, "IntegrationMethod")
        .value("TRAPEZOIDAL", IntegrationMethod::TRAPEZOIDAL)
        .value("GEAR2", IntegrationMethod::GEAR2);

    nb::enum_<ConvergenceMethod>(m, "ConvergenceMethod")
        .value("DIRECT", ConvergenceMethod::DIRECT)
        .value("GMIN_STEPPING", ConvergenceMethod::GMIN_STEPPING)
        .value("SOURCE_STEPPING", ConvergenceMethod::SOURCE_STEPPING)
        .value("PSEUDO_TRANSIENT", ConvergenceMethod::PSEUDO_TRANSIENT);

    // --- SimStatus ---
    nb::class_<SimStatus>(m, "SimStatus")
        .def_ro("converged", &SimStatus::converged)
        .def_ro("iterations", &SimStatus::iterations)
        .def_ro("convergence_method", &SimStatus::convergence_method)
        .def_ro("warnings", &SimStatus::warnings)
        .def_ro("elapsed_seconds", &SimStatus::elapsed_seconds);

    // --- Options structs ---
    nb::class_<SimulatorOptions>(m, "SimulatorOptions")
        .def(nb::init<>())
        .def_rw("abstol", &SimulatorOptions::abstol)
        .def_rw("reltol", &SimulatorOptions::reltol)
        .def_rw("vntol", &SimulatorOptions::vntol)
        .def_rw("trtol", &SimulatorOptions::trtol)
        .def_rw("gmin", &SimulatorOptions::gmin);

    nb::class_<SourceSpec>(m, "SourceSpec")
        .def(nb::init<>())
        .def_rw("dc", &SourceSpec::dc)
        .def_rw("ac_mag", &SourceSpec::ac_mag)
        .def_rw("ac_phase", &SourceSpec::ac_phase);

    nb::class_<PulseSpec>(m, "PulseSpec")
        .def(nb::init<>())
        .def_rw("v1", &PulseSpec::v1)
        .def_rw("v2", &PulseSpec::v2)
        .def_rw("td", &PulseSpec::td)
        .def_rw("tr", &PulseSpec::tr)
        .def_rw("tf", &PulseSpec::tf)
        .def_rw("pw", &PulseSpec::pw)
        .def_rw("per", &PulseSpec::per);

    nb::class_<SinSpec>(m, "SinSpec")
        .def(nb::init<>())
        .def_rw("vo", &SinSpec::vo)
        .def_rw("va", &SinSpec::va)
        .def_rw("freq", &SinSpec::freq)
        .def_rw("td", &SinSpec::td)
        .def_rw("theta", &SinSpec::theta)
        .def_rw("phase", &SinSpec::phase);

    nb::class_<TransientOptions>(m, "TransientOptions")
        .def(nb::init<>())
        .def_rw("uic", &TransientOptions::uic);

    nb::class_<ACOptions>(m, "ACOptions")
        .def(nb::init<>());

    nb::class_<DCSweepParam>(m, "DCSweepParam")
        .def(nb::init<>())
        .def_rw("source_name", &DCSweepParam::source_name)
        .def_rw("start", &DCSweepParam::start)
        .def_rw("stop", &DCSweepParam::stop)
        .def_rw("step", &DCSweepParam::step);

    // --- Circuit (move-only) ---
    nb::class_<Circuit>(m, "Circuit")
        .def_ro("title", &Circuit::title)
        .def("node_names", &Circuit::node_names)
        .def("device_names", &Circuit::device_names)
        .def("device_info", &Circuit::device_info)
        .def("set_param", &Circuit::set_param);

    nb::class_<DeviceInfo>(m, "DeviceInfo")
        .def_ro("name", &DeviceInfo::name)
        .def_ro("type", &DeviceInfo::type)
        .def_ro("nodes", &DeviceInfo::nodes)
        .def_ro("value", &DeviceInfo::value);

    // --- Simulator ---
    nb::class_<Simulator>(m, "Simulator")
        .def(nb::init<>())
        .def(nb::init<SimulatorOptions>())
        .def("load", &Simulator::load)
        .def("parse", &Simulator::parse)
        .def("run_dc", &Simulator::run_dc)
        .def("run_transient",
             nb::overload_cast<Circuit&, double, double>(&Simulator::run_transient))
        .def("run_transient_with_opts",
             nb::overload_cast<Circuit&, double, double, const TransientOptions&>(
                 &Simulator::run_transient))
        .def("run_ac",
             nb::overload_cast<Circuit&, ACMode, int, double, double>(
                 &Simulator::run_ac))
        .def("run_ac_with_opts",
             nb::overload_cast<Circuit&, ACMode, int, double, double, const ACOptions&>(
                 &Simulator::run_ac))
        .def("run_noise", &Simulator::run_noise)
        .def("run_dc_sweep", &Simulator::run_dc_sweep)
        .def("run_tf", &Simulator::run_tf)
        .def("run_sens", &Simulator::run_sens)
        .def("run", &Simulator::run)
        .def("run_step_sweep", &Simulator::run_step_sweep);

    // --- CircuitBuilder (fluent API) ---
    auto cb = nb::class_<CircuitBuilder>(m, "CircuitBuilder");
    cb.def(nb::init<>());
    cb.def("title", &CircuitBuilder::title, nb::rv_policy::reference);
    cb.def("resistor", &CircuitBuilder::resistor, nb::rv_policy::reference);
    cb.def("capacitor", &CircuitBuilder::capacitor, nb::rv_policy::reference);
    cb.def("inductor", &CircuitBuilder::inductor, nb::rv_policy::reference);
    cb.def("vsource", &CircuitBuilder::vsource, nb::rv_policy::reference);
    cb.def("vsource_pulse", &CircuitBuilder::vsource_pulse, nb::rv_policy::reference);
    cb.def("vsource_sin", &CircuitBuilder::vsource_sin, nb::rv_policy::reference);
    cb.def("isource", &CircuitBuilder::isource, nb::rv_policy::reference);
    cb.def("diode", &CircuitBuilder::diode, nb::rv_policy::reference);
    cb.def("subcircuit", &CircuitBuilder::subcircuit, nb::rv_policy::reference);
    cb.def("model", &CircuitBuilder::model, nb::rv_policy::reference);
    cb.def("include", &CircuitBuilder::include, nb::rv_policy::reference);
    cb.def("raw_line", &CircuitBuilder::raw_line, nb::rv_policy::reference);
    cb.def("build", &CircuitBuilder::build);

    // --- DCResult ---
    nb::class_<DCResult>(m, "DCResult")
        .def("voltage", [](DCResult& self, const std::string& node) -> double {
            try { return self.voltage(node); }
            catch (const std::out_of_range&) {
                throw nb::key_error(node.c_str());
            }
        })
        .def("current", [](DCResult& self, const std::string& dev) -> double {
            try { return self.current(dev); }
            catch (const std::out_of_range&) {
                throw nb::key_error(dev.c_str());
            }
        })
        .def("diff", [](DCResult& self, const std::string& np, const std::string& nn) {
            try { return self.diff(np, nn); }
            catch (const std::out_of_range& e) {
                throw nb::key_error(e.what());
            }
        })
        .def("signal_names", &DCResult::signal_names)
        .def_ro("status", &DCResult::status);

    // --- TransientResult ---
    nb::class_<TransientResult>(m, "TransientResult")
        .def_prop_ro("time", [](TransientResult& self) {
            return make_owned_double_array(self.time);
        }, nb::rv_policy::move)
        .def("voltage", [](TransientResult& self, const std::string& node) {
            try { return make_owned_double_array(self.voltage(node)); }
            catch (const std::out_of_range&) {
                throw nb::key_error(node.c_str());
            }
        })
        .def("current", [](TransientResult& self, const std::string& dev) {
            try { return make_owned_double_array(self.current(dev)); }
            catch (const std::out_of_range&) {
                throw nb::key_error(dev.c_str());
            }
        })
        .def("diff", [](TransientResult& self, const std::string& np, const std::string& nn) {
            try { return make_owned_double_array(self.diff(np, nn)); }
            catch (const std::out_of_range& e) {
                throw nb::key_error(e.what());
            }
        })
        .def("signal_names", &TransientResult::signal_names)
        .def_ro("rejected_steps", &TransientResult::rejected_steps)
        .def_ro("status", &TransientResult::status);

    // --- ACResult ---
    nb::class_<ACResult>(m, "ACResult")
        .def_prop_ro("frequency", [](ACResult& self) {
            return make_owned_double_array(self.frequency);
        }, nb::rv_policy::move)
        .def("voltage", [](ACResult& self, const std::string& node) {
            try { return make_owned_complex_array(self.voltage(node)); }
            catch (const std::out_of_range&) {
                throw nb::key_error(node.c_str());
            }
        })
        .def("current", [](ACResult& self, const std::string& dev) {
            try { return make_owned_complex_array(self.current(dev)); }
            catch (const std::out_of_range&) {
                throw nb::key_error(dev.c_str());
            }
        })
        .def("magnitude_db", [](ACResult& self, const std::string& node) {
            try { return make_owned_double_array(self.magnitude_db(node)); }
            catch (const std::out_of_range&) { throw nb::key_error(node.c_str()); }
        })
        .def("phase_deg", [](ACResult& self, const std::string& node) {
            try { return make_owned_double_array(self.phase_deg(node)); }
            catch (const std::out_of_range&) { throw nb::key_error(node.c_str()); }
        })
        .def("magnitude", [](ACResult& self, const std::string& node) {
            try { return make_owned_double_array(self.magnitude(node)); }
            catch (const std::out_of_range&) { throw nb::key_error(node.c_str()); }
        })
        .def("diff", [](ACResult& self, const std::string& np, const std::string& nn) {
            try { return make_owned_complex_array(self.diff(np, nn)); }
            catch (const std::out_of_range& e) {
                throw nb::key_error(e.what());
            }
        })
        .def("diff_magnitude_db", [](ACResult& self,
                const std::string& np, const std::string& nn) {
            try { return make_owned_double_array(self.diff_magnitude_db(np, nn)); }
            catch (const std::out_of_range& e) { throw nb::key_error(e.what()); }
        })
        .def("current_magnitude_db", [](ACResult& self, const std::string& dev) {
            try { return make_owned_double_array(self.current_magnitude_db(dev)); }
            catch (const std::out_of_range&) { throw nb::key_error(dev.c_str()); }
        })
        .def("current_phase_deg", [](ACResult& self, const std::string& dev) {
            try { return make_owned_double_array(self.current_phase_deg(dev)); }
            catch (const std::out_of_range&) { throw nb::key_error(dev.c_str()); }
        })
        .def("current_magnitude", [](ACResult& self, const std::string& dev) {
            try { return make_owned_double_array(self.current_magnitude(dev)); }
            catch (const std::out_of_range&) { throw nb::key_error(dev.c_str()); }
        })
        .def("signal_names", &ACResult::signal_names)
        .def_ro("status", &ACResult::status);

    // --- DCSweepResult ---
    nb::class_<DCSweepResult>(m, "DCSweepResult")
        .def_prop_ro("sweep_values", [](DCSweepResult& self) {
            return make_owned_double_array(self.sweep_values);
        }, nb::rv_policy::move)
        .def_ro("sweep_var", &DCSweepResult::sweep_var)
        .def("voltage", [](DCSweepResult& self, const std::string& node) {
            try { return make_owned_double_array(self.voltage(node)); }
            catch (const std::out_of_range&) {
                throw nb::key_error(node.c_str());
            }
        })
        .def("current", [](DCSweepResult& self, const std::string& dev) {
            try { return make_owned_double_array(self.current(dev)); }
            catch (const std::out_of_range&) {
                throw nb::key_error(dev.c_str());
            }
        })
        .def("diff", [](DCSweepResult& self,
                const std::string& np, const std::string& nn) {
            try { return make_owned_double_array(self.diff(np, nn)); }
            catch (const std::out_of_range& e) { throw nb::key_error(e.what()); }
        })
        .def("signal_names", &DCSweepResult::signal_names)
        .def_ro("status", &DCSweepResult::status);

    // --- NoiseResult ---
    nb::class_<NoiseResult>(m, "NoiseResult")
        .def_prop_ro("frequency", [](NoiseResult& self) {
            return make_owned_double_array(self.frequency);
        }, nb::rv_policy::move)
        .def_prop_ro("output_noise_density", [](NoiseResult& self) {
            return make_owned_double_array(self.output_noise_density);
        }, nb::rv_policy::move)
        .def_prop_ro("input_noise_density", [](NoiseResult& self) {
            return make_owned_double_array(self.input_noise_density);
        }, nb::rv_policy::move)
        .def("output_noise_sqrt", [](NoiseResult& self) {
            return make_owned_double_array(self.output_noise_sqrt());
        })
        .def("input_noise_sqrt", [](NoiseResult& self) {
            return make_owned_double_array(self.input_noise_sqrt());
        })
        .def("integrated_output_noise", &NoiseResult::integrated_output_noise)
        .def("integrated_input_noise", &NoiseResult::integrated_input_noise)
        .def("device_names", &NoiseResult::device_names)
        .def("device_noise_density", [](NoiseResult& self, const std::string& name) {
            try { return make_owned_double_array(self.device_noise_density(name)); }
            catch (const std::out_of_range&) {
                throw nb::key_error(name.c_str());
            }
        })
        .def("signal_names", &NoiseResult::signal_names)
        .def_ro("status", &NoiseResult::status);

    // --- TFResult ---
    nb::class_<TFResult>(m, "TFResult")
        .def_ro("output_var", &TFResult::output_var)
        .def_ro("input_src", &TFResult::input_src)
        .def_ro("transfer_function", &TFResult::transfer_function)
        .def_ro("input_impedance", &TFResult::input_impedance)
        .def_ro("output_impedance", &TFResult::output_impedance)
        .def_ro("status", &TFResult::status);

    // --- SensResult ---
    nb::class_<SensResult::Entry>(m, "SensEntry")
        .def_ro("element", &SensResult::Entry::element)
        .def_ro("parameter", &SensResult::Entry::parameter)
        .def_ro("sensitivity", &SensResult::Entry::sensitivity)
        .def_ro("normalized", &SensResult::Entry::normalized);

    nb::class_<SensResult>(m, "SensResult")
        .def_ro("output_var", &SensResult::output_var)
        .def_ro("output_value", &SensResult::output_value)
        .def_ro("entries", &SensResult::entries)
        .def_ro("status", &SensResult::status);

    // --- PZResult ---
    nb::class_<PZResult>(m, "PZResult")
        .def_prop_ro("poles", [](PZResult& self) {
            return make_owned_complex_array(self.poles);
        }, nb::rv_policy::move)
        .def_prop_ro("zeros", [](PZResult& self) {
            return make_owned_complex_array(self.zeros);
        }, nb::rv_policy::move)
        .def_ro("status", &PZResult::status);
}
