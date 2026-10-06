#include "api/neospice.hpp"
#include "parser/tokenizer.hpp"
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <cmath>

using emscripten::val;
using namespace neospice;
namespace {
template<class F> val checked(F work) {
    auto envelope=val::object();
    try { envelope.set("value",work());envelope.set("ok",true); }
    catch(const std::exception& error) { envelope.set("ok",false);envelope.set("error",std::string(error.what())); }
    return envelope;
}
val js(double value) { return val(value); }
val js(const std::string& value) { return val(value); }
val js(std::complex<double> value) {
    auto result=val::object();result.set("real",value.real());result.set("imag",value.imag());return result;
}
template<class T> val js(const std::vector<T>& values) {
    auto result=val::array();
    for(std::size_t i=0;i<values.size();++i) result.set(i,js(values[i]));
    return result;
}
template<class T> val js(const std::map<std::string,T>& values) {
    auto result=val::object();for(const auto& [name,value]:values) result.set(name,js(value));return result;
}
val status(const SimStatus& s) {
    auto value=val::object();value.set("converged",s.converged);value.set("iterations",s.iterations);
    value.set("elapsedSeconds",s.elapsed_seconds);value.set("warnings",js(s.warnings));return value;
}
template<class T> std::vector<T> read_array(const val& values) {
    if (!val::global("Array").call<bool>("isArray",values)) throw std::invalid_argument("Expected an array");
    std::vector<T> result;
    unsigned size=values["length"].as<unsigned>();result.reserve(size);
    for(unsigned i=0;i<size;++i) result.push_back(values[i].as<T>());
    return result;
}
class BrowserCircuit {
    Simulator simulator;
    std::unique_ptr<Circuit> circuit;
    Circuit& get() {
        if (!circuit) throw std::logic_error("Load a circuit first");
        return *circuit;
    }
public:
    val load(const std::string& text) { return checked([&] {
        for (const auto& line:tokenize(text)) {
            if (line.tokens.empty()) continue;
            auto directive=line.tokens.front();
            for(auto& c:directive)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(directive==".include" || directive==".inc" || directive==".lib")
                throw std::invalid_argument("Browser netlists must inline included model/subcircuit definitions");
        }
        auto parsed=simulator.parse(text);
        if(!parsed.step_commands.empty()) throw std::invalid_argument("Browser .step sweeps are not supported");
        circuit=std::make_unique<Circuit>(std::move(parsed));
        return val::undefined();
    }); }
    val update(const std::string& device,double value) {return checked([&]{get().update_param(device,value);return val::undefined();});}
    val temperature(double celsius) {return checked([&]{
        if(!std::isfinite(celsius)||celsius<=-273.15)throw std::invalid_argument("Invalid Celsius temperature");
        get().options.temp=celsius+273.15;return val::undefined();
    });}
    val dc() {return checked([&]{
        auto result=simulator.re_solve(get());auto value=val::object();
        value.set("voltages",js(result.node_voltages));value.set("currents",js(result.branch_currents));
        value.set("status",status(result.status));return value;
    });}
    val ac(const std::string& mode,int points,double start,double stop) {return checked([&]{
        if(points<=0 || !std::isfinite(start)||!std::isfinite(stop)||start<=0||stop<start)
            throw std::invalid_argument("Invalid AC frequency grid");
        ACMode sweep;
        if(mode=="dec")sweep=ACMode::DEC;else if(mode=="oct")sweep=ACMode::OCT;else if(mode=="lin")sweep=ACMode::LIN;
        else throw std::invalid_argument("AC mode must be dec, oct, or lin");
        auto result=simulator.re_solve_ac(get(),sweep,points,start,stop);auto value=val::object();
        value.set("frequency",js(result.frequency));value.set("voltages",js(result.voltages));
        value.set("currents",js(result.currents));value.set("status",status(result.status));return value;
    });}
    val transient(double step,double stop) {return checked([&]{
        if(!std::isfinite(step)||!std::isfinite(stop)||step<=0||stop<=0)throw std::invalid_argument("Invalid transient time window");
        auto result=simulator.run_transient(get(),step,stop);auto value=val::object();
        value.set("time",js(result.time));value.set("voltages",js(result.voltages));
        value.set("currents",js(result.currents));value.set("status",status(result.status));return value;
    });}
    val sensitivity(const val& outputs,const val& parameters) {return checked([&]{
        auto result=simulator.sensitivity(get(),read_array<std::string>(outputs),read_array<std::string>(parameters));
        auto value=val::object();value.set("outputs",js(result.outputs));value.set("parameters",js(result.parameters));
        value.set("values",js(result.values));value.set("jacobian",js(result.jacobian));
        value.set("adjointSolves",static_cast<double>(result.adjoint_solves));value.set("status",status(result.status));return value;
    });}
    val sensitivity_ac(const val& outputs,const val& parameters,const val& frequencies) {return checked([&]{
        auto result=simulator.sensitivity_ac(get(),read_array<std::string>(outputs),read_array<std::string>(parameters),read_array<double>(frequencies));
        auto value=val::object();value.set("outputs",js(result.outputs));value.set("parameters",js(result.parameters));
        value.set("frequency",js(result.frequency));value.set("values",js(result.values));value.set("jacobian",js(result.jacobian));
        value.set("adjointSolves",static_cast<double>(result.adjoint_solves));value.set("status",status(result.status));return value;
    });}
    val statistics() {return checked([&]{
        auto stats=get().reuse_statistics();auto value=val::object();
        value.set("dcSymbolicAnalyses",static_cast<double>(stats.dc_symbolic_analyses));
        value.set("acSymbolicAnalyses",static_cast<double>(stats.ac_symbolic_analyses));
        value.set("dcRuns",static_cast<double>(stats.dc_runs));value.set("acRuns",static_cast<double>(stats.ac_runs));return value;
    });}
};
}
EMSCRIPTEN_BINDINGS(neospice_browser) {
    emscripten::class_<BrowserCircuit>("NativeCircuit").constructor<>()
        .function("load",&BrowserCircuit::load).function("update",&BrowserCircuit::update)
        .function("temperature",&BrowserCircuit::temperature).function("dc",&BrowserCircuit::dc)
        .function("ac",&BrowserCircuit::ac).function("transient",&BrowserCircuit::transient)
        .function("sensitivity",&BrowserCircuit::sensitivity).function("sensitivityAC",&BrowserCircuit::sensitivity_ac)
        .function("statistics",&BrowserCircuit::statistics);
}
