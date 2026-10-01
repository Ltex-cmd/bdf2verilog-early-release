#pragma once
#include "bdf.hpp"
#include <cstdint>
#include <string_view>

namespace bdf::experimental {
enum class TapeKind : std::uint8_t { list, atom, quoted };
struct TapeNode {
    std::uint32_t offset=0,length=0,next=0,child_count=0,line=1,column=1;
    TapeKind kind=TapeKind::atom;
};
struct TapeDocument {
    // Spans refer into this owned buffer; escaped values use separate storage.
    std::string source;
    std::vector<TapeNode> nodes;
    std::vector<std::string> escaped;
    std::string_view text(std::uint32_t index) const;
    std::string_view tag(std::uint32_t index) const;
};
TapeDocument parse_tape(std::string source);
TapeDocument parse_span_tape(std::string source);
Schematic decode_tape(const TapeDocument& document);
Schematic load_tape(const std::string& path);
} // namespace bdf::experimental
