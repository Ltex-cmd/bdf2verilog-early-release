#include "bdf.hpp"
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace bdf {
namespace {
[[noreturn]] void fail(const Node& node, const std::string& message) {
    throw std::runtime_error(std::to_string(node.location.line) + ":" +
                             std::to_string(node.location.column) + ": " + message);
}
std::string atom(const Node& node, std::size_t i) {
    if (i >= node.children.size() || node.children[i].list) fail(node, "missing atom");
    return node.children[i].value;
}
int integer(const Node& node, std::size_t i) {
    auto text = atom(node, i);
    std::size_t consumed = 0;
    long long value;
    try { value = std::stoll(text, &consumed); }
    catch (const std::exception&) { fail(node, "invalid integer: " + text); }
    if (consumed != text.size() || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) fail(node, "invalid coordinate: " + text);
    return static_cast<int>(value);
}
Point point(const Node& node) {
    if (node.children.size() != 3) fail(node, "point requires two coordinates");
    return {integer(node, 1), integer(node, 2)};
}
Point absolute_point(const Node& node) {
    const auto& rect = required_child(node, "rect");
    if (rect.children.size() != 5) fail(rect, "rectangle requires four coordinates");
    const auto p = point(required_child(node, "pt"));
    const long long x = static_cast<long long>(integer(rect, 1)) + p.x;
    const long long y = static_cast<long long>(integer(rect, 2)) + p.y;
    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
        y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
        fail(node, "coordinate overflow");
    return {static_cast<int>(x), static_cast<int>(y)};
}
std::string direction(const Node& node) {
    std::string result;
    for (const auto* name : {"input", "output", "bidir"}) {
        if (!children(node, name).empty()) {
            if (!result.empty()) fail(node, "multiple port directions");
            result = name;
        }
    }
    if (result.empty()) fail(node, "missing port direction");
    return result;
}
std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
            else out << c;
        }
    }
    return out.str() + '"';
}
void terminal_json(std::ostream& out, const Terminal& t) {
    out << "{\"name\":" << quote(t.name) << ",\"direction\":" << quote(t.direction)
        << ",\"point\":[" << t.point.x << ',' << t.point.y << "]}";
}
}
Schematic decode(const Document& document) {
    Schematic result;
    bool header_seen = false;
    for (const auto& node : document) {
        if (node.tag().empty()) fail(node, "expected a tagged root list");
        ++result.root_counts[node.tag()];
        if (node.tag() == "header") {
            if (header_seen || atom(node, 1) != "graphic") fail(node, "expected one graphic header");
            header_seen = true;
            result.version = atom(required_child(node, "version"), 1);
        } else if (node.tag() == "pin") {
            auto texts = children(node, "text");
            if (texts.size() < 2) fail(node, "pin requires direction text and signal name");
            result.pins.push_back({atom(*texts[1], 1), direction(node), absolute_point(node), node.location});
        } else if (node.tag() == "symbol") {
            auto texts = children(node, "text");
            if (texts.size() < 2) fail(node, "symbol requires type and instance text");
            const auto& rect = required_child(node, "rect");
            const long long x = integer(rect, 1), y = integer(rect, 2);
            Symbol symbol{atom(*texts[0], 1), atom(*texts[1], 1), {},
                          !children(node, "parameter").empty(), node.location, {}};
            for (const auto* parameter : children(node,"parameter"))
                symbol.parameters.push_back({atom(*parameter,1),atom(*parameter,2),parameter->location});
            for (const auto* port : children(node, "port")) {
                auto labels = children(*port, "text");
                if (labels.empty()) fail(*port, "port requires a name");
                const auto p = point(required_child(*port, "pt"));
                if (x+p.x < std::numeric_limits<int>::min() || x+p.x > std::numeric_limits<int>::max() ||
                    y+p.y < std::numeric_limits<int>::min() || y+p.y > std::numeric_limits<int>::max())
                    fail(*port, "coordinate overflow");
                symbol.ports.push_back({atom(*labels[0], 1), direction(*port),
                    {static_cast<int>(x+p.x), static_cast<int>(y+p.y)}, port->location,
                    !children(*port,"unused").empty()});
            }
            result.symbols.push_back(std::move(symbol));
        } else if (node.tag() == "connector") {
            auto points = children(node, "pt");
            if (points.size() != 2) fail(node, "connector requires exactly two points");
            auto labels = children(node, "text");
            if (labels.size() > 1) fail(node, "connector has multiple labels");
            result.connectors.push_back({point(*points[0]), point(*points[1]),
                labels.empty() ? "" : atom(*labels[0], 1), !children(node, "bus").empty(), node.location});
        } else if (node.tag() == "junction") {
            result.junctions.push_back(point(required_child(node, "pt")));
        }
    }
    if (!header_seen) throw std::runtime_error("missing graphic header");
    return result;
}
std::string inspect_json(const Schematic& s) {
    std::ostringstream out;
    out << "{\"version\":" << quote(s.version) << ",\"root_counts\":{";
    bool comma = false;
    for (const auto& [name, count] : s.root_counts) {
        if (comma) out << ',';
        comma = true; out << quote(name) << ':' << count;
    }
    out << "},\"pins\":[";
    for (std::size_t i=0; i<s.pins.size(); ++i) { if (i) out << ','; terminal_json(out,s.pins[i]); }
    out << "],\"symbols\":[";
    for (std::size_t i=0; i<s.symbols.size(); ++i) {
        if (i) out << ',';
        const auto& v = s.symbols[i];
        out << "{\"type\":" << quote(v.type) << ",\"instance\":" << quote(v.instance)
            << ",\"parameterized\":" << (v.parameterized ? "true" : "false") << ",\"parameters\":[";
        for (std::size_t j=0; j<v.parameters.size(); ++j) {
            if (j) out << ',';
            out << "{\"name\":" << quote(v.parameters[j].name) << ",\"value\":" << quote(v.parameters[j].value) << '}';
        }
        out << "],\"ports\":[";
        for (std::size_t j=0; j<v.ports.size(); ++j) { if (j) out << ','; terminal_json(out,v.ports[j]); }
        out << "]}";
    }
    out << "],\"connectors\":[";
    for (std::size_t i=0; i<s.connectors.size(); ++i) {
        if (i) out << ',';
        const auto& c = s.connectors[i];
        out << "{\"a\":[" << c.a.x << ',' << c.a.y << "],\"b\":[" << c.b.x << ',' << c.b.y
            << "],\"label\":" << quote(c.label) << ",\"bus\":" << (c.bus ? "true" : "false") << '}';
    }
    out << "],\"junctions\":[";
    for (std::size_t i=0; i<s.junctions.size(); ++i) {
        if (i) out << ',';
        out << '[' << s.junctions[i].x << ',' << s.junctions[i].y << ']';
    }
    return out.str() + "]}\n";
}
} // namespace bdf
