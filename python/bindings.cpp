#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include "api/neospice.hpp"
#include "api/circuit_builder.hpp"

namespace nb = nanobind;
using namespace neospice;

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
}
