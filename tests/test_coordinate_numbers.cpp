// Standalone regression: compile with src/parser.cpp and src/bdf_tape.cpp.
#include "bdf_tape.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

int coordinate(const std::string& token) {
    const auto source="(header graphic(version 1.4))(junction(pt \""+token+"\" 0))";
    return bdf::experimental::decode_tape(bdf::experimental::parse_tape(source)).junctions.at(0).x;
}
int main() {
    try {
        const std::vector<std::pair<std::string,int>> accepted={
            {"0",0},{"-0",0},{"+0",0},{"01",1},{"-001",-1},{"+12",12},
            {"2147483647",2147483647},{"-2147483648",(-2147483647-1)},
            {" 1",1},{"\t-2",-2},{"\r\n+3",3}};
        for(const auto& item:accepted)
            if(coordinate(item.first)!=item.second) throw std::runtime_error("accepted coordinate changed");
        const std::vector<std::pair<std::string,std::string>> rejected={
            {"2147483648","invalid coordinate"},{"-2147483649","invalid coordinate"},
            {"9223372036854775807","invalid coordinate"},{"9223372036854775808","invalid integer"},
            {"-9223372036854775808","invalid coordinate"},{"-9223372036854775809","invalid integer"},
            {"","invalid integer"},{"+","invalid integer"},{"-","invalid integer"},
            {"1 ","invalid coordinate"},{"0x10","invalid coordinate"},{"1a","invalid coordinate"},
            {"--1","invalid integer"},{"+-1","invalid integer"},{std::string(500,'9'),"invalid integer"}};
        for(const auto& item:rejected) {
            bool failed=false;
            try {(void)coordinate(item.first);}
            catch(const std::runtime_error& error) {
                failed=true;
                if(std::string(error.what())!="1:39: "+item.second+": "+item.first)
                    throw std::runtime_error("coordinate error category or source location changed");
            }
            if(!failed) throw std::runtime_error("invalid coordinate accepted");
        }
        std::cout<<accepted.size()+rejected.size()<<" coordinate acceptance/value/diagnostic checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
