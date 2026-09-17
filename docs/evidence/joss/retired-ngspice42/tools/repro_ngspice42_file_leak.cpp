// Standalone libngspice lifecycle reproducer for a supplied netlist.
// No neospice engine or reference wrapper is linked. This diagnoses allocation
// lifetime, not result correctness (ngspice may return zero after an error).
#include <ngspice/sharedspice.h>
#include <cstdio>
#include <string>

static int message(char* text, int, void*) {
    if (text) std::fprintf(stderr, "%s\n", text);
    return 0;
}
static int status(char*, int, void*) { return 0; }
static int finish(int, bool, bool, int, void*) { return 0; }

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s netlist.cir\n", argv[0]);
        return 2;
    }
    ngSpice_Init(message, status, finish, nullptr, nullptr, nullptr, nullptr);
    std::string source = std::string("source ") + argv[1];
    ngSpice_Command(source.data());
    char run[] = "run", plots[] = "destroy all", circuit[] = "remcirc";
    ngSpice_Command(run);
    ngSpice_Command(plots);
    ngSpice_Command(circuit);
}
