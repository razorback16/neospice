#include <gtest/gtest.h>
#include "output/raw_writer.hpp"
#include "core/transient.hpp"
#include <fstream>
#include <filesystem>

using namespace neospice;

TEST(RawWriter, WritesTransientFile) {
    TransientResult result;
    result.time = {0.0, 1e-6, 2e-6};
    result.voltages["v(out)"] = {0.0, 2.5, 5.0};

    auto path = std::filesystem::temp_directory_path() / "test_output.raw";
    write_raw(path.string(), result);

    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_GT(std::filesystem::file_size(path), 0u);

    std::filesystem::remove(path);
}
