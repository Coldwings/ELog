# Logging custom types

There are two extension points, picked by what the type contains.

| Pattern | Use when | Cost model |
|---|---|---|
| **`elog_render` ADL** | All members render to text (numbers, enums, fixed strings) | Single contiguous Iov; bytes go through scratch |
| **`iov_pack`** | The value contains `std::string` / `string_ref` / external buffers you want to pass zero-copy | Multiple Iov entries; strings flow as borrowed pointers straight into `writev` |

These patterns are not mutually exclusive — you can offer both for the
same type. Choose based on the call site.

---

## Pattern 1: `elog_render` ADL hook

Provide a free function `elog_render` in your type's namespace. Argument
deduction lookup finds it automatically.

```cpp
namespace geom {
struct Vec3 { double x, y, z; };

inline elog::Iov elog_render(char* scratch, std::size_t& pos,
                             const Vec3& v) noexcept {
    using elog::elog_render;          // bring built-in overloads in scope
    std::size_t start = pos;
    if (pos < 4096) scratch[pos++] = '<';
    (void)elog_render(scratch, pos, elog::fixed(v.x, 2));
    if (pos + 1 < 4096) { scratch[pos++] = ','; scratch[pos++] = ' '; }
    (void)elog_render(scratch, pos, elog::fixed(v.y, 2));
    if (pos + 1 < 4096) { scratch[pos++] = ','; scratch[pos++] = ' '; }
    (void)elog_render(scratch, pos, elog::fixed(v.z, 2));
    if (pos < 4096) scratch[pos++] = '>';
    return elog::Iov{scratch + start, pos - start};
}
}  // namespace geom

LOG_INFO_F("at = {}", geom::Vec3{1.0, 2.0, 3.0});
// at = <1.00, 2.00, 3.00>
```

### Contract

```cpp
elog::Iov elog_render(char* scratch, std::size_t& pos, const T& v) noexcept;
```

- `scratch` is the calling thread's 4 KiB scratch buffer (one per thread).
- `pos` is a write cursor into `scratch`. Advance it to reflect bytes you
  wrote. Never write past 4096.
- Return an `Iov` describing the byte range you produced. For numeric
  output that's `{scratch + old_pos, pos - old_pos}`.

### Composing built-in renderers

Inside your overload you call `elog_render(scratch, pos, sub)` for each
member. The `using elog::elog_render;` brings in built-in overloads so
unqualified lookup finds them.

For numeric/literal members, the inner call will write into scratch and
update `pos` — your end Iov simply spans `[start, pos)`.

For string members, the inner call returns an Iov that points at the
**source** (e.g. `std::string::data()`), **not** at scratch. If you ignore
this and just use `Iov{scratch + start, pos - start}`, the string content
is silently dropped from your output. Two ways to handle it:

- **Manual memcpy into scratch** — preserves single-Iov return at the cost
  of losing zero-copy on the string.
- **Use `iov_pack` instead** — see Pattern 2.

### Lifetime

The returned Iov is consumed by the synchronous sinks before `LOG_*_F`
returns. If you point at borrowed source data (a `std::string::data()`,
a config buffer, anything not in `scratch`), make sure that data outlives
the call expression — that's the same guarantee that built-in string
renderers rely on.

---

## Pattern 2: `iov_pack` for composite zero-copy

When the value carries strings or external buffers you want to ship to
`writev` without an intermediate copy, build a list of `Iov` and wrap it
with `elog::iov_pack`. ELog detects the wrapper at compile time and
splices its entries directly into the per-call iovec.

```cpp
namespace audit {
struct UserRecord {
    std::string name;
    std::string ip;

    void as_iov(std::vector<elog::Iov>& out) const {
        out.push_back({"name=", 5});
        out.push_back({name.data(), name.size()});   // borrowed
        out.push_back({" ip=", 4});
        out.push_back({ip.data(), ip.size()});       // borrowed
    }
};
}

audit::UserRecord r{"alice", "10.0.0.1"};
std::vector<elog::Iov> pieces;
pieces.reserve(8);
r.as_iov(pieces);
LOG_INFO_F("user: {}", elog::iov_pack(pieces));
// user: name=alice ip=10.0.0.1
```

### Constructors

```cpp
elog::iov_pack(const Iov*, std::size_t n);
elog::iov_pack(const std::vector<Iov>&);
elog::iov_pack(const std::array<Iov, N>&);
elog::iov_pack(const Iov (&)[N]);
```

The pack does **not** own. The underlying iov entries (and whatever they
point at) must outlive the LOG call.

### Compile-time dispatch

Whether a LOG_*_F call uses the pack code path is determined entirely at
compile time. If none of the arguments is `iov_pack`, you get the original
fast path with the iovec array sized at compile time and no extra overhead
of any kind. If at least one argument is a pack, ELog's `emit_f` switches
to a slightly larger iovec array (32 entries per pack max) and a runtime
expansion loop. So you can adopt `iov_pack` for some call sites without
slowing down the rest.

### When to prefer this over Pattern 1

- The type has `std::string` or `string_ref` members and you want
  zero-copy.
- The number of segments is dynamic (e.g. you append iov entries in a
  loop based on data shape).
- You're at the boundary between an existing iovec-style API (e.g.
  `iovec[]` from elsewhere) and the logger.

For pure-numeric types Pattern 1 is simpler. The two coexist fine.

### Limits

- Up to 32 iov entries per `iov_pack` per LOG call. Larger packs are
  silently truncated. If you genuinely need more, split the LOG call.
- The pack itself must be a fully-formed range at the LOG site — its
  storage must already be filled.

---

## Choosing between the two

```cpp
// All-numeric Vec3 — render-to-scratch is the right pattern.
LOG_INFO_F("at = {}", geom::Vec3{1.0, 2.0, 3.0});

// String-bearing record — iov_pack keeps name and ip zero-copy.
LOG_INFO_F("user: {}", elog::iov_pack(pieces));

// Mixed — both patterns in the same call.
LOG_INFO_F("op n={} record: {}", count, elog::iov_pack(pieces));
```
