#pragma once

#include "ember/core/Reflect.h"

namespace ember::debugui {

// Draws ImGui widgets for every field of a reflected object. Returns true if anything changed.
bool inspect(void* object, const TypeInfo& type);

template <Reflected T>
bool inspect(T& object)
{
    return inspect(&object, typeOf<T>());
}

} // namespace ember::debugui
