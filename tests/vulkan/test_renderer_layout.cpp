// Presentation layout (pillarbox / Hor+), camera FOV and sprite batching; no GPU needed.
#include "check.hpp"

#include "wc98/renderer/renderer.hpp"

#include <cmath>

using namespace wc98::render;

int main() {
    CHECK((fit4x3(1920, 1080) == Rect{240, 0, 1440, 1080}));
    CHECK((fit4x3(1280, 1024) == Rect{0, 32, 1280, 960}));
    CHECK((fit4x3(640, 480) == Rect{0, 0, 640, 480}));

    Layout o = computeLayout({PresentMode::Original, Aspect::Widescreen, true}, 1920, 1080);
    CHECK(o.sceneW == 640 && o.sceneH == 480 && (o.ui == Rect{0, 0, 640, 480}) && (o.dest == Rect{240, 0, 1440, 1080}));
    Layout p = computeLayout({PresentMode::Enhanced, Aspect::Pillarbox4x3, true}, 1920, 1080);
    CHECK(p.sceneW == 1440 && p.sceneH == 1080 && (p.ui == Rect{0, 0, 1440, 1080}) && (p.dest == Rect{240, 0, 1440, 1080}));
    Layout w = computeLayout({PresentMode::Enhanced, Aspect::Widescreen, true}, 1920, 1080);
    CHECK(w.sceneW == 1920 && w.sceneH == 1080 && (w.ui == Rect{240, 0, 1440, 1080}) && (w.dest == Rect{0, 0, 1920, 1080}));

    // Hor+: vertical FOV fixed, horizontal grows with aspect.
    Camera c;
    c.verticalFovDeg = 40;
    CHECK(std::abs(c.horizontalFovDeg(4.0f / 3) - 51.78f) < 0.05f);
    CHECK(c.horizontalFovDeg(16.0f / 9) > c.horizontalFovDeg(4.0f / 3));
    CHECK(std::abs(c.projection(16.0f / 9)[1][1] - c.projection(4.0f / 3)[1][1]) < 1e-6f);

    SpriteBatch b;
    b.draw(1, {0, 0, 10, 10});
    b.draw(1, {10, 0, 10, 10});
    b.draw(2, {20, 0, 10, 10});
    CHECK(b.vertices().size() == 18 && b.runs().size() == 2);
    CHECK(b.runs()[0].vertexCount == 12 && b.runs()[1].firstVertex == 12);
    return 0;
}
