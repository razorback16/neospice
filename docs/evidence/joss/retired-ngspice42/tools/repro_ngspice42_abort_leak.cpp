// Standalone libngspice failure-path reproducer; no neospice code is linked.
// Build/run instructions and observed results: docs/ngspice42-abort-leak.md.
#include <ngspice/sharedspice.h>

static int message(char*, int, void*) { return 0; }
static int status(char*, int, void*) { return 0; }
static int finish(int, bool, bool, int, void*) { return 0; }

int main() {
    ngSpice_Init(message, status, finish, nullptr, nullptr, nullptr, nullptr);
    char title[] = "Singular reference";
    char v1[] = "V1 out 0 1";
    char v2[] = "V2 out 0 2";
    char op[] = ".op";
    char end[] = ".end";
    char* lines[] = {title, v1, v2, op, end, nullptr};
    ngSpice_Circ(lines);
    char run[] = "run";
    char plots[] = "destroy all";
    char circuit[] = "remcirc";
    ngSpice_Command(run);
    ngSpice_Command(plots);
    ngSpice_Command(circuit);
}
