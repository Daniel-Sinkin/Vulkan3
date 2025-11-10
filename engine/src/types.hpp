// engine/src/types.hpp
#include <cstddef>
#include <cstdint>

namespace DSEngine
{
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using usize = std::size_t;
using isize = std::ptrdiff_t;

using f32 = float;
using f64 = double;

using Vec3f = glm::vec3;
using Vec3d = glm::dvec3;
using Vec4f = glm::vec4;
using Vec4d = glm::dvec4;
} // namespace DSEngine