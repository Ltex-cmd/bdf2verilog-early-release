#include "bdf.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc!=2 && argc!=3) throw std::runtime_error("Usage: benchmark-core FILE.bdf [OUTPUT.v]");
        const auto source=std::filesystem::absolute(argv[1]);
        const auto module=source.stem().string();
        using Clock=std::chrono::steady_clock;
        volatile std::size_t consumed=0;
        std::size_t output_bytes=0;
        auto batch=[&](std::size_t repetitions) {
            const auto start=Clock::now();
            for(std::size_t i=0; i<repetitions; ++i) {
                const auto output=bdf::emit_verilog_project(source.string(),module);
                output_bytes=output.size();
                consumed+=output.size();
                if (argc==3) {
                    std::ofstream stream(argv[2], std::ios::binary | std::ios::trunc);
                    stream.write(output.data(), static_cast<std::streamsize>(output.size()));
                    stream.close();
                    if (!stream) throw std::runtime_error("Could not write benchmark output");
                }
            }
            return std::chrono::duration<double>(Clock::now()-start).count();
        };
        // Warm up without retaining a parsed document. Every iteration reads,
        // parses, resolves, validates, and emits the input from its file again.
        const auto preliminary=batch(5)/5;
        const auto repetitions=static_cast<std::size_t>(std::ceil(
            std::clamp(0.15/std::max(preliminary,1e-9),1.0,20000.0)));
        std::vector<double> samples;
        for(int trial=0; trial<7; ++trial) samples.push_back(batch(repetitions)/repetitions);
        std::cout << std::setprecision(12)
                  << "{\"iterations_per_trial\":" << repetitions
                  << ",\"trials\":7,\"output_bytes\":" << output_bytes
                  << ",\"writes_output_file\":" << (argc==3 ? "true" : "false")
                  << ",\"consumed_bytes\":" << consumed
                  << ",\"per_conversion_seconds\":[";
        for(std::size_t i=0; i<samples.size(); ++i) {
            if(i) std::cout << ',';
            std::cout << samples[i];
        }
        std::cout << "]}\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
