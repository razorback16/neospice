// Original TLV3201 circuit and edge/DC-port qualification, with paired phases.
#include "bench/tlv3201_validation.hpp"
using namespace neospice::bench::driver;

const std::vector<Workload> workloads = {
    {"tran_tlv3201", "tlv3201_switching.cir", "tran 100n 30u", Workload::TRAN, 0, 100e-9, 30e-6},
};

int main(int argc, char** argv) { return run(workloads, argc, argv, Tlv3201Validation{}); }
