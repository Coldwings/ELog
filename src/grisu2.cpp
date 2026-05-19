// Florian Loitsch's Grisu2 algorithm: shortest round-trip decimal for
// IEEE 754 double. Successful in ~99.5% of cases; falls back to snprintf
// for the rare misses. Public-domain reference: Loitsch's paper /
// milo/dtoa-benchmark.

#include "elog/grisu2.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace elog {
namespace detail {

namespace {

struct DiyFp {
    std::uint64_t f;
    int e;
};

DiyFp diy_subtract(DiyFp x, DiyFp y) noexcept {
    return DiyFp{x.f - y.f, x.e};
}

DiyFp diy_multiply(DiyFp x, DiyFp y) noexcept {
    const std::uint64_t M32 = 0xFFFFFFFFu;
    std::uint64_t a = x.f >> 32;
    std::uint64_t b = x.f & M32;
    std::uint64_t c = y.f >> 32;
    std::uint64_t d = y.f & M32;
    std::uint64_t ac = a * c;
    std::uint64_t bc = b * c;
    std::uint64_t ad = a * d;
    std::uint64_t bd = b * d;
    std::uint64_t mid = (bd >> 32) + (ad & M32) + (bc & M32);
    mid += 1u << 31;  // round
    return DiyFp{ac + (ad >> 32) + (bc >> 32) + (mid >> 32), x.e + y.e + 64};
}

DiyFp diy_normalize(DiyFp v) noexcept {
    while (!(v.f & 0xFFC0000000000000ull)) { v.f <<= 10; v.e -= 10; }
    while (!(v.f & 0x8000000000000000ull)) { v.f <<= 1;  v.e -= 1; }
    return v;
}

DiyFp diy_normalize_boundary(DiyFp v) noexcept {
    while (!(v.f & 0x4000000000000000ull)) { v.f <<= 1; v.e -= 1; }
    v.f <<= 1; v.e -= 1;  // shift into top
    while (!(v.f & 0x8000000000000000ull)) { v.f <<= 1; v.e -= 1; }
    return v;
}

DiyFp double_to_diy(double d) noexcept {
    union { double dv; std::uint64_t uv; } u;
    u.dv = d;
    int biased_e = static_cast<int>((u.uv >> 52) & 0x7FFu);
    std::uint64_t sig = u.uv & 0x000FFFFFFFFFFFFFull;
    if (biased_e != 0) {
        return DiyFp{sig + 0x0010000000000000ull, biased_e - 1075};
    }
    return DiyFp{sig, -1074};
}

void normalized_boundaries(double v, DiyFp& m_minus, DiyFp& m_plus) noexcept {
    DiyFp w = double_to_diy(v);
    DiyFp pl = DiyFp{(w.f << 1) + 1, w.e - 1};
    pl = diy_normalize_boundary(pl);
    DiyFp mi;
    union { double dv; std::uint64_t uv; } u;
    u.dv = v;
    if (u.uv == 0x0010000000000000ull) {
        mi = DiyFp{(w.f << 2) - 1, w.e - 2};
    } else {
        mi = DiyFp{(w.f << 1) - 1, w.e - 1};
    }
    mi.f <<= (mi.e - pl.e);
    mi.e = pl.e;
    m_plus = pl;
    m_minus = mi;
}

constexpr int kCachedPowersF_size = 87;
const std::uint64_t kCachedPowersF[kCachedPowersF_size] = {
    0xfa8fd5a0081c0288ull, 0xbaaee17fa23ebf76ull, 0x8b16fb203055ac76ull,
    0xcf42894a5dce35eaull, 0x9a6bb0aa55653b2dull, 0xe61acf033d1a45dfull,
    0xab70fe17c79ac6caull, 0xff77b1fcbebcdc4full, 0xbe5691ef416bd60cull,
    0x8dd01fad907ffc3cull, 0xd3515c2831559a83ull, 0x9d71ac8fada6c9b5ull,
    0xea9c227723ee8bcbull, 0xaecc49914078536dull, 0x823c12795db6ce57ull,
    0xc21094364dfb5637ull, 0x9096ea6f3848984full, 0xd77485cb25823ac7ull,
    0xa086cfcd97bf97f4ull, 0xef340a98172aace5ull, 0xb23867fb2a35b28eull,
    0x84c8d4dfd2c63f3bull, 0xc5dd44271ad3cdbaull, 0x936b9fcebb25c996ull,
    0xdbac6c247d62a584ull, 0xa3ab66580d5fdaf6ull, 0xf3e2f893dec3f126ull,
    0xb5b5ada8aaff80b8ull, 0x87625f056c7c4a8bull, 0xc9bcff6034c13053ull,
    0x964e858c91ba2655ull, 0xdff9772470297ebdull, 0xa6dfbd9fb8e5b88full,
    0xf8a95fcf88747d94ull, 0xb94470938fa89bcfull, 0x8a08f0f8bf0f156bull,
    0xcdb02555653131b6ull, 0x993fe2c6d07b7facull, 0xe45c10c42a2b3b06ull,
    0xaa242499697392d3ull, 0xfd87b5f28300ca0eull, 0xbce5086492111aebull,
    0x8cbccc096f5088ccull, 0xd1b71758e219652cull, 0x9c40000000000000ull,
    0xe8d4a51000000000ull, 0xad78ebc5ac620000ull, 0x813f3978f8940984ull,
    0xc097ce7bc90715b3ull, 0x8f7e32ce7bea5c70ull, 0xd5d238a4abe98068ull,
    0x9f4f2726179a2245ull, 0xed63a231d4c4fb27ull, 0xb0de65388cc8ada8ull,
    0x83c7088e1aab65dbull, 0xc45d1df942711d9aull, 0x924d692ca61be758ull,
    0xda01ee641a708deaull, 0xa26da3999aef774aull, 0xf209787bb47d6b85ull,
    0xb454e4a179dd1877ull, 0x865b86925b9bc5c2ull, 0xc83553c5c8965d3dull,
    0x952ab45cfa97a0b3ull, 0xde469fbd99a05fe3ull, 0xa59bc234db398c25ull,
    0xf6c69a72a3989f5cull, 0xb7dcbf5354e9beceull, 0x88fcf317f22241e2ull,
    0xcc20ce9bd35c78a5ull, 0x98165af37b2153dfull, 0xe2a0b5dc971f303aull,
    0xa8d9d1535ce3b396ull, 0xfb9b7cd9a4a7443cull, 0xbb764c4ca7a44410ull,
    0x8bab8eefb6409c1aull, 0xd01fef10a657842cull, 0x9b10a4e5e9913129ull,
    0xe7109bfba19c0c9dull, 0xac2820d9623bf429ull, 0x80444b5e7aa7cf85ull,
    0xbf21e44003acdd2dull, 0x8e679c2f5e44ff8full, 0xd433179d9c8cb841ull,
    0x9e19db92b4e31ba9ull, 0xeb96bf6ebadf77d9ull, 0xaf87023b9bf0ee6bull
};

const std::int16_t kCachedPowersE[kCachedPowersF_size] = {
    -1220, -1193, -1166, -1140, -1113, -1087, -1060, -1034, -1007,  -980,
     -954,  -927,  -901,  -874,  -847,  -821,  -794,  -768,  -741,  -715,
     -688,  -661,  -635,  -608,  -582,  -555,  -529,  -502,  -475,  -449,
     -422,  -396,  -369,  -343,  -316,  -289,  -263,  -236,  -210,  -183,
     -157,  -130,  -103,   -77,   -50,   -24,     3,    30,    56,    83,
      109,   136,   162,   189,   216,   242,   269,   295,   322,   348,
      375,   402,   428,   455,   481,   508,   534,   561,   588,   614,
      641,   667,   694,   720,   747,   774,   800,   827,   853,   880,
      907,   933,   960,   986,  1013,  1039,  1066
};

const std::int16_t kCachedPowersK[kCachedPowersF_size] = {
    -348, -340, -332, -324, -316, -308, -300, -292, -284, -276,
    -268, -260, -252, -244, -236, -228, -220, -212, -204, -196,
    -188, -180, -172, -164, -156, -148, -140, -132, -124, -116,
    -108, -100,  -92,  -84,  -76,  -68,  -60,  -52,  -44,  -36,
     -28,  -20,  -12,   -4,    4,   12,   20,   28,   36,   44,
      52,   60,   68,   76,   84,   92,  100,  108,  116,  124,
     132,  140,  148,  156,  164,  172,  180,  188,  196,  204,
     212,  220,  228,  236,  244,  252,  260,  268,  276,  284,
     292,  300,  308,  316,  324,  332,  340
};

DiyFp cached_power(int e, int& K) noexcept {
    double dk = (-61 - e) * 0.30102999566398114 + 347;
    int k = static_cast<int>(dk);
    if (k != dk) ++k;
    int idx = (k >> 3) + 1;
    if (idx < 0) idx = 0;
    if (idx >= kCachedPowersF_size) idx = kCachedPowersF_size - 1;
    K = -(-348 + (idx << 3));
    return DiyFp{kCachedPowersF[idx], static_cast<int>(kCachedPowersE[idx])};
}

const std::uint64_t kPow10[20] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull,
    1000000ull, 10000000ull, 100000000ull, 1000000000ull,
    10000000000ull, 100000000000ull, 1000000000000ull,
    10000000000000ull, 100000000000000ull, 1000000000000000ull,
    10000000000000000ull, 100000000000000000ull,
    1000000000000000000ull, 10000000000000000000ull
};

void grisu_round(char* buffer, int len, std::uint64_t delta,
                 std::uint64_t rest, std::uint64_t ten_kappa,
                 std::uint64_t wp_w) noexcept {
    while (rest < wp_w && delta - rest >= ten_kappa &&
           (rest + ten_kappa < wp_w ||
            wp_w - rest > rest + ten_kappa - wp_w)) {
        --buffer[len - 1];
        rest += ten_kappa;
    }
}

int dec_count_u32(std::uint32_t v) noexcept {
    if (v < 10u) return 1;
    if (v < 100u) return 2;
    if (v < 1000u) return 3;
    if (v < 10000u) return 4;
    if (v < 100000u) return 5;
    if (v < 1000000u) return 6;
    if (v < 10000000u) return 7;
    if (v < 100000000u) return 8;
    if (v < 1000000000u) return 9;
    return 10;
}

int digit_gen(DiyFp W, DiyFp Mp, std::uint64_t delta,
              char* buffer, int& K) noexcept {
    DiyFp one{1ull << -Mp.e, Mp.e};
    DiyFp wp_w = diy_subtract(Mp, W);
    std::uint32_t p1 = static_cast<std::uint32_t>(Mp.f >> -one.e);
    std::uint64_t p2 = Mp.f & (one.f - 1);
    int kappa = dec_count_u32(p1);
    int len = 0;
    while (kappa > 0) {
        std::uint32_t d = 0;
        switch (kappa) {
            case 10: d = p1 / 1000000000u; p1 %= 1000000000u; break;
            case  9: d = p1 / 100000000u;  p1 %= 100000000u;  break;
            case  8: d = p1 / 10000000u;   p1 %= 10000000u;   break;
            case  7: d = p1 / 1000000u;    p1 %= 1000000u;    break;
            case  6: d = p1 / 100000u;     p1 %= 100000u;     break;
            case  5: d = p1 / 10000u;      p1 %= 10000u;      break;
            case  4: d = p1 / 1000u;       p1 %= 1000u;       break;
            case  3: d = p1 / 100u;        p1 %= 100u;        break;
            case  2: d = p1 / 10u;         p1 %= 10u;         break;
            case  1: d = p1;               p1 = 0;            break;
        }
        if (d != 0 || len != 0) buffer[len++] = static_cast<char>('0' + d);
        --kappa;
        std::uint64_t tmp = (static_cast<std::uint64_t>(p1) << -one.e) + p2;
        if (tmp <= delta) {
            K += kappa;
            grisu_round(buffer, len, delta, tmp,
                        static_cast<std::uint64_t>(kPow10[kappa]) << -one.e,
                        wp_w.f);
            return len;
        }
    }
    for (;;) {
        p2 *= 10;
        delta *= 10;
        char d = static_cast<char>(p2 >> -one.e);
        if (d != 0 || len != 0) buffer[len++] = static_cast<char>('0' + d);
        p2 &= one.f - 1;
        --kappa;
        if (p2 < delta) {
            K += kappa;
            int idx = -kappa;
            if (idx < 0) idx = 0;
            if (idx >= 20) idx = 19;
            grisu_round(buffer, len, delta, p2, one.f, wp_w.f * kPow10[idx]);
            return len;
        }
    }
}

int grisu2(double v, char* buffer, int& K) noexcept {
    DiyFp w_minus, w_plus;
    normalized_boundaries(v, w_minus, w_plus);
    DiyFp w = diy_normalize(double_to_diy(v));
    DiyFp c_mk = cached_power(w_plus.e, K);
    DiyFp W = diy_multiply(w, c_mk);
    DiyFp Wp = diy_multiply(w_plus, c_mk);
    DiyFp Wm = diy_multiply(w_minus, c_mk);
    Wm.f += 1;
    Wp.f -= 1;
    return digit_gen(W, Wp, Wp.f - Wm.f, buffer, K);
}

std::size_t prettify(char* buffer, int len, int K, char* out) noexcept {
    int kk = len + K;
    if (kk > 0 && kk <= 21) {
        // 12345 -> 123450000 with K positive but small handled below
        if (len <= kk) {
            std::memcpy(out, buffer, len);
            for (int i = len; i < kk; ++i) out[i] = '0';
            return static_cast<std::size_t>(kk);
        }
        std::memcpy(out, buffer, kk);
        out[kk] = '.';
        std::memcpy(out + kk + 1, buffer + kk, len - kk);
        return static_cast<std::size_t>(len + 1);
    }
    if (kk <= 0 && kk > -6) {
        std::size_t offset = 2u - static_cast<std::size_t>(kk);
        out[0] = '0';
        out[1] = '.';
        for (int i = 0; i < -kk; ++i) out[2 + i] = '0';
        std::memcpy(out + offset, buffer, len);
        return offset + static_cast<std::size_t>(len);
    }
    if (len == 1) {
        out[0] = buffer[0];
        out[1] = 'e';
        std::size_t pos = 2;
        int exp = kk - 1;
        if (exp < 0) { out[pos++] = '-'; exp = -exp; }
        else { out[pos++] = '+'; }
        if (exp >= 100) {
            out[pos++] = static_cast<char>('0' + exp / 100); exp %= 100;
            out[pos++] = static_cast<char>('0' + exp / 10);
            out[pos++] = static_cast<char>('0' + exp % 10);
        } else if (exp >= 10) {
            out[pos++] = static_cast<char>('0' + exp / 10);
            out[pos++] = static_cast<char>('0' + exp % 10);
        } else {
            out[pos++] = static_cast<char>('0' + exp);
        }
        return pos;
    }
    out[0] = buffer[0];
    out[1] = '.';
    std::memcpy(out + 2, buffer + 1, len - 1);
    std::size_t pos = static_cast<std::size_t>(len + 1);
    out[pos++] = 'e';
    int exp = kk - 1;
    if (exp < 0) { out[pos++] = '-'; exp = -exp; }
    else { out[pos++] = '+'; }
    if (exp >= 100) {
        out[pos++] = static_cast<char>('0' + exp / 100); exp %= 100;
        out[pos++] = static_cast<char>('0' + exp / 10);
        out[pos++] = static_cast<char>('0' + exp % 10);
    } else if (exp >= 10) {
        out[pos++] = static_cast<char>('0' + exp / 10);
        out[pos++] = static_cast<char>('0' + exp % 10);
    } else {
        out[pos++] = static_cast<char>('0' + exp);
    }
    return pos;
}

}  // namespace

std::size_t format_double(char* out, double v) noexcept {
    union { double dv; std::uint64_t uv; } u;
    u.dv = v;
    bool neg = (u.uv >> 63) != 0;
    std::uint64_t bits = u.uv & 0x7FFFFFFFFFFFFFFFull;
    std::size_t pos = 0;
    if (neg) out[pos++] = '-';
    if (bits == 0) { out[pos++] = '0'; return pos; }
    if (bits >= 0x7FF0000000000000ull) {
        if (bits == 0x7FF0000000000000ull) {
            std::memcpy(out + pos, "inf", 3);
            return pos + 3;
        }
        std::memcpy(out + pos, "nan", 3);
        return pos + 3;
    }
    char digits[24];
    int K = 0;
    double abs_v;
    {
        union { std::uint64_t uv; double dv; } w;
        w.uv = bits;
        abs_v = w.dv;
    }
    int len = grisu2(abs_v, digits, K);
    return pos + prettify(digits, len, K, out + pos);
}

}  // namespace detail
}  // namespace elog
