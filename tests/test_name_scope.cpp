#include "bdf.hpp"
#include "project_context.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string wire() {
    return "(header \"graphic\" (version \"1.4\"))"
           "(pin(input)(rect 0 0 168 16)(text \"INPUT\")(text \"A\")(pt 0 8))"
           "(pin(output)(rect 120 0 288 16)(text \"OUTPUT\")(text \"Y\")(pt 0 8))"
           "(connector(pt 0 8)(pt 120 8))";
}

std::string wrapper(const std::string& type) {
    auto source = wire();
    source.erase(source.find("(connector"));
    return source + "(symbol(rect 40 0 80 16)(text \"" + type + "\")(text \"dut\")"
           "(port(input)(pt 0 8)(text \"A\"))(port(output)(pt 40 8)(text \"Y\")))"
           "(connector(pt 0 8)(pt 40 8))(connector(pt 80 8)(pt 120 8))";
}

std::string inverter() {
    auto source = wire();
    source.erase(source.find("(connector"));
    return source + "(symbol(rect 40 0 80 16)(text \"NOT\")(text \"gate\")"
           "(port(input)(pt 0 8)(text \"IN\"))(port(output)(pt 40 8)(text \"OUT\")))"
           "(connector(pt 0 8)(pt 40 8))(connector(pt 80 8)(pt 120 8))";
}

void write(const fs::path& path, const std::string& source) {
    std::ofstream out(path, std::ios::binary);
    out << source;
    out.close();
    require(bool(out), "fixture write failed");
}

// Public emit boundary: two children share A/Y port names but must not share
// their local port bits with each other or with same-named parent signals.
void sibling_port_scope() {
    auto child = bdf::decode(bdf::parse(wire()));
    bdf::Schematic parent;
    parent.version = "1.4";
    parent.pins = {{"A", "input", {0, 0}, {}, false}, {"X", "input", {0, 20}, {}, false},
                   {"Y", "output", {100, 0}, {}, false}, {"Z", "output", {100, 20}, {}, false}};
    parent.symbols = {
        {"Leaf", "first", {{"A", "input", {0, 20}, {}, false}, {"Y", "output", {100, 0}, {}, false}},
         false, {}, {}},
        {"Leaf", "second", {{"A", "input", {0, 0}, {}, false}, {"Y", "output", {100, 20}, {}, false}},
         false, {}, {}}
    };
    const auto hdl = bdf::emit_verilog(parent, "probe", {{"Leaf", child}});
    require(hdl.find("Leaf bdf_component_first (\n    .A(X),\n    .Y(Y)") != std::string::npos,
            "first child's ports escaped their instance scope");
    require(hdl.find("Leaf bdf_component_second (\n    .A(A),\n    .Y(Z)") != std::string::npos,
            "second child's ports escaped their instance scope");
}

// Public project boundary: documented source-first, ordered-library lookup
// must remain case-folded for filenames without changing Verilog spelling.
void ordered_component_lookup(const fs::path& directory) {
    const auto root = directory / "project", lib1 = directory / "lib1", lib2 = directory / "lib2";
    fs::create_directories(root); fs::create_directory(lib1); fs::create_directory(lib2);
    write(root / "top.bdf", wrapper("LeAf"));
    write(root / "leaf.bdf", wire());
    write(lib1 / "LEAF.bdf", inverter());
    write(lib2 / "leaf.bdf", wire());
    const auto source = (root / "top.bdf").string();
    const auto check_spelling = [](const std::string& hdl) {
        require(hdl.find("module LeAf(") != std::string::npos,
                "case-folded filename lookup changed Verilog module spelling");
        require(hdl.find(".A(A)") != std::string::npos && hdl.find(".Y(Y)") != std::string::npos,
                "case-folded component lookup changed the child port spelling");
    };
    auto hdl = bdf::emit_verilog_project(source, "probe", {lib1.string(), lib2.string()});
    check_spelling(hdl);
    require(hdl.find("assign Y = A;") != std::string::npos && hdl.find("~(A)") == std::string::npos,
            "library component overrode the source-directory definition");
    fs::remove(root / "leaf.bdf");
    hdl = bdf::emit_verilog_project(source, "probe", {lib1.string(), lib2.string()});
    check_spelling(hdl);
    require(hdl.find("assign Y = ~(A);") != std::string::npos,
            "first explicit library lost lookup precedence");
    hdl = bdf::emit_verilog_project(source, "probe", {lib2.string(), lib1.string()});
    check_spelling(hdl);
    require(hdl.find("assign Y = A;") != std::string::npos && hdl.find("~(A)") == std::string::npos,
            "reversing explicit library order did not change the selected definition");
}

// Original, temporary one-bit ROM fixture. This restores the portable
// memory-path invalidation check without importing any external corpus.
void memory_path_recovery(const fs::path& directory) {
    const auto root = directory / "memory";
    fs::create_directory(root);
    const auto source = root / "memory_probe.bdf", memory = root / "probe.mif";
    write(source,
        "(header \"graphic\"(version \"1.4\"))"
        "(pin(input)(rect 0 0 168 16)(text \"INPUT\")(text \"A[0..0]\")(pt 0 8))"
        "(pin(output)(rect 120 0 288 16)(text \"OUTPUT\")(text \"Q[0..0]\")(pt 0 8))"
        "(symbol(rect 40 0 80 16)(text \"LPM_ROM\")(text \"memory\")"
        "(parameter \"LPM_WIDTH\" \"1\")(parameter \"LPM_WIDTHAD\" \"1\")"
        "(parameter \"LPM_ADDRESS_CONTROL\" \"\\\"UNREGISTERED\\\"\")"
        "(parameter \"LPM_OUTDATA\" \"\\\"UNREGISTERED\\\"\")"
        "(parameter \"LPM_FILE\" \"\\\"probe.mif\\\"\")"
        "(port(input)(pt 0 8)(text \"address[0..0]\"))"
        "(port(output)(pt 40 8)(text \"q[0..0]\")))"
        "(connector(pt 0 8)(pt 40 8))(connector(pt 80 8)(pt 120 8))");
    const std::string contents = "WIDTH=1;\nDEPTH=2;\nADDRESS_RADIX=UNS;\n"
        "DATA_RADIX=BIN;\nCONTENT BEGIN\n0 : 0;\n1 : 1;\nEND;\n";
    write(memory, contents);
    bdf::experimental::ProjectContext context(root.string());
    const auto baseline = context.convert(source.string(), "memory_probe");
    fs::remove(memory);
    bool refresh_rejected = false, conversion_rejected = false;
    try { context.refresh(); }
    catch (const std::exception& error) {
        refresh_rejected = std::string(error.what()).find("memory file not found") != std::string::npos;
    }
    try { (void)context.convert(source.string(), "memory_probe"); }
    catch (const std::exception& error) {
        conversion_rejected = std::string(error.what()).find("project refresh failed") != std::string::npos;
    }
    require(refresh_rejected, "refresh did not identify the deleted memory file");
    require(conversion_rejected, "failed refresh returned cached HDL after memory deletion");
    write(memory, contents);
    context.refresh();
    require(context.convert(source.string(), "memory_probe") == baseline,
            "memory-file restoration did not recover the cached design");
}

int main() {
    const auto directory = fs::temp_directory_path() /
        ("bdf-qa-scope-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(directory);
        sibling_port_scope();
        ordered_component_lookup(directory);
        memory_path_recovery(directory);
        fs::remove_all(directory);
        std::cout << "Compatibility checks passed: sibling port isolation; case-folded source/library priority; "
                     "memory-path deletion/recovery.\n";
    } catch (const std::exception& error) {
        fs::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
