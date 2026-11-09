#include "ember/core/Guid.h"

#include <mutex>
#include <random>

namespace ember {

Guid Guid::generate()
{
    static std::mutex mutex;
    static std::mt19937_64 rng{std::random_device{}() ^ (static_cast<u64>(std::random_device{}()) << 32)};
    std::lock_guard lock(mutex);
    Guid g{rng(), rng()};
    return g.isValid() ? g : generate();
}

std::optional<Guid> Guid::parse(std::string_view text)
{
    Guid g;
    int digits = 0;
    for (char c : text) {
        if (c == '-') {
            continue;
        }
        u64 value = 0;
        if (c >= '0' && c <= '9') {
            value = static_cast<u64>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value = static_cast<u64>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value = static_cast<u64>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        if (digits >= 32) {
            return std::nullopt;
        }
        u64& half = digits < 16 ? g.high : g.low;
        half = (half << 4) | value;
        ++digits;
    }
    if (digits != 32) {
        return std::nullopt;
    }
    return g;
}

std::string Guid::toString() const
{
    static const char* hex = "0123456789abcdef";
    std::string out(32, '0');
    for (int i = 0; i < 16; ++i) {
        out[static_cast<usize>(i)] = hex[(high >> (60 - 4 * i)) & 0xf];
        out[static_cast<usize>(16 + i)] = hex[(low >> (60 - 4 * i)) & 0xf];
    }
    return out;
}

} // namespace ember
