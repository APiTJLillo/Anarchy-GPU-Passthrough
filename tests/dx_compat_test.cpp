#include <gtest/gtest.h>
#include "common/gpu/dx_compat.hpp"

#ifdef HAS_DIRECTX
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#endif

using namespace anarchy::gpu;

class DXCompatTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef _WIN32
        DXConfig config;
        config.use_d3d12 = true;
        config.enable_debug_layer = false;
        config.enable_validation = false;
        config.feature_level = D3D_FEATURE_LEVEL_12_0;
        config.allow_tearing = true;
        dx_compat = std::make_unique<DXCompat>(config);
#endif
    }

    void TearDown() override {
        dx_compat.reset();
    }

    std::unique_ptr<DXCompat> dx_compat;
};

TEST_F(DXCompatTest, InitializationTest) {
#ifdef _WIN32
    // Windows-specific DirectX tests
    ASSERT_TRUE(dx_compat->initialize());
#else
    GTEST_SKIP() << "Skipping DirectX tests on non-Windows platform";
#endif
}

TEST_F(DXCompatTest, CommandListRecording) {
    ASSERT_TRUE(dx_compat->initialize());
    // Add more test cases here
} 