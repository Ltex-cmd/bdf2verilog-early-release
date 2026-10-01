#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bdf {
struct Location { std::size_t line = 1, column = 1; };
struct Node {
    bool list = false, quoted = false;
    std::string value;
    std::vector<Node> children;
    Location location;
    std::string tag() const;
};
using Document = std::vector<Node>;
Document parse(const std::string& source);
std::string read_file(const std::string& path);
std::vector<const Node*> children(const Node& node, const std::string& tag);
const Node& required_child(const Node& node, const std::string& tag);

// Bit order follows each written range, including ascending ranges.
struct SignalBit {
    std::string base;
    std::optional<int> index;
    std::string key() const;
};
struct SignalTerm {
    std::string base;
    bool indexed = false;
    int left = 0, right = 0;
    std::vector<SignalBit> bits() const;
};
struct SignalName {
    std::vector<SignalTerm> terms;
    std::vector<SignalBit> bits() const;
};
SignalName parse_signal_name(const std::string& source);

struct Point {
    int x = 0, y = 0;
    bool operator<(const Point& p) const { return x < p.x || (x == p.x && y < p.y); }
    bool operator==(const Point& p) const { return x == p.x && y == p.y; }
};
struct Terminal {
    std::string name, direction;
    Point point;
    Location location;
    bool unused = false;
};
struct Parameter { std::string name, value; Location location; };
struct Symbol {
    std::string type, instance;
    std::vector<Terminal> ports;
    bool parameterized = false;
    Location location;
    std::vector<Parameter> parameters;
};
struct Connector {
    Point a, b;
    std::string label;
    bool bus = false;
    Location location;
};
struct Schematic {
    std::string version;
    std::string source_directory;
    std::vector<Terminal> pins;
    std::vector<Symbol> symbols;
    std::vector<Connector> connectors;
    std::vector<Point> junctions;
    std::map<std::string, std::size_t> root_counts;
};
Schematic decode(const Document& document);
std::string inspect_json(const Schematic& schematic);
// Supported subset is deliberately checked before emitting any HDL.
using ComponentLibrary = std::map<std::string, Schematic>;
bool is_supported_primitive(const std::string& type);
bool is_lpm(const std::string& type);
int evaluate_integer(const std::string& expression, const std::map<std::string,std::string>& parameters = {});
Symbol prepare_lpm(Symbol symbol, const std::string& source_directory);
std::string emit_verilog(const Schematic& schematic, const std::string& module,
                         const ComponentLibrary& components = {});
// Resolve BDF components beside the source and in explicitly supplied directories.
// All dependencies are validated before returning the complete Verilog document.
std::string emit_verilog_project(const std::string& path, const std::string& module,
                                 const std::vector<std::string>& libraries = {});
} // namespace bdf
