file(READ "${INPUT}" bytes HEX)
# SPIR-V produced by glslc is little-endian; emit native uint32_t words.
string(REGEX REPLACE "([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])" "0x\\4\\3\\2\\1u,\n" words "${bytes}")
file(WRITE "${OUTPUT}" "#pragma once\n#include <cstdint>\ninline constexpr std::uint32_t ${SYMBOL}[] = {\n${words}};\n")
