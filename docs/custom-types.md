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
    elog::Iov* buf = elog::iov_scratch_alloc(4);  // ELog-managed scratch
    buf[0] = {"name=", 5};
    buf[1] = {r.name.data(), r.name.size()};      // borrowed (zero-copy)
    buf[2] = {" ip=", 4};
    buf[3] = {r.ip.data(), r.ip.size()};          // borrowed (zero-copy)
    return elog::iov_pack(buf, 4);
}
}  // namespace audit

audit::UserRecord r{"alice", "10.0.0.1"};
LOG_INFO_F("user: {}", r);
// user: name=alice ip=10.0.0.1
```

### Mixing strings, numerics, and built-in formatters

Most real composite types contain a mix of:

- string fields you want **zero-copy**
- numeric fields that need **formatting** (precision, width, hex, ...)
- literal separators (`"="`, `", "`, `" {"`, ...)

The natural way is to call `elog_render` for each field — it returns the
right `Iov` regardless of where the bytes live — and store the returned
Iovs into the iov_pack buffer.

```cpp
namespace trading {
struct Trade {
    std::string symbol;
    double      price;
    int         volume;
    bool        is_buy;
};

inline elog::iov_pack elog_render(char* scratch, std::size_t& pos,
                                  const Trade& t) noexcept {
    using elog::elog_render;          // bring built-in overloads into scope
    elog::Iov* buf = elog::iov_scratch_alloc(8);

    buf[0] = {"symbol=", 7};                                       // .rodata
    buf[1] = elog_render(scratch, pos, t.symbol);                  // borrowed
    buf[2] = {" price=", 7};
    buf[3] = elog_render(scratch, pos, elog::fixed(t.price, 4));   // into scratch
    buf[4] = {" vol=", 5};
    buf[5] = elog_render(scratch, pos, t.volume);                  // into scratch
    buf[6] = {" side=", 6};
    buf[7] = elog_render(scratch, pos, t.is_buy ? "BUY" : "SELL"); // .rodata

    return elog::iov_pack(buf, 8);
}
}  // namespace trading

trading::Trade t{"AAPL", 150.25, 1000, true};
LOG_INFO_F("trade: {}", t);
// trade: symbol=AAPL price=150.2500 vol=1000 side=BUY
```

#### How `elog_render`'s return value covers all three sources

The `Iov` returned by a built-in `elog_render` already encodes where the
bytes live:

| Argument type | What `elog_render` does | Returned `Iov.base` points at |
|---|---|---|
| `int`, `unsigned`, `long`, ... | writes ASCII digits into `scratch + pos`, advances `pos` | `scratch + old_pos` |
| `double`, `elog::fixed(d, prec)`, `elog::hex`, `elog::bin` | renders into scratch | `scratch + old_pos` |
| `bool` | returns `Iov{"true",4}` or `Iov{"false",5}` | `.rodata` |
| `const char*`, `char[N]` | returns `Iov{ptr, len}` | source / `.rodata` |
| `std::string`, `string_ref` | returns `Iov{data, size}` | source memory |
| custom type with `Iov`-returning renderer | the user's logic, typically into scratch | typically `scratch + old_pos` |

So you can chain `elog_render(scratch, pos, ...)` calls — each one's
returned `Iov` is exactly the segment to put into the pack. `scratch` and
`pos` accumulate naturally across the calls. The pattern is uniform and
copy-paste-friendly.

#### Mixing wrappers

```cpp
buf[N] = elog_render(scratch, pos, elog::pad_left(value, 8, '0'));
buf[N] = elog_render(scratch, pos, elog::hex(addr));
buf[N] = elog_render(scratch, pos, elog::quoted(text));
```

Wrappers like `pad_left`, `hex`, `bin`, `oct`, `fixed`, `escaped`,
`quoted`, `hexdump` all render into `scratch` and return an `Iov` you
can drop into the pack.

### Backing-storage lifetime

The `iov_pack` returned does not own — it's a `{base, count}` view. The
storage `base` points at must remain valid until `LOG_*_F` returns.

**Recommended:** allocate from `elog::iov_scratch_alloc(n)` (shown above).
This carves a range out of the per-LOG-call scratch buffer that emit_f
sets up. The returned pointer is stable for the entire LOG call, and each
call to `iov_scratch_alloc` returns a fresh range — so logging two
instances of the same type in one `LOG_*_F` is safe, the second render's
iovs do not clobber the first's. Returns `nullptr` if called outside an
emit_f context or if scratch is exhausted (capacity is `kPackMax` per
arg, default 32).

Other valid storage choices:

- **A member buffer of the value being logged** — works if the value
  itself outlives the LOG call.
- **The byte `scratch` buffer** is also valid for the duration of the
  LOG — any iov segment that points into `scratch` is safe. This lets
  you mix borrowed string members and rendered numeric members in one
  pack (see "Mixing rendered and borrowed segments" below).

**Avoid:**

- Function-local stack arrays returned via `iov_pack` — the array dies
  when the renderer returns and emit_f reads from it later. Undefined
  behavior, even though it sometimes appears to work due to stack reuse.
- `static thread_local Iov[]` per-type — the storage is shared across
  all instances of the same type, so two same-type values in one LOG
  call clobber each other. `iov_scratch_alloc` exists specifically to
  avoid this.

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

- Up to 32 iov entries per `iov_pack` per arg. Larger packs are silently
  truncated.
- The aggregate iov-scratch capacity is `32 × number-of-args` Iov slots
  per LOG call. Practically unbounded for typical structured logs.
- The pack itself must be fully formed by the time `elog_render` returns
  — `emit_f` reads it synchronously when assembling the iovec.
- `iov_scratch_alloc(n)` returns `nullptr` if called outside an emit_f
  (e.g. from a unit test that calls `elog_render` directly without going
  through `LOG_*_F`). For testing isolated renderers, supply your own
  buffer.

---

## Choosing between Pattern 1 and Pattern 2

```cpp
// All-numeric type — Pattern 1 (Iov return into scratch) is simplest.
LOG_INFO_F("at = {}", geom::Vec3{1.0, 2.0, 3.0});

// String-bearing record — Pattern 2 (iov_pack return) keeps strings zero-copy.
LOG_INFO_F("user: {}", record);

// Mixed numeric + string + literals in one type — Pattern 2 plus the
// mixing technique above (chain elog_render calls).
LOG_INFO_F("trade: {}", trade);

// Multiple types, some pack-returning some not — combine freely.
LOG_INFO_F("count={} record: {}", count, record);
```
