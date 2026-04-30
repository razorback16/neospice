#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <span>
#include "api/neospice.hpp"
#include "neospice/types.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"

using namespace neospice;

class IntrospectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string netlist = R"(
Introspection test
V1 in 0 10
R1 in mid 1k
R2 mid out 2k
C1 out 0 1n
.op
.end
)";
        Simulator sim;
        ckt = sim.parse(netlist);
    }
    Circuit ckt;
};

TEST_F(IntrospectionTest, NodeNames) {
    auto names = ckt.node_names();
    EXPECT_GE(names.size(), 3u);
    EXPECT_TRUE(std::find(names.begin(), names.end(), "in") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "mid") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "out") != names.end());
}

TEST_F(IntrospectionTest, DeviceNames) {
    auto names = ckt.device_names();
    EXPECT_EQ(names.size(), 4u);
}

TEST_F(IntrospectionTest, DeviceInfoResistor) {
    auto info = ckt.device_info("R1");
    EXPECT_EQ(info.type, "R");
    EXPECT_EQ(info.nodes.size(), 2u);
    EXPECT_TRUE(info.value.has_value());
    EXPECT_NEAR(info.value.value(), 1e3, 1e-6);
}

TEST_F(IntrospectionTest, DeviceInfoVSource) {
    auto info = ckt.device_info("V1");
    EXPECT_EQ(info.type, "V");
    EXPECT_EQ(info.nodes.size(), 2u);
    EXPECT_TRUE(info.value.has_value());
    EXPECT_NEAR(info.value.value(), 10.0, 1e-6);
}

TEST_F(IntrospectionTest, DeviceInfoCapacitor) {
    auto info = ckt.device_info("C1");
    EXPECT_EQ(info.type, "C");
    EXPECT_TRUE(info.value.has_value());
    EXPECT_NEAR(info.value.value(), 1e-9, 1e-15);
}

TEST_F(IntrospectionTest, DeviceInfoCaseInsensitive) {
    auto info = ckt.device_info("r1");
    EXPECT_EQ(info.type, "R");
}

TEST_F(IntrospectionTest, DeviceInfoNotFound) {
    EXPECT_THROW(ckt.device_info("nonexistent"), std::out_of_range);
}

TEST_F(IntrospectionTest, DevicesAtNode) {
    auto devs = ckt.devices_at_node("mid");
    EXPECT_EQ(devs.size(), 2u);

    auto devs_out = ckt.devices_at_node("out");
    EXPECT_EQ(devs_out.size(), 2u);
}

TEST_F(IntrospectionTest, DevicesAtGround) {
    auto devs = ckt.devices_at_node("0");
    EXPECT_GE(devs.size(), 2u);
}

TEST_F(IntrospectionTest, FindDevice) {
    auto* dev = ckt.find_device_ptr("R1");
    ASSERT_NE(dev, nullptr);
    EXPECT_EQ(dev->name(), "R1");

    EXPECT_EQ(ckt.find_device_ptr("nonexistent"), nullptr);
}

TEST(SetParam, ResistorValue) {
    Simulator sim;
    auto ckt = sim.parse(R"(
SetParam test
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)");
    auto dc1 = sim.run_dc(ckt);
    EXPECT_NEAR(dc1.voltage("out"), 5.0, 1e-6);

    EXPECT_TRUE(ckt.set_param("R2", 3e3));
    ckt.reset_state();
    auto dc2 = sim.run_dc(ckt);
    EXPECT_NEAR(dc2.voltage("out"), 7.5, 1e-6);
}

TEST(SetParam, VSourceValue) {
    Simulator sim;
    auto ckt = sim.parse(R"(
SetParam source
V1 in 0 10
R1 in 0 1k
.op
.end
)");
    auto dc1 = sim.run_dc(ckt);
    EXPECT_NEAR(dc1.voltage("in"), 10.0, 1e-6);

    EXPECT_TRUE(ckt.set_param("V1", 5.0));
    ckt.reset_state();
    auto dc2 = sim.run_dc(ckt);
    EXPECT_NEAR(dc2.voltage("in"), 5.0, 1e-6);
}

TEST(SetParam, CapacitorValue) {
    Simulator sim;
    auto ckt1 = sim.parse(R"(
SetParam cap
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)");
    auto ac1 = sim.run_ac(ckt1, ACMode::DEC, 10, 100, 10e6);

    auto ckt2 = sim.parse(R"(
SetParam cap2
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.end
)");
    EXPECT_TRUE(ckt2.set_param("C1", 10e-9));
    auto ac2 = sim.run_ac(ckt2, ACMode::DEC, 10, 100, 10e6);

    auto gain1 = ac1.magnitude_db("out");
    auto gain2 = ac2.magnitude_db("out");
    EXPECT_LT(gain2.back(), gain1.back());
}

TEST(SetParam, NonexistentDevice) {
    Simulator sim;
    auto ckt = sim.parse(R"(
SetParam none
V1 in 0 10
R1 in 0 1k
.op
.end
)");
    EXPECT_FALSE(ckt.set_param("R99", 1e3));
}

TEST(SetParam, InductorValue) {
    Simulator sim;
    auto ckt = sim.parse(R"(
SetParam inductor
V1 in 0 DC 0 AC 1
R1 in out 1k
L1 out 0 1m
.ac dec 10 100 10meg
.end
)");
    EXPECT_TRUE(ckt.set_param("L1", 10e-3));
    auto info = ckt.device_info("L1");
    EXPECT_NEAR(info.value.value(), 10e-3, 1e-9);
}

// ---------------------------------------------------------------------------
// Handle-based introspection tests (Task 13)
// ---------------------------------------------------------------------------

TEST(HandleIntrospection, FindNode) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    EXPECT_EQ(ckt.find_node("in"), in);
    EXPECT_EQ(ckt.find_node("out"), out);
    EXPECT_EQ(ckt.find_node("0"), GND);
    EXPECT_EQ(ckt.find_node("gnd"), GND);
}

TEST(HandleIntrospection, FindNodeNotFound) {
    Circuit ckt;
    ckt.node("in");
    EXPECT_THROW(ckt.find_node("nonexistent"), std::out_of_range);
}

TEST(HandleIntrospection, FindDeviceByHandle) {
    Circuit ckt;
    auto in = ckt.node("in");
    DevId v = ckt.V("V1", in, GND, 5.0);
    DevId r = ckt.R("R1", in, GND, 1e3);
    EXPECT_EQ(ckt.find_device("v1"), v);
    EXPECT_EQ(ckt.find_device("V1"), v);
    EXPECT_EQ(ckt.find_device("r1"), r);
}

TEST(HandleIntrospection, FindDeviceNotFoundByHandle) {
    Circuit ckt;
    auto in = ckt.node("in");
    ckt.V("V1", in, GND, 5.0);
    EXPECT_THROW(ckt.find_device("v99"), std::out_of_range);
}

TEST(HandleIntrospection, NodeName) {
    Circuit ckt;
    auto in = ckt.node("in");
    EXPECT_EQ(ckt.name(in), "in");
}

TEST(HandleIntrospection, DeviceName) {
    Circuit ckt;
    auto in = ckt.node("in");
    DevId v = ckt.V("V1", in, GND, 5.0);
    EXPECT_EQ(ckt.name(v), "V1");
}

TEST(HandleIntrospection, DeviceInfoByDevId) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 5.0);
    DevId r = ckt.R("R1", in, out, 1e3);
    ckt.finalize();
    auto info = ckt.device_info(r);
    EXPECT_EQ(info.name, "R1");
    EXPECT_EQ(info.type, "R");
}

// Test ModelMatcher registry (Task 11)
TEST(SimulatorRegistry, RegisterCustomDevice) {
    Simulator sim;
    bool factory_called = false;
    sim.register_device("U", {},
        [&](std::string_view, std::span<const int32_t>,
            const std::map<std::string, double>&) -> std::unique_ptr<Device> {
            factory_called = true;
            return nullptr;
        });
    EXPECT_FALSE(factory_called);  // Just registered, not called
}

// Test .save view layer (Task 12)
TEST(SaveFilter, HandleAccessWorksEvenWithSave) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Save test
V1 in 0 10
R1 in mid 1k
R2 mid out 1k
R3 out 0 1k
.save v(out)
.op
.end
)");
    auto result = sim.run(ckt);
    auto& dc = std::get<DCResult>(result.analysis);
    auto names = dc.signal_names();
    EXPECT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "v(out)");
    // Handle access works for ALL nodes regardless of .save
    auto mid_idx = ckt.node_index("mid");
    EXPECT_NEAR(dc.voltage(NodeId{mid_idx}), 10.0 * 2.0 / 3.0, 0.01);
}
