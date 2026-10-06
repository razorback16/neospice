#include "core/gradient.hpp"
#include "core/dc.hpp"
#include "core/neo_solver.hpp"
#include "core/ckt_mode.hpp"
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <numbers>
#include <set>

namespace neospice {
namespace {
using Complex = std::complex<double>;
std::string canonical(std::string text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); }), text.end());
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}
struct Output { std::string name; int positive = -1, negative = -1; };
std::vector<Output> parse_outputs(Circuit& ckt, const std::vector<std::string>& names) {
    if (names.empty()) throw std::invalid_argument("Sensitivity requires an output");
    std::vector<Output> outputs;
    const auto node = [&](const std::string& name) {
        if (name == "0" || name == "gnd") return -1;
        for (int i = 0; i < ckt.num_nodes(); ++i)
            if (canonical(ckt.node_name(i)) == name) return i;
        throw std::invalid_argument("Unknown output node: " + name);
    };
    for (const auto& name : names) {
        auto text = canonical(name);
        if (text.size() < 4 || text[1] != '(' || text.back() != ')')
            throw std::invalid_argument("Invalid sensitivity output: " + name);
        auto inner = text.substr(2, text.size()-3);
        Output out{text};
        if (text[0] == 'v') {
            auto comma = inner.find(',');
            out.positive = node(inner.substr(0,comma));
            if (comma != std::string::npos) out.negative = node(inner.substr(comma+1));
        } else if (text[0] == 'i') {
            auto* device = ckt.find_device_ptr(inner);
            if (!device || device->branch_index() < 0)
                throw std::invalid_argument("Sensitivity current output requires an MNA branch: " + name);
            out.positive = device->branch_index();
        } else throw std::invalid_argument("Invalid sensitivity output: " + name);
        outputs.push_back(out);
    }
    return outputs;
}
struct Parameter { std::string name, property; Device* device; };
std::vector<Parameter> parse_parameters(Circuit& ckt, std::vector<std::string> names, bool ac) {
    if (names.empty()) {
        for (const auto& d : ckt.devices())
            if (dynamic_cast<Resistor*>(d.get()) || dynamic_cast<Capacitor*>(d.get()) ||
                dynamic_cast<Inductor*>(d.get()) || dynamic_cast<VSource*>(d.get()) || dynamic_cast<ISource*>(d.get()))
                names.push_back(d->name());
    }
    std::vector<Parameter> parameters;
    std::set<std::string> seen;
    for (auto name : names) {
        name = canonical(name);
        auto colon = name.find(':');
        auto device_name = name.substr(0,colon);
        auto property = colon == std::string::npos ? "" : name.substr(colon+1);
        auto* device = ckt.find_device_ptr(device_name);
        if (!device) throw std::invalid_argument("Unknown sensitivity parameter: " + name);
        std::string expected;
        if (dynamic_cast<Resistor*>(device)) expected = "resistance";
        else if (dynamic_cast<Capacitor*>(device)) expected = "capacitance";
        else if (auto* l = dynamic_cast<Inductor*>(device)) {
            expected = "inductance";
            if (ac && l->is_coupled()) throw std::invalid_argument("Coupled-inductor AC gradients are unsupported: " + name);
        } else if (dynamic_cast<VSource*>(device) || dynamic_cast<ISource*>(device)) expected = ac ? "ac_mag" : "dc";
        else throw std::invalid_argument("Unsupported sensitivity parameter: " + name);
        if (property.empty()) property = expected;
        bool source_property = ac && expected == "ac_mag" && (property == "dc" || property == "ac_phase");
        if (property != expected && !source_property)
            throw std::invalid_argument("Unsupported sensitivity property: " + name);
        auto key = device_name + ":" + property;
        if (!seen.insert(key).second) throw std::invalid_argument("Duplicate sensitivity parameter: " + key);
        parameters.push_back({key, property, device});
    }
    return parameters;
}

template<class T> T at(const std::vector<T>& x, int i) { return i < 0 ? T{} : x.at(i); }
template<class T> T difference(const std::vector<T>& x, const Device& device) {
    auto nodes = device.external_nodes();
    return at(x,nodes.at(0))-at(x,nodes.at(1));
}
template<class T> void finite(const T& value) {
    if (!std::isfinite(std::abs(value))) throw std::runtime_error("Nonfinite sensitivity value or derivative");
}
double ratio(double effective, double nominal, const std::string& name) {
    if (!std::isfinite(effective) || !std::isfinite(nominal) || nominal == 0)
        throw std::invalid_argument("Sensitivity requires a finite nonzero nominal value: " + name);
    return effective / nominal;
}

struct ContextGuard {
    Circuit& ckt;
    IntegratorCtx saved;
    const IntegratorCtx* previous = tls_integrator_ctx;
    OneBasedEvalArrays* arrays = tls_one_based_eval_arrays;
    explicit ContextGuard(Circuit& circuit) : ckt(circuit), saved(circuit.integrator_ctx) {}
    ~ContextGuard() {
        ckt.integrator_ctx = saved;
        tls_integrator_ctx = previous;
        tls_one_based_eval_arrays = arrays;
        ckt.clear_operating_point(); // device evaluation updated state
    }
};

double dc_derivative(const Parameter& parameter, const std::vector<double>& x,
                     const std::vector<double>& adjoint, double source_scale) {
    auto& device = *parameter.device;
    if (auto* r = dynamic_cast<Resistor*>(&device)) {
        auto coefficient = ratio(r->resistance(), r->resistance_nom(), parameter.name);
        if (r->resistance() == 0) throw std::invalid_argument("Zero effective resistance");
        return difference(adjoint,device)*difference(x,device)*coefficient / (r->resistance()*r->resistance());
    }
    if (auto* source = dynamic_cast<VSource*>(&device)) {
        if (!source->dc_given() && source->source_function() != SourceFunction::DC) return 0;
        return at(adjoint,device.branch_index())*source_scale;
    }
    if (auto* source = dynamic_cast<ISource*>(&device)) {
        if (!source->dc_given() && source->source_function() != SourceFunction::DC) return 0;
        return -difference(adjoint,device)*source_scale;
    }
    return 0; // C and L values do not change DC equations
}

Complex ac_derivative(const Parameter& parameter, const std::vector<Complex>& x,
                      const std::vector<Complex>& adjoint, double omega) {
    auto& device = *parameter.device;
    if (auto* r = dynamic_cast<Resistor*>(&device)) {
        if (r->rac() > 0) return 0;
        double coefficient = ratio(r->resistance(),r->resistance_nom(),parameter.name);
        if (r->resistance() == 0) throw std::invalid_argument("Zero effective resistance");
        return difference(adjoint,device)*difference(x,device)*coefficient/(r->resistance()*r->resistance());
    }
    if (auto* c = dynamic_cast<Capacitor*>(&device))
        return -Complex(0,omega)*ratio(c->capacitance(),c->capacitance_nom(),parameter.name)*difference(adjoint,device)*difference(x,device);
    if (auto* l = dynamic_cast<Inductor*>(&device))
        return Complex(0,omega)*ratio(l->inductance(),l->inductance_nom(),parameter.name)*at(adjoint,l->branch_index())*at(x,l->branch_index());
    if (parameter.property == "dc") return 0;
    double magnitude, phase;
    Complex transfer;
    if (auto* v = dynamic_cast<VSource*>(&device)) {
        magnitude = v->ac_mag(); phase = v->ac_phase_rad(); transfer = at(adjoint,v->branch_index());
    } else {
        auto* i = dynamic_cast<ISource*>(&device);
        magnitude = i->ac_mag(); phase = i->ac_phase_rad(); transfer = -difference(adjoint,device);
    }
    Complex derivative = std::polar(1.0,phase);
    if (parameter.property == "ac_phase") derivative *= Complex(0,magnitude*std::numbers::pi/180.0);
    return transfer*derivative;
}

template<class Result, class Work>
Result execute_gradient(Circuit& ckt, Result result, Work work) {
    const auto start = std::chrono::steady_clock::now();
    ContextGuard guard(ckt);
    try { work(result); }
    catch (const std::invalid_argument&) { throw; }
    catch (const SimulationError& e) {
        result.status = e.status(); result.status.converged = false;
        result.status.warnings.push_back(e.what());
        if (!ckt.options.no_throw) throw;
        result.values.clear(); result.jacobian.clear();
    }
    catch (const std::runtime_error& e) {
        result.status.converged = false;
        result.status.warnings.push_back(e.what());
        if (!ckt.options.no_throw) throw SimulationError(e.what(),result.status);
        result.values.clear(); result.jacobian.clear();
    }
    result.status.elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    return result;
}
} // namespace

GradientResult solve_gradient(Circuit& ckt, const std::vector<std::string>& output_names,
                              const std::vector<std::string>& parameter_names) {
    auto outputs = parse_outputs(ckt,output_names);
    auto parameters = parse_parameters(ckt,parameter_names,false);
    GradientResult initial;
    for (const auto& o:outputs) initial.outputs.push_back(o.name);
    for (const auto& p:parameters) initial.parameters.push_back(p.name);
    return execute_gradient(ckt,std::move(initial),[&](GradientResult& result) {
        auto dc = solve_dc(ckt);
        result.status = dc.status;
        if (!dc.status.converged) throw SimulationError("Sensitivity operating point failed",dc.status);
        const auto x = *ckt.operating_point();
        const auto& pattern = ckt.pattern();
        NumericMatrix matrix(pattern);
        const int n = ckt.num_vars();
        std::vector<double> rhs(n+1,0), one_based_x(n+1,0);
        std::copy(x.begin(),x.end(),one_based_x.begin()+1);
        OneBasedEvalArrays arrays{one_based_x.data(),rhs.data(),n+1};
        ckt.integrator_ctx.mode = MODEDCOP_BIT|MODEINITFLOAT_BIT;
        ckt.integrator_ctx.options = &ckt.options;
        tls_integrator_ctx = &ckt.integrator_ctx;
        tls_one_based_eval_arrays = &arrays;
        for (auto* device : ckt.device_load_order())
            device->evaluate(x,matrix,std::span<double>(rhs.data()+1,n));
        tls_one_based_eval_arrays = nullptr;
        for (int i : ckt.dead_node_indices()) matrix.add(pattern.offset(i,i),ckt.options.gmin>0?ckt.options.gmin:1e-12);
        NeoSolver solver;
        solver.symbolic(pattern);
        if (solver.numeric(pattern,matrix,ckt.options.gshunt)) throw std::runtime_error("Singular sensitivity Jacobian");
        for (const auto& output : outputs) {
            std::vector<double> adjoint(n,0);
            if (output.positive >= 0) adjoint[output.positive] += 1;
            if (output.negative >= 0) adjoint[output.negative] -= 1;
            solver.solve_transposed(adjoint);
            ++result.adjoint_solves;
            double value = at(x,output.positive)-at(x,output.negative);
            finite(value); result.values.push_back(value);
            std::vector<double> row;
            for (const auto& parameter : parameters) {
                double derivative = dc_derivative(parameter,x,adjoint,ckt.options.src_fact);
                finite(derivative); row.push_back(derivative);
            }
            result.jacobian.push_back(std::move(row));
        }
    });
}

ACGradientResult solve_ac_gradient(Circuit& ckt, const std::vector<std::string>& output_names,
    const std::vector<std::string>& parameter_names, const std::vector<double>& frequencies) {
    if (!ckt.is_linear()) throw std::invalid_argument("AC adjoint gradients currently require a linear circuit");
    if (frequencies.empty()) throw std::invalid_argument("AC sensitivity requires frequencies");
    for (double frequency : frequencies)
        if (!std::isfinite(frequency) || frequency <= 0) throw std::invalid_argument("AC sensitivity frequencies must be finite and positive");
    auto outputs = parse_outputs(ckt,output_names);
    auto parameters = parse_parameters(ckt,parameter_names,true);
    ACGradientResult initial; initial.frequency = frequencies;
    for (const auto& o:outputs) initial.outputs.push_back(o.name);
    for (const auto& p:parameters) initial.parameters.push_back(p.name);
    return execute_gradient(ckt,std::move(initial),[&](ACGradientResult& result) {
        auto dc = solve_dc(ckt);
        result.status = dc.status;
        if (!dc.status.converged) throw SimulationError("AC sensitivity operating point failed",dc.status);
        auto xdc = *ckt.operating_point();
        auto& pattern = ckt.pattern();
        NumericMatrix conductance(pattern), capacitance(pattern);
        for (const auto& device : ckt.devices()) device->ac_stamp(xdc,conductance,capacitance);
        const auto n = ckt.num_vars();
        std::vector<Complex> excitation(n);
        for (const auto& device : ckt.devices()) device->apply_ac_excitation(excitation,n);
        NeoSolver solver; solver.symbolic(pattern);
        for (double frequency : frequencies) {
            double omega = 2*std::numbers::pi*frequency;
            ckt.integrator_ctx.ac_freq = frequency;
            std::vector<double> matrix(2*pattern.nnz());
            for (int i=0;i<pattern.nnz();++i) { matrix[2*i]=conductance.data()[i]; matrix[2*i+1]=omega*capacitance.data()[i]; }
            auto source = excitation;
            for (const auto& device : ckt.devices()) device->ac_stamp_freq(omega,matrix,pattern.nnz(),source);
            solver.numeric_complex(pattern,matrix);
            std::vector<double> rhs(2*n);
            for (int i=0;i<n;++i) { rhs[2*i]=source[i].real(); rhs[2*i+1]=source[i].imag(); }
            solver.solve_complex(rhs);
            std::vector<Complex> x(n);
            for (int i=0;i<n;++i) x[i]={rhs[2*i],rhs[2*i+1]};
            std::vector<Complex> values;
            std::vector<std::vector<Complex>> rows;
            for (const auto& output : outputs) {
                std::fill(rhs.begin(),rhs.end(),0);
                if (output.positive>=0) rhs[2*output.positive]+=1;
                if (output.negative>=0) rhs[2*output.negative]-=1;
                solver.solve_complex_transposed(rhs); ++result.adjoint_solves;
                std::vector<Complex> adjoint(n);
                for (int i=0;i<n;++i) adjoint[i]={rhs[2*i],rhs[2*i+1]};
                auto value=at(x,output.positive)-at(x,output.negative);
                finite(value); values.push_back(value);
                std::vector<Complex> row;
                for (const auto& parameter : parameters) {
                    auto derivative=ac_derivative(parameter,x,adjoint,omega);
                    finite(derivative); row.push_back(derivative);
                }
                rows.push_back(std::move(row));
            }
            result.values.push_back(std::move(values)); result.jacobian.push_back(std::move(rows));
        }
    });
}
} // namespace neospice
