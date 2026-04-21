"""Generate per-device test scaffolding."""
from __future__ import annotations


def generate_test_cmake(desc) -> str:
    ns = desc.neospice_name
    return f'''file(GLOB {ns.upper()}_TEST_SOURCES "*.cpp")

add_executable(test_{ns}_device ${{{ns.upper()}_TEST_SOURCES}})
target_link_libraries(test_{ns}_device PRIVATE neospice_lib ngspice_runner GTest::gtest_main)
target_compile_definitions(test_{ns}_device PRIVATE
    TEST_CIRCUITS_DIR="${{CMAKE_CURRENT_SOURCE_DIR}}/circuits"
    NGSPICE_BINARY="${{NGSPICE_BINARY}}")
target_include_directories(test_{ns}_device PRIVATE ${{CMAKE_SOURCE_DIR}}/src ${{CMAKE_SOURCE_DIR}}/tests)

add_test(NAME {ns}_device COMMAND test_{ns}_device)
'''


def generate_test_dc(desc) -> str:
    ns = desc.neospice_name
    prefix = desc.prefix
    class_name = f'{prefix}DCTest'
    return f'''#include <gtest/gtest.h>
#include "ngspice_runner.hpp"
#include "simulator.hpp"
#include "compare.hpp"

class {class_name} : public ::testing::Test {{
protected:
    void SetUp() override {{
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }}
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
}};

TEST_F({class_name}, BasicDC) {{
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/basic_dc.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {{5e-2, 1e-9}});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}}
'''


def generate_test_transient(desc) -> str:
    ns = desc.neospice_name
    prefix = desc.prefix
    class_name = f'{prefix}TransientTest'
    return f'''#include <gtest/gtest.h>
#include "ngspice_runner.hpp"
#include "simulator.hpp"
#include "compare.hpp"

class {class_name} : public ::testing::Test {{
protected:
    void SetUp() override {{
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }}
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
}};

TEST_F({class_name}, BasicTransient) {{
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/basic_transient.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    auto cmp = compare_transient(*cs_result.transient, ng_result, {{5e-2, 1e-3}});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}}
'''
