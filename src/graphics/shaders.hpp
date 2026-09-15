#pragma once

#include <cstddef>
#include <cstdint>

// Defined in the generated shader translation units; sizes are in bytes.
extern const std::uint32_t cube_vert_spirv[];
extern const std::size_t cube_vert_spirv_size;
extern const std::uint32_t cube_frag_spirv[];
extern const std::size_t cube_frag_spirv_size;

extern const std::uint32_t advect_comp_spirv[];
extern const std::size_t advect_comp_spirv_size;

extern const std::uint32_t divergence_comp_spirv[];
extern const std::size_t divergence_comp_spirv_size;

extern const std::uint32_t pressure_comp_spirv[];
extern const std::size_t pressure_comp_spirv_size;

extern const std::uint32_t pressure_reduce_comp_spirv[];
extern const std::size_t pressure_reduce_comp_spirv_size;

extern const std::uint32_t pressure_statistics_comp_spirv[];
extern const std::size_t pressure_statistics_comp_spirv_size;

extern const std::uint32_t pressure_normalize_comp_spirv[];
extern const std::size_t pressure_normalize_comp_spirv_size;
