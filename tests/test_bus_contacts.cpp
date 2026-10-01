#include "bdf.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bdf::Connector bus(bdf::Point a, bdf::Point b, const std::string& label) {
    return {a, b, label, true, {}};
}

// Input a sits at the probe point; output y sits on the far end of the last
// bus connector. y is assigned from a only when a is classified as a bus contact.
bdf::Schematic probe(bdf::Point a, const std::vector<bdf::Connector>& connectors) {
    bdf::Schematic s;
    s.version = "1.4";
    s.pins = {{"a", "input", a, {}, false}, {"y", "output", connectors.back().b, {}, false}};
    s.connectors = connectors;
    return s;
}

bool joined(const bdf::Schematic& s) {
    return bdf::emit_verilog(s, "probe").find("assign y = a;\n") != std::string::npos;
}

std::string error(const bdf::Schematic& s) {
    try { bdf::emit_verilog(s, "probe"); }
    catch (const std::exception& e) { return e.what(); }
    return "";
}

void contact_geometry() {
    require(joined(probe({50, 0}, {bus({0, 0}, {100, 0}, "n")})), "horizontal interior");
    require(joined(probe({0, 50}, {bus({0, 0}, {0, 100}, "n")})), "vertical interior");
    require(joined(probe({0, 0}, {bus({0, 0}, {100, 100}, "n")})), "diagonal endpoint");
    require(!joined(probe({50, 50}, {bus({0, 0}, {100, 100}, "n")})), "diagonal interior");
    require(!joined(probe({150, 0}, {bus({0, 0}, {100, 0}, "n")})), "outside horizontal range");
    require(joined(probe({50, 0}, {bus({100, 0}, {0, 0}, "n")})), "reversed endpoints");
}

void contact_labels() {
    // An unlabeled or multi-bit label does not classify a scalar term; a later
    // touching connector with a one-bit label still does.
    require(error(probe({50, 0}, {bus({0, 0}, {100, 0}, "")})) ==
            "unsupported HDL conversion: unnamed bus without a vector port", "unlabeled contact");
    require(joined(probe({50, 0}, {bus({50, 0}, {50, 40}, ""), bus({0, 0}, {100, 0}, "n")})),
            "later one-bit label");
}

void diagnostic_order() {
    auto duplicate = [](bdf::Schematic s) {
        const bdf::Symbol gate{"NOT", "g", {{"IN", "input", {300, 8}, {}, false},
                                            {"OUT", "output", {340, 8}, {}, false}}, false, {}, {}};
        s.symbols = {gate, gate};
        return s;
    };
    // A touching bus label is parsed while pins are classified, before symbols.
    auto touching = duplicate(probe({50, 0}, {bus({0, 0}, {100, 0}, "1bad"), bus({0, 0}, {100, 0}, "n")}));
    require(error(touching) == "unsupported signal name: 1bad", "touching label: " + error(touching));
    // Connector order decides across orientations: the earlier row is tested
    // before the later column, even though the column is a match.
    auto ordered = duplicate(probe({50, 0}, {bus({0, 0}, {100, 0}, "1bad"), bus({50, 40}, {50, 0}, "n")}));
    require(error(ordered) == "unsupported signal name: 1bad", "connector order: " + error(ordered));
    // A label no terminal touches is parsed only after the symbol checks.
    auto distant = duplicate(probe({50, 0}, {bus({0, 90}, {100, 90}, "1bad"), bus({0, 0}, {100, 0}, "n")}));
    require(error(distant) == "unsupported HDL conversion: duplicate instance g", "distant label: " + error(distant));
}

// Many terminals on one row shared by many bus connectors. The first connector
// matches every terminal, so later labels in the same row are not parsed while
// pins are classified.
void dense_row() {
    bdf::Schematic s;
    s.version = "1.4";
    const int terminals = 1000, connectors = 4000;
    s.pins.push_back({"a", "input", {0, 0}, {}, false});
    for (int k = 1; k <= terminals; ++k) s.pins.push_back({"y" + std::to_string(k), "output", {10 * k, 0}, {}, false});
    for (int i = 0; i < connectors; ++i) s.connectors.push_back(bus({-10, 0}, {10 * terminals + 10 + i, 0}, "n"));
    const auto hdl = bdf::emit_verilog(s, "dense");
    require(hdl.find("assign y1 = a;\n") != std::string::npos, "dense first output");
    require(hdl.find("assign y1000 = a;\n") != std::string::npos, "dense last output");
    for (int i = 1; i < connectors; ++i) s.connectors[i].label = "1bad";
    const bdf::Symbol gate{"NOT", "g", {{"IN", "input", {0, 50}, {}, false},
                                        {"OUT", "output", {40, 50}, {}, false}}, false, {}, {}};
    s.symbols = {gate, gate};
    require(error(s) == "unsupported HDL conversion: duplicate instance g", "dense early hit: " + error(s));
}

// Many component instances, each with scalar ports beside labeled bus rows.
void hierarchy_scale() {
    bdf::Schematic stage;
    stage.version = "1.4";
    stage.pins = {{"A", "input", {0, 8}, {}, false}, {"Y", "output", {40, 8}, {}, false}};
    stage.symbols = {{"BUF", "g", {{"IN", "input", {0, 8}, {}, false},
                                   {"OUT", "output", {40, 8}, {}, false}}, false, {}, {}}};
    bdf::Schematic top;
    top.version = "1.4";
    const int count = 2000;
    top.pins = {{"a", "input", {-20, 8}, {}, false}, {"y", "output", {100 * count, 8}, {}, false}};
    top.connectors.push_back({{-20, 8}, {0, 8}, "", false, {}});
    for (int i = 0; i < count; ++i) {
        const int x = 100 * i;
        top.symbols.push_back({"stage", "u" + std::to_string(i),
                               {{"A", "input", {x, 8}, {}, false}, {"Y", "output", {x + 40, 8}, {}, false}},
                               false, {}, {}});
        top.connectors.push_back({{x + 40, 8}, {x + 100, 8}, "", false, {}});
        top.connectors.push_back(bus({x, 100}, {x + 50, 100}, "d[3..0]"));
    }
    const auto hdl = bdf::emit_verilog(top, "top", {{"stage", stage}});
    require(hdl.find("stage bdf_component_u0 (\n    .A(a),") != std::string::npos, "first instance");
    require(hdl.find("stage bdf_component_u1999 (") != std::string::npos, "last instance");
}

int main() {
    try {
        contact_geometry();
        contact_labels();
        diagnostic_order();
        dense_row();
        hierarchy_scale();
        std::cout << "Bus contact checks passed: geometry, labels, diagnostic order, dense row, 2000-instance hierarchy.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
