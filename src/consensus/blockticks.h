// Copyright (c) 2026-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <compare>  // used for operator<=> below
#include <cstdint>

/// This class abstracts out the unsigned 64-bit "block tick" value into a separate type-safe class
class BlockTicks {
    uint64_t val{}; // Tick value in seconds.  Note: If we rescale this the c'tor needs to change as do many callsites.

public:
    constexpr BlockTicks() noexcept = default;
    explicit constexpr BlockTicks(uint64_t valInSeconds) noexcept : val{valInSeconds} {}
    constexpr BlockTicks(const BlockTicks &other) noexcept : val{other.val} {}

    /**
     * Getters
     */
    /// Return the raw tick value (presently this is always in seconds)
    constexpr uint64_t value() const noexcept { return val; }

    /// Return the tick value as a type T, scaled to seconds (note: the raw tick value is already always in seconds)
    template <typename T = int64_t>
    constexpr T toSecs() const noexcept { return static_cast<T>(val); }

    /**
     * Assignment operator.
     */
    constexpr BlockTicks &operator=(const BlockTicks &other) noexcept {
        val = other.val;
        return *this;
    }

    /**
     * Addition & Subtraction
     */
    constexpr BlockTicks &operator+=(const BlockTicks o) noexcept { val += o.val; return *this; }
    friend constexpr BlockTicks operator+(const BlockTicks a, const BlockTicks b) noexcept {
        return BlockTicks(a).operator+=(b);
    }
    constexpr BlockTicks &operator-=(const BlockTicks o) noexcept { val -= o.val; return *this; }
    friend constexpr BlockTicks operator-(const BlockTicks a, const BlockTicks b) noexcept {
        return BlockTicks(a).operator-=(b);
    }

    /**
     * Multiplication & Division
     */
    constexpr BlockTicks &operator*=(const uint64_t b) noexcept { val *= b; return *this; }
    friend constexpr BlockTicks operator*(const BlockTicks a, const uint64_t b) noexcept {
        return BlockTicks(a).operator*=(b);
    }
    friend constexpr BlockTicks operator*(const uint64_t a, const BlockTicks b) noexcept { return operator*(b, a); }
    friend constexpr uint64_t operator/(const BlockTicks a, const BlockTicks b) noexcept { return a.val / b.val; }
    friend constexpr uint64_t operator%(const BlockTicks a, const BlockTicks b) noexcept { return a.val % b.val; }

    /**
     * Comparison ops
     */
    constexpr bool operator==(const BlockTicks &o) const noexcept { return val == o.val; }
    constexpr auto operator<=>(const BlockTicks &o) const noexcept { return val <=> o.val; }
};
