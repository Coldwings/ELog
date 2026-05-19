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

## Pattern 2: `elog_render` returning `iov_pack`

For composite values that carry `std::string` / `string_ref` / borrowed
buffers, return `iov_pack` from `elog_render` instead of `Iov`. ELog
detects the return type at compile time (SFINAE) and routes the type
through the multi-iov path. Strings flow as borrowed iovec entries
straight into `writev`.

```cpp
namespace audit {
struct UserRecord {
    std::string name;
    std::string ip;
};

inline elog::iov_pack elog_render(char* /*scratch*/, std::size_t& /*pos*/,
                                  const UserRecord& r) noexcept {
    static thread_local elog::Iov buf[4];
    buf[0] = {"name=", 5};
    buf[1] = {r.name.data(), r.name.size()};       // borrowed (zero-copy)
    buf[2] = {" ip=", 4};
    buf[3] = {r.ip.data(), r.ip.size()};           // borrowed (zero-copy)
    return elog::iov_pack(buf, 4);
}
}  // namespace audit

audit::UserRecord r{"alice", "10.0.0.1"};
LOG_INFO_F("user: {}", r);
// user: name=alice ip=10.0.0.1
```

The signature is the same as Pattern 1 except the return type. The same
`scratch` and `pos` are still available — you can mix render-to-scratch
fields with borrowed pointers in one pack:

```cpp
inline elog::iov_pack elog_render(char* scratch, std::size_t& pos,
                                  const Item& it) noexcept {
    using elog::elog_render;
    static thread_local elog::Iov buf[5];

    // Numeric field: render into scratch, take that segment.
    std::size_t old = pos;
    elog_render(scratch, pos, it.id);              // writes into scratch
    buf[0] = elog::Iov{"id=", 3};
    buf[1] = elog::Iov{scratch + old, pos - old};  // pointer into scratch

    // String field: borrow the source.
    buf[2] = elog::Iov{" name=", 6};
    buf[3] = elog::Iov{it.name.data(), it.name.size()};
    buf[4] = elog::Iov{" }", 2};
    return elog::iov_pack(buf, 5);
}
```

### Backing-storage lifetime

The `iov_pack` returned does not own — it's a `{base, count}` view. The
storage `base` points at must remain valid until `LOG_*_F` returns. The
common patterns:

- **Per-type `static thread_local` array** (shown above). Each custom
  type uses its own buffer, so multiple distinct pack-returning types
  in one LOG call don't collide. Two instances of the *same* type in
  one LOG call DO collide — see "Limits" below.
- **Function-local stack array, returned via the pack** — fine if the
  function inlines and the array lives on the caller's frame, which gcc
  and clang handle correctly when the renderer is `inline`.
- **A member buffer of the value being logged** — works if the value
  itself outlives the LOG call.

The `scratch` buffer is also valid for the duration of the LOG, so any
segment that points into `scratch` is safe.

### When to prefer Pattern 2

- Any value with `std::string` / `string_ref` / external pointer members
  you want zero-copy.
- Mixed structures where some fields render to text and some are
  borrowed — Pattern 2 lets you mix in one pack.
- Types where you'd otherwise have to manually `memcpy` borrowed strings
  into `scratch` to fit a single-Iov return.

For pure-numeric types Pattern 1 is simpler and equally fast.

## Direct `iov_pack` argument

If you already have a `std::vector<Iov>` (or `std::array<Iov, N>`, or a C
array of `Iov`) at the call site, you can pass it directly without
defining a custom renderer:

```cpp
std::vector<elog::Iov> pieces;
pieces.push_back({"key=", 4});
pieces.push_back({k.data(), k.size()});
pieces.push_back({" val=", 5});
pieces.push_back({v.data(), v.size()});
LOG_INFO_F("entry: {}", elog::iov_pack(pieces));
```

Useful at boundaries with existing iovec-shaped APIs.

## Constructors of `iov_pack`

```cpp
elog::iov_pack(const Iov*, std::size_t n);
elog::iov_pack(const std::vector<Iov>&);
elog::iov_pack(const std::array<Iov, N>&);
elog::iov_pack(const Iov (&)[N]);
```

## Compile-time dispatch and overhead

Whether a `LOG_*_F` call uses the pack code path is determined entirely
at compile time. ELog inspects each arg:

- Static type is `iov_pack` → pack path
- `elog_render(...)` for the static type returns `iov_pack` → pack path
- Otherwise → the simple Iov path

If **no** arg falls into either pack category, you get the original
fully-unrolled fast path. The extension is zero-cost for code that
doesn't use it.

If at least one arg is pack-typed, `emit_f` switches to a slightly
larger iovec array (32 entries per pack max) and a runtime expansion
loop. The overhead is still small (a few ns).

## Limits

- Up to 32 iov entries per `iov_pack` per LOG call. Larger packs are
  silently truncated.
- A `static thread_local` backing buffer is **per-type, not per-instance**.
  If you log two values of the same custom type in one `LOG_*_F`, the
  second call overwrites the first's buffer before `emit_f` reads it.
  Use a function-local buffer (returned via `iov_pack`) or split into
  two LOG calls if you need this.
- The pack itself must be fully formed at the LOG site (or by the time
  `elog_render` returns) — `emit_f` reads it synchronously.

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
