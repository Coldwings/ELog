#include "elog/elog.hpp"
#include "elog/sink.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <sys/uio.h>
#include <vector>

namespace {

class CapturingSink : public elog::Sink {
public:
    void write(elog::Level, const iovec* iov, int n) override {
        std::lock_guard<std::mutex> lk(mu);
        std::string line;
        std::vector<const void*> pointers;
        for (int i = 0; i < n; ++i) {
            line.append(static_cast<const char*>(iov[i].iov_base), iov[i].iov_len);
            pointers.push_back(iov[i].iov_base);
        }
        lines.push_back(std::move(line));
        all_pointers.push_back(std::move(pointers));
        ++count;
    }
    std::mutex mu;
    int count = 0;
    std::vector<std::string> lines;
    std::vector<std::vector<const void*>> all_pointers;
};

struct Probe {
    elog::Logger L{"iov_pack_test"};
    CapturingSink* raw = nullptr;
    Probe() {
        auto s = std::unique_ptr<CapturingSink>(new CapturingSink());
        raw = s.get();
        L.add_sink(std::move(s));
        L.set_level(elog::Level::INFO);
    }
};

}  // namespace

TEST(IovPack, EmitsAllSegments) {
    Probe p;
    std::string k = "user";
    std::string v = "alice";
    elog::Iov entries[] = {
        {"key=", 4},
        {k.data(), k.size()},
        {" val=", 5},
        {v.data(), v.size()},
    };
    LOGGER_F(p.L, elog::Level::INFO, "entry: {}", elog::iov_pack(entries));
    ASSERT_EQ(p.raw->count, 1);
    EXPECT_NE(p.raw->lines[0].find("entry: key=user val=alice"), std::string::npos);
}

TEST(IovPack, PreservesZeroCopyPointers) {
    Probe p;
    std::string a = "alpha";
    std::string b = "beta";
    std::vector<elog::Iov> pieces;
    pieces.push_back({a.data(), a.size()});
    pieces.push_back({", ", 2});
    pieces.push_back({b.data(), b.size()});
    LOGGER_F(p.L, elog::Level::INFO, "list={}", elog::iov_pack(pieces));
    ASSERT_EQ(p.raw->count, 1);

    // The Iov entries from the user's vector should still point at the
    // source strings — that's the whole point.
    bool found_a = false, found_b = false;
    for (const void* ptr : p.raw->all_pointers[0]) {
        if (ptr == a.data()) found_a = true;
        if (ptr == b.data()) found_b = true;
    }
    EXPECT_TRUE(found_a) << "string 'a' was not borrowed zero-copy";
    EXPECT_TRUE(found_b) << "string 'b' was not borrowed zero-copy";
}

TEST(IovPack, EmptyPack) {
    Probe p;
    elog::Iov* nothing = nullptr;
    LOGGER_F(p.L, elog::Level::INFO, "empty=[{}]", elog::iov_pack(nothing, 0));
    ASSERT_EQ(p.raw->count, 1);
    EXPECT_NE(p.raw->lines[0].find("empty=[]"), std::string::npos);
}

TEST(IovPack, MixedWithRegularArgs) {
    Probe p;
    std::vector<elog::Iov> pieces;
    pieces.push_back({"X", 1});
    pieces.push_back({"Y", 1});
    LOGGER_F(p.L, elog::Level::INFO, "n={} pieces={} done={}",
             42, elog::iov_pack(pieces), true);
    ASSERT_EQ(p.raw->count, 1);
    EXPECT_NE(p.raw->lines[0].find("n=42 pieces=XY done=true"),
              std::string::npos);
}

// ---- Custom type whose elog_render returns iov_pack ----

namespace zerocopy_user {
struct Pair {
    std::string key;
    std::string value;
};

inline elog::iov_pack elog_render(char* /*scratch*/, std::size_t& /*pos*/,
                                  const Pair& p) noexcept {
    // Allocate from emit_f's per-call iov scratch. Each render call gets
    // its own range so multiple Pairs in the same LOG don't collide.
    elog::Iov* buf = elog::iov_scratch_alloc(3);
    buf[0] = {p.key.data(), p.key.size()};
    buf[1] = {"=", 1};
    buf[2] = {p.value.data(), p.value.size()};
    return elog::iov_pack(buf, 3);
}
}  // namespace zerocopy_user

TEST(IovPack, CustomTypeReturnsIovPack) {
    Probe p;
    zerocopy_user::Pair kv{"name", "alice"};
    LOGGER_F(p.L, elog::Level::INFO, "kv: {}", kv);
    ASSERT_EQ(p.raw->count, 1);
    EXPECT_NE(p.raw->lines[0].find("kv: name=alice"), std::string::npos);

    // Strings should still be borrowed pointers.
    bool found_key = false, found_val = false;
    for (const void* ptr : p.raw->all_pointers[0]) {
        if (ptr == kv.key.data())   found_key = true;
        if (ptr == kv.value.data()) found_val = true;
    }
    EXPECT_TRUE(found_key) << "key string was not borrowed";
    EXPECT_TRUE(found_val) << "value string was not borrowed";
}

TEST(IovPack, TwoSameTypeInstancesNoClobber) {
    Probe p;
    zerocopy_user::Pair a{"first", "alpha"};
    zerocopy_user::Pair b{"second", "beta"};
    LOGGER_F(p.L, elog::Level::INFO, "{} | {}", a, b);
    ASSERT_EQ(p.raw->count, 1);
    EXPECT_NE(p.raw->lines[0].find("first=alpha | second=beta"),
              std::string::npos);

    // All four strings should be borrowed at distinct addresses.
    bool found_a_key = false, found_a_val = false;
    bool found_b_key = false, found_b_val = false;
    for (const void* ptr : p.raw->all_pointers[0]) {
        if (ptr == a.key.data())    found_a_key = true;
        if (ptr == a.value.data())  found_a_val = true;
        if (ptr == b.key.data())    found_b_key = true;
        if (ptr == b.value.data())  found_b_val = true;
    }
    EXPECT_TRUE(found_a_key && found_a_val && found_b_key && found_b_val)
        << "scratch allocator clobbered earlier render's iovs";
}
