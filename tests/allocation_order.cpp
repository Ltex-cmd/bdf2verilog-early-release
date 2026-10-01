#include "bdf.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>

// Exercise emit_verilog's public seam with equal designs, differing only in
// vector allocation history. Move each allocated vector into its original slot.
int main(int argc, char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("Usage: allocation-order SOURCE.bdf OUTPUT-DIR");
        auto original=bdf::decode(bdf::parse(bdf::read_file(argv[1])));
        original.source_directory=std::filesystem::absolute(argv[1]).parent_path().string();
        std::filesystem::create_directories(argv[2]);
        std::set<std::string> distinct;
        std::mt19937 random(20260930);
        for(int trial=0;trial<48;++trial) {
            auto design=original;
            std::vector<std::size_t> order(design.symbols.size());
            std::iota(order.begin(),order.end(),0);
            if(trial==1) std::reverse(order.begin(),order.end());
            else if(trial>1) std::shuffle(order.begin(),order.end(),random);
            std::vector<std::vector<bdf::Terminal>> allocated(design.symbols.size());
            for(auto i:order) allocated[i]=original.symbols[i].ports;
            for(std::size_t i=0;i<design.symbols.size();++i) design.symbols[i].ports=std::move(allocated[i]);
            if(bdf::inspect_json(design)!=bdf::inspect_json(original))
                throw std::runtime_error("Harness changed semantic input");
            auto output=bdf::emit_verilog(design,"allocation_probe");
            distinct.insert(output);
            std::ofstream file(std::filesystem::path(argv[2])/("trial-"+std::to_string(trial)+".v"), std::ios::binary);
            file<<output;file.close();if(!file) throw std::runtime_error("output write failed");
        }
        std::cout<<"48 allocations, distinct HDL outputs="<<distinct.size()<<'\n';
        return distinct.size()==1 ? 0 : 1;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}
}
