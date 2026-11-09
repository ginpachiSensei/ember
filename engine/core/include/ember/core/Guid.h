#pragma once

// 128-bit globally unique id. Assets and scene entities are referenced by Guid, never by file path
// or pointer, so files can be moved or renamed without breaking references.

#include "ember/core/Types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ember {

struct Guid {
    u64 high = 0;
    u64 low = 0;

    static Guid generate(); // random (version 4 style)
    static std::optional<Guid> parse(std::string_view text); // 32 hex digits, dashes allowed
    std::string toString() const;                             // 32 lowercase hex digits

    bool isValid() const { return high != 0 || low != 0; }
    friend bool operator==(const Guid&, const Guid&) = default;
    friend auto operator<=>(const Guid&, const Guid&) = default;
};

// FNV-1a: a tiny, decent 64-bit hash for content fingerprints and string ids.
inline u64 hash64(const void* data, usize size, u64 seed = 0xcbf29ce484222325ull)
{
    const auto* bytes = static_cast<const u8*>(data);
    u64 h = seed;
    for (usize i = 0; i < size; ++i) {
        h = (h ^ bytes[i]) * 0x100000001b3ull;
    }
    return h;
}

inline u64 hash64(std::string_view text)
{
    return hash64(text.data(), text.size());
}

} // namespace ember

template <>
struct std::hash<ember::Guid> {
    size_t operator()(const ember::Guid& g) const noexcept { return static_cast<size_t>(g.high ^ (g.low * 0x9e3779b97f4a7c15ull)); }
};
