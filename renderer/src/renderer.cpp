#include "wc98/renderer/renderer.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace wc98::render {

glm::mat4 Camera::view() const { return leftHanded ? glm::lookAtLH(eye, target, up) : glm::lookAtRH(eye, target, up); }

glm::mat4 Camera::projection(float aspect) const {
    return leftHanded ? glm::perspectiveLH_ZO(glm::radians(verticalFovDeg), aspect, nearZ, farZ)
                      : glm::perspectiveRH_ZO(glm::radians(verticalFovDeg), aspect, nearZ, farZ);
}

float Camera::horizontalFovDeg(float aspect) const {
    return glm::degrees(2.0f * std::atan(std::tan(glm::radians(verticalFovDeg) * 0.5f) * aspect));
}

std::pair<glm::vec3, glm::vec3> Camera::axes() const {
    glm::mat4 v = view();
    return {glm::vec3(v[0][0], v[1][0], v[2][0]), glm::vec3(v[0][1], v[1][1], v[2][1])};
}

void WorldBatch::quad(const Material& m, const std::array<glm::vec3, 4>& c, glm::vec4 uv, std::uint32_t rgba) {
    quad(m, c, uv, {rgba, rgba, rgba, rgba});
}

void WorldBatch::quad(const Material& m, const std::array<glm::vec3, 4>& c, glm::vec4 uv, const std::array<std::uint32_t, 4>& rgba) {
    glm::vec3 n = glm::cross(c[1] - c[0], c[2] - c[0]);
    n = glm::dot(n, n) > 0 ? glm::normalize(n) : glm::vec3(0, 1, 0);
    auto v = [&](int i, float u, float t) { vertices_.push_back({c[i], n, {u, t}, rgba[std::size_t(i)]}); };
    auto first = static_cast<std::uint32_t>(vertices_.size());
    v(0, uv.x, uv.y), v(1, uv.x, uv.w), v(2, uv.z, uv.w);
    v(0, uv.x, uv.y), v(2, uv.z, uv.w), v(3, uv.z, uv.y);
    if (!runs_.empty() && runs_.back().material == m) runs_.back().vertexCount += 6;
    else runs_.push_back({m, first, 6});
}

void WorldBatch::billboard(const Material& m, glm::vec3 c, glm::vec2 h, glm::vec3 right, glm::vec3 up, glm::vec4 uv, std::uint32_t rgba) {
    glm::vec3 r = right * h.x, u = up * h.y;
    quad(m, {c - r + u, c - r - u, c + r - u, c + r + u}, uv, rgba);
}

void SpriteBatch::draw(TextureId tex, glm::vec4 dst, glm::vec4 uv, std::uint32_t rgba) {
    auto v = [&](float x, float y, float u, float t) { vertices_.push_back({{x, y, 0}, {0, 0, 1}, {u, t}, rgba}); };
    auto first = static_cast<std::uint32_t>(vertices_.size());
    float x0 = dst.x, y0 = dst.y, x1 = dst.x + dst.z, y1 = dst.y + dst.w;
    // Counter-clockwise on screen with +Y down is irrelevant: the UI pipeline does not cull.
    v(x0, y0, uv.x, uv.y), v(x0, y1, uv.x, uv.w), v(x1, y1, uv.z, uv.w);
    v(x0, y0, uv.x, uv.y), v(x1, y1, uv.z, uv.w), v(x1, y0, uv.z, uv.y);
    if (!runs_.empty() && runs_.back().texture == tex) runs_.back().vertexCount += 6;
    else runs_.push_back({tex, first, 6});
}

Rect fit4x3(std::uint32_t w, std::uint32_t h) {
    if (std::uint64_t(w) * 3 >= std::uint64_t(h) * 4) {
        auto rw = static_cast<std::uint32_t>(std::uint64_t(h) * 4 / 3);
        return {std::int32_t((w - rw) / 2), 0, rw, h};
    }
    auto rh = static_cast<std::uint32_t>(std::uint64_t(w) * 3 / 4);
    return {0, std::int32_t((h - rh) / 2), w, rh};
}

Layout computeLayout(const RenderSettings& s, std::uint32_t outW, std::uint32_t outH) {
    outW = std::max(outW, 4u), outH = std::max(outH, 3u);
    Layout l;
    Rect r = fit4x3(outW, outH);
    if (s.mode == PresentMode::Original) {
        l.sceneW = 640, l.sceneH = 480;
        l.ui = {0, 0, 640, 480};
        l.dest = r;
    } else if (s.aspect == Aspect::Pillarbox4x3) {
        l.sceneW = r.w, l.sceneH = r.h;
        l.ui = {0, 0, r.w, r.h};
        l.dest = r;
    } else {
        l.sceneW = outW, l.sceneH = outH;
        l.ui = r;
        l.dest = {0, 0, outW, outH};
    }
    return l;
}

} // namespace wc98::render
