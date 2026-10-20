#pragma once

#include <cstddef>
#include <cstdint>

namespace xs {
    template <size_t N>
    struct Lit {
        char v[N]{};
        consteval Lit(char const (&s)[N]) {
            for (size_t i = 0; i < N; i++) v[i] = s[i];
        }
    };

    constexpr uint8_t key(uint32_t seed, size_t i) {
        uint32_t x = seed ^ (uint32_t)(i * 0x9E3779B9u);
        x ^= x >> 15;
        x *= 0x2C1B3C6Du;
        x ^= x >> 12;
        return (uint8_t)(x ^ (x >> 8));
    }

    template <Lit S, uint32_t Seed>
    struct Enc {
        static constexpr size_t N = sizeof(S.v);
        struct Buf {
            char v[N];
        };
        static constexpr Buf data = [] {
            Buf b{};
            for (size_t i = 0; i < N; i++) b.v[i] = (char)(S.v[i] ^ key(Seed, i));
            return b;
        }();

        static char const* get() {
            static char out[N];
            static bool ready = false;
            if (!ready) {
                auto src = reinterpret_cast<volatile char const*>(data.v);
                for (size_t i = 0; i < N; i++) out[i] = (char)(src[i] ^ key(Seed, i));
                ready = true;
            }
            return out;
        }
    };
}

#define XS(s) (::xs::Enc<s, (uint32_t)(__COUNTER__ * 0x45D9F3Bu + __LINE__ * 0x119DE1F3u)>::get())
