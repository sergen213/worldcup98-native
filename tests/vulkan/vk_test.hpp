#pragma once
// Helpers for Vulkan renderer tests: headless renderer with validation forced on.
#include "check.hpp"

#include "wc98/renderer/vulkan.hpp"

#include <array>
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <memory>

struct VkTest {
    std::shared_ptr<std::atomic<std::uint32_t>> messages = std::make_shared<std::atomic<std::uint32_t>>(0);
    std::unique_ptr<wc98::render::Renderer> r;

    explicit VkTest(const wc98::render::RenderSettings& s = {}) {
        wc98::render::VulkanConfig cfg;
        cfg.validation = true;
        cfg.validationCounter = messages;
        try {
            r = wc98::render::createVulkanRenderer(cfg, s);
        } catch (const wc98::render::NoVulkanDevice& e) {
            std::fprintf(stderr, "skipping: %s\n", e.what());
            std::exit(77);
        }
    }
    // Destroys the renderer and requires zero validation messages over its whole lifetime.
    void finish() {
        r.reset();
        CHECK(messages->load() == 0);
    }
};

inline std::array<int, 4> pixel(const wc98::render::Image& img, std::uint32_t x, std::uint32_t y) {
    CHECK(x < img.width && y < img.height);
    const std::uint8_t* p = &img.rgba[(std::size_t(y) * img.width + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}

inline bool near(std::array<int, 4> c, int r, int g, int b, int tol = 3) {
    bool ok = std::abs(c[0] - r) <= tol && std::abs(c[1] - g) <= tol && std::abs(c[2] - b) <= tol;
    if (!ok) std::fprintf(stderr, "pixel (%d,%d,%d) != expected (%d,%d,%d)\n", c[0], c[1], c[2], r, g, b);
    return ok;
}
