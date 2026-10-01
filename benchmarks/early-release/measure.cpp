#include "bdf.hpp"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
// This driver uses the public fresh-conversion API. It retains no context.
int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("usage: converter-measure FILE REPETITIONS WARMUPS");
        const auto path=std::filesystem::absolute(argv[1]);
        const auto repetitions=std::stoull(argv[2]), warmups=std::stoull(argv[3]);
        if (!repetitions || repetitions>100000 || warmups>100000) throw std::runtime_error("invalid count");
        const auto module=path.stem().string();
        std::size_t bytes=0;
        volatile std::size_t consumed=0;
        auto convert=[&] {auto output=bdf::emit_verilog_project(path.string(),module);bytes=output.size();consumed+=bytes;};
        for (std::size_t i=0;i<warmups;++i) convert();
        const auto start=std::chrono::steady_clock::now();
        for (std::size_t i=0;i<repetitions;++i) convert();
        const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<std::setprecision(12)<<"{\"seconds\":"<<seconds<<",\"repetitions\":"<<repetitions<<",\"output_bytes\":"<<bytes<<",\"consumed\":"<<consumed<<"}\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
