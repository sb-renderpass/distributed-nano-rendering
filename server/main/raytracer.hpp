#pragma once

#include <cmath>
#include <cstring>

#include "types.hpp"

#if 0

struct fp32_t
{
    int32_t value {0};

    constexpr fp32_t() = default;
    constexpr fp32_t(float x) : value {static_cast<int32_t>(x * (1 << 16))} {}

    constexpr auto to_float() const -> float { return static_cast<float>(value) / (1 << 16); }

    constexpr auto operator+(const fp32_t& x) const -> fp32_t { return value + x.value; }
    constexpr auto operator-(const fp32_t& x) const -> fp32_t { return value - x.value; }
    constexpr auto operator/(const fp32_t& x) const -> fp32_t { return (value / x.value) << 16; }
    constexpr auto operator*(const fp32_t& x) const -> fp32_t { return (value * x.value) >> 16; }
};

struct fp32_vec3_t
{
    fp32_t x {0.0F};
    fp32_t y {0.0F};
    fp32_t z {0.0F};

    constexpr auto operator+(const fp32_vec3_t& v) const -> fp32_vec3_t { return {x + v.x, y + v.y, z + v.z}; }
    constexpr auto operator-(const fp32_vec3_t& v) const -> fp32_vec3_t { return {x - v.x, y - v.y, z - v.z}; }
    constexpr auto operator/(const fp32_vec3_t& v) const -> fp32_vec3_t { return {x / v.x, y / v.y, z / v.z}; }
    constexpr auto operator*(const fp32_vec3_t& v) const -> fp32_vec3_t { return {x * v.x, y * v.y, z * v.z}; }
    constexpr auto operator*(const fp32_t&      s) const -> fp32_vec3_t { return {x * s, y * s, z * s}; }
};

constexpr auto dot(const fp32_vec3_t& a, const fp32_vec3_t& b) -> fp32_t { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr auto normalize(const fp32_vec3_t& v) -> fp32_vec3_t
{
    return v * dot(v, v);
}

auto test(const fp32_vec3_t& dir) -> bool
{
    return dir.x.value > 0 && dir.y.value > 0 && dir.z.value > 0;
}

#endif

struct vec3_t
{
    int x {0};
    int y {0};
    int z {0};

    constexpr auto operator-()                const -> vec3_t { return {-x, -y, -z}; }
    constexpr auto operator-(const vec3_t& v) const -> vec3_t { return {x - v.x, y - v.y, z - v.z}; }
    constexpr auto operator+(const vec3_t& v) const -> vec3_t { return {x + v.x, y + v.y, z + v.z}; }
    constexpr auto operator*(int           s) const -> vec3_t { return {x * s, y * s, z * s}; }
    constexpr auto operator/(int           s) const -> vec3_t { return {x / s, y / s, z / s}; }
};

constexpr auto dot(const vec3_t& a, const vec3_t& b) -> float { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct sphere_t
{
    vec3_t center {0, 0, 0};
    float  radius {1};
};

auto fast_sqrt(int n)
{
    auto x = n / 2;
    x = (x + n / x) / 2; 
    x = (x + n / x) / 2; 
    x = (x + n / x) / 2; 
    x = (x + n / x) / 2; 
    return x;
}

auto intersect(const vec3_t& origin, const vec3_t& dir, const sphere_t& sphere) -> bool
{
    const auto P1 = sphere.center - origin;
    const auto P2 = (dir * dot(dir, P1)) / dot(dir, dir);
    const auto d  = P1 - P2;
    const auto d2 = dot(d, d);
    const auto r2 = sphere.radius * sphere.radius;
    //return d2 <= r2;
    if (d2 > r2) return false;
    const auto k = fast_sqrt(dot(P2, P2));
    const auto m = fast_sqrt(r2 - d2);
    const auto t = std::min(k - m, k + m);
    return t >= 0;
}

[[maybe_unused]]
auto render_raytracer(frame_t& frame) -> void
{
    const auto sphere = sphere_t {{0, 0, -256}, 128};
    const auto origin = vec3_t {0, 0, 0};

    for (auto j = 0; j < frame.height; j++)
    {
        const auto y = j - frame.height / 2;
        for (auto i = 0; i < frame.width; i++)
        {
            //const auto color = static_cast<uint8_t>((255.0F * j) / frame.height);
            //frame.buffer[i + j * frame.width] = color;

            const auto x = i - frame.width / 2;
            const auto dir = vec3_t {x, y, -128};
            if (intersect(origin, dir, sphere)) frame.buffer[i + j * frame.width] = 0x47;
        }
    }
}

