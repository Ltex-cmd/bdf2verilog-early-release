#include "bdf.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
bdf::Schematic aliases(unsigned count) {
    bdf::Schematic s;
    s.version = "1.4";
    s.root_counts["header"] = 1;
    s.pins.push_back({"A", "input", {0, 0}, {}});
    for (unsigned i = 0; i < count; ++i)
        s.pins.push_back({"Y" + std::to_string(i), "output", {0, 0}, {}});
    return s;
}
bdf::Symbol buffer(const std::string& name, bdf::Point input, bdf::Point output) {
    return {"BUF", name, {{"IN", "input", input, {}}, {"OUT", "output", output, {}}}, false, {}, {}};
}
void rejects(const bdf::Schematic& s, const std::string& expected) {
    try { (void)bdf::emit_verilog(s, "probe"); }
    catch (const std::exception& error) {
        require(error.what() == "unsupported HDL conversion: " + expected,
                "diagnostic changed: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("invalid graph accepted: " + expected);
}
int main() {
    try {
        for (unsigned count : {0u, 1u, 16u, 256u, 4096u}) {
            const auto hdl = bdf::emit_verilog(aliases(count), "probe");
            for (unsigned i = 0; i < count; ++i)
                require(hdl.find("assign Y" + std::to_string(i) + " = A;") != std::string::npos,
                        "collapsed alias changed");
        }
        auto s = aliases(1);
        s.pins.push_back({"B", "input", {0, 0}, {}});
        s.pins.push_back({"C", "input", {10, 0}, {}});
        s.pins.push_back({"D", "input", {10, 0}, {}});
        rejects(s, "multiple drivers on A");
        s = aliases(0); s.pins.clear();
        s.symbols.push_back(buffer("floating", {0, 0}, {100, 0}));
        rejects(s, "undriven input floating.IN");
        s.symbols.clear();
        s.symbols.push_back(buffer("cycle", {0, 0}, {0, 0}));
        rejects(s, "combinational feedback on bdf_net_0");
        s.symbols.clear();
        s.symbols.push_back(buffer("first", {0, 0}, {100, 0}));
        s.symbols.push_back(buffer("second", {100, 0}, {0, 0}));
        rejects(s, "combinational feedback on bdf_net_0");
        for (bool ascending : {false, true}) {
            s = aliases(1);
            s.pins[0].name = ascending ? "A[0..4095]" : "A[4095..0]";
            s.pins[1].name = ascending ? "Y[0..4095]" : "Y[4095..0]";
            const auto hdl = bdf::emit_verilog(s, "probe");
            for (unsigned i = 0; i < 4096; ++i) {
                const auto bit = std::to_string(i);
                require(hdl.find("assign Y[" + bit + "] = A[" + bit + "];") != std::string::npos,
                        "vector lane order changed");
            }
        }
        std::cout << "11 synthetic net checks passed: aliases, diagnostic order, undriven inputs, feedback and vector lanes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
