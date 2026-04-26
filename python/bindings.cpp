#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
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
}
