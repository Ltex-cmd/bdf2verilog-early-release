#include "bdf_tape.hpp"
#include "bdf.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: bdf-tool inspect FILE.bdf\n"
                      << "       bdf-tool verilog FILE.bdf [MODULE_NAME] [--library DIRECTORY ...]\n";
            return 2;
        }
        const std::string command = argv[1];
        if (command != "inspect" && command != "verilog") throw std::runtime_error("unknown command: " + command);
        if (command == "inspect") {
            if (argc!=3) throw std::runtime_error("inspect requires exactly one BDF file");
            std::cout << bdf::inspect_json(bdf::experimental::load_tape(argv[2]));
        } else {
            std::string module=std::filesystem::path(argv[2]).stem().string();
            bool named=false;
            std::vector<std::string> libraries;
            for (int i=3; i<argc; ++i) {
                const std::string argument=argv[i];
                if (argument=="--library") {
                    if (++i==argc) throw std::runtime_error("--library requires a directory");
                    libraries.emplace_back(argv[i]);
                } else if (!named && argument.rfind("--",0)!=0) { module=argument; named=true; }
                else throw std::runtime_error("unexpected argument: " + argument);
            }
            std::cout << bdf::emit_verilog_project(argv[2],module,libraries);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
