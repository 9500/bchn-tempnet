// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

class uint256;

class uint_error : public std::runtime_error {
public:
    explicit uint_error(const std::string &str) : std::runtime_error(str) {}
};

/** Template base class for unsigned big integers. */
template <size_t BITS> class base_uint {
protected:
    static constexpr size_t WIDTH = BITS / 32u;
    static_assert(BITS / 32u > 0u && BITS % 32u == 0u, "Template parameter BITS must be a positive multiple of 32.");
    static_assert(WIDTH >= 2, "Assertion WIDTH >= 2 failed (WIDTH = BITS / 32). BITS is a template parameter.");
    uint32_t pn[WIDTH];

public:
    constexpr base_uint() noexcept : pn{0} /* 0-init */ {}

    constexpr base_uint(const base_uint &b) noexcept {
        if (std::is_constant_evaluated()) {
            std::copy_n(std::begin(b.pn), WIDTH, std::begin(pn));
        } else {
            std::memcpy(pn, b.pn, size());
        }
    }

    constexpr base_uint &operator=(const base_uint &b) noexcept {
        if (this != &b) {
            if (std::is_constant_evaluated()) {
                std::copy_n(std::begin(b.pn), WIDTH, std::begin(pn));
            } else {
                std::memcpy(pn, b.pn, size());
            }
        }
        return *this;
    }

    constexpr base_uint(uint64_t b) noexcept {
        pn[0] = static_cast<uint32_t>(b);
        pn[1] = static_cast<uint32_t>(b >> 32);
        if (std::is_constant_evaluated()) {
            std::fill_n(std::begin(pn) + 2, WIDTH - 2, uint32_t{0});
        } else {
            std::memset(&pn[2], 0, (WIDTH - 2) * sizeof(*pn));
        }
    }

    explicit base_uint(const std::string &str) noexcept;

    // Uninitialized construction (for immediate overwrite -- use with caution)
    struct Uninitialized_t {};
    static constexpr Uninitialized_t Uninitialized{};
    explicit constexpr base_uint(Uninitialized_t) noexcept {}

    constexpr const base_uint operator~() const noexcept {
        base_uint ret{Uninitialized};
        for (size_t i = 0; i < WIDTH; ++i) {
            ret.pn[i] = ~pn[i];
        }
        return ret;
    }

    constexpr const base_uint operator-() const noexcept {
        base_uint ret{Uninitialized};
        for (size_t i = 0; i < WIDTH; ++i) {
            ret.pn[i] = ~pn[i];
        }
        ++ret;
        return ret;
    }

    double getdouble() const noexcept;

    constexpr base_uint &operator=(uint64_t b) noexcept {
        pn[0] = static_cast<uint32_t>(b);
        pn[1] = static_cast<uint32_t>(b >> 32);
        if (std::is_constant_evaluated()) {
            std::fill_n(std::begin(pn) + 2, WIDTH - 2, uint32_t{0});
        } else {
            std::memset(&pn[2], 0, (WIDTH - 2) * sizeof(*pn));
        }
        return *this;
    }

    constexpr base_uint &operator^=(const base_uint &b) noexcept {
        for (size_t i = 0; i < WIDTH; ++i) {
            pn[i] ^= b.pn[i];
        }
        return *this;
    }

    constexpr base_uint &operator&=(const base_uint &b) noexcept {
        if (this != &b) { // optimization: *this &= *this is identity
            for (size_t i = 0; i < WIDTH; ++i) {
                pn[i] &= b.pn[i];
            }
        }
        return *this;
    }

    constexpr base_uint &operator|=(const base_uint &b) noexcept {
        if (this != &b) { // optimization: *this |= *this is identity
            for (size_t i = 0; i < WIDTH; ++i) {
                pn[i] |= b.pn[i];
            }
        }
        return *this;
    }

    constexpr base_uint &operator^=(uint64_t b) noexcept {
        pn[0] ^= static_cast<uint32_t>(b);
        pn[1] ^= static_cast<uint32_t>(b >> 32);
        return *this;
    }

    constexpr base_uint &operator|=(uint64_t b) noexcept {
        pn[0] |= static_cast<uint32_t>(b);
        pn[1] |= static_cast<uint32_t>(b >> 32);
        return *this;
    }

    base_uint &operator<<=(unsigned int shift) noexcept;
    base_uint &operator>>=(unsigned int shift) noexcept;

    constexpr base_uint &operator+=(const base_uint &b) noexcept {
        uint64_t carry = 0;
        for (size_t i = 0; i < WIDTH; ++i) {
            uint64_t n = carry + pn[i] + b.pn[i];
            pn[i] = n & 0xffffffff;
            carry = n >> 32;
        }
        return *this;
    }

    constexpr base_uint &operator-=(const base_uint &b) noexcept {
        *this += -b;
        return *this;
    }

    constexpr base_uint &operator+=(uint64_t b64) noexcept {
        base_uint b(b64);
        *this += b;
        return *this;
    }

    constexpr base_uint &operator-=(uint64_t b64) noexcept {
        base_uint b(b64);
        *this += -b;
        return *this;
    }

    base_uint &operator*=(uint32_t b32) noexcept;
    base_uint &operator*=(const base_uint &b) noexcept;
    base_uint &operator/=(const base_uint &b);

    constexpr base_uint &operator++() noexcept {
        // prefix operator
        size_t i = 0;
        while (i < WIDTH && ++pn[i] == 0) {
            ++i;
        }
        return *this;
    }

    constexpr base_uint operator++(int) noexcept {
        // postfix operator
        base_uint ret{*this};
        ++(*this);
        return ret;
    }

    constexpr base_uint &operator--() noexcept {
        // prefix operator
        size_t i = 0;
        while (i < WIDTH && --pn[i] == std::numeric_limits<uint32_t>::max()) {
            ++i;
        }
        return *this;
    }

    constexpr base_uint operator--(int) noexcept {
        // postfix operator
        base_uint ret{*this};
        --(*this);
        return ret;
    }

    int CompareTo(const base_uint &b) const noexcept;
    bool EqualTo(uint64_t b) const noexcept;

    friend inline constexpr base_uint operator+(const base_uint &a, const base_uint &b) noexcept {
        return base_uint(a) += b;
    }
    friend inline constexpr base_uint operator-(const base_uint &a, const base_uint &b) noexcept {
        return base_uint(a) -= b;
    }
    friend inline base_uint operator*(const base_uint &a, const base_uint &b) noexcept {
        return base_uint(a) *= b;
    }
    friend inline base_uint operator/(const base_uint &a, const base_uint &b) {
        return base_uint(a) /= b;
    }
    friend inline constexpr base_uint operator|(const base_uint &a, const base_uint &b) noexcept {
        return base_uint(a) |= b;
    }
    friend inline constexpr base_uint operator&(const base_uint &a, const base_uint &b) noexcept {
        return base_uint(a) &= b;
    }
    friend inline constexpr base_uint operator^(const base_uint &a, const base_uint &b) noexcept {
        return base_uint(a) ^= b;
    }
    friend inline base_uint operator>>(const base_uint &a, int shift) noexcept {
        return base_uint(a) >>= shift;
    }
    friend inline base_uint operator<<(const base_uint &a, int shift) noexcept {
        return base_uint(a) <<= shift;
    }
    friend inline base_uint operator*(const base_uint &a, uint32_t b) noexcept {
        return base_uint(a) *= b;
    }
    friend inline constexpr bool operator==(const base_uint &a, const base_uint &b) noexcept {
        if (&a == &b) return true;
        if (std::is_constant_evaluated()) {
            return std::equal(std::begin(a.pn), std::end(a.pn), std::begin(b.pn));
        } else {
            return std::memcmp(a.pn, b.pn, sizeof(a.pn)) == 0;
        }
    }
    friend inline constexpr bool operator!=(const base_uint &a, const base_uint &b) noexcept {
        return !(a == b);
    }
    friend inline bool operator>(const base_uint &a, const base_uint &b) noexcept {
        return a.CompareTo(b) > 0;
    }
    friend inline bool operator<(const base_uint &a, const base_uint &b) noexcept {
        return a.CompareTo(b) < 0;
    }
    friend inline bool operator>=(const base_uint &a, const base_uint &b) noexcept {
        return a.CompareTo(b) >= 0;
    }
    friend inline bool operator<=(const base_uint &a, const base_uint &b) noexcept {
        return a.CompareTo(b) <= 0;
    }
    friend inline bool operator==(const base_uint &a, uint64_t b) noexcept {
        return a.EqualTo(b);
    }
    friend inline bool operator!=(const base_uint &a, uint64_t b) noexcept {
        return !a.EqualTo(b);
    }

    std::string GetHex() const;
    void SetHex(const char *psz) noexcept;
    void SetHex(const std::string &str) noexcept;
    std::string ToString() const;

    static constexpr size_t size() noexcept { return sizeof(std::declval<base_uint>().pn); }

    /**
     * Returns the position of the highest bit set plus one, or zero if the
     * value is zero.
     */
    unsigned int bits() const noexcept;

    constexpr uint64_t GetLow64() const noexcept {
        return pn[0] | static_cast<uint64_t>(pn[1]) << 32;
    }
};

/** 256-bit unsigned big integer. */
class arith_uint256 : public base_uint<256> {
public:
    constexpr arith_uint256() noexcept {}
    constexpr arith_uint256(const base_uint<256> &b) noexcept : base_uint<256>(b) {}
    constexpr arith_uint256(uint64_t b) noexcept : base_uint<256>(b) {}
    explicit arith_uint256(const std::string &str) noexcept : base_uint<256>(str) {}
    explicit constexpr arith_uint256(Uninitialized_t u) noexcept : base_uint(u) {}

    /**
     * The "compact" format is a representation of a whole number N using an
     * unsigned 32bit number similar to a floating point format.
     * The most significant 8 bits are the unsigned exponent of base 256.
     * This exponent can be thought of as "number of bytes of N".
     * The lower 23 bits are the mantissa.
     * Bit number 24 (0x800000) represents the sign of N.
     * N = (-1^sign) * mantissa * 256^(exponent-3)
     *
     * Satoshi's original implementation used BN_bn2mpi() and BN_mpi2bn().
     * MPI uses the most significant bit of the first byte as sign.
     * Thus 0x1234560000 is compact (0x05123456)
     * and  0xc0de000000 is compact (0x0600c0de)
     *
     * Bitcoin only uses this "compact" format for encoding difficulty targets,
     * which are unsigned 256bit quantities. Thus, all the complexities of the
     * sign bit and using base 256 are probably an implementation accident.
     */
    arith_uint256 &SetCompact(uint32_t nCompact, bool *pfNegative = nullptr, bool *pfOverflow = nullptr) noexcept;
    uint32_t GetCompact(bool fNegative = false) const noexcept;

    friend uint256 ArithToUint256(const arith_uint256 &) noexcept;
    friend arith_uint256 UintToArith256(const uint256 &) noexcept;
};

uint256 ArithToUint256(const arith_uint256 &) noexcept;
arith_uint256 UintToArith256(const uint256 &) noexcept;
