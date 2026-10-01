#include "bdf.hpp"
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::string& input) {
    bool failed=false;
    try { (void)bdf::parse(input); } catch (const std::exception&) { failed=true; }
    check(failed,"malformed input was accepted");
}
int main() {
    try {
        auto doc=bdf::parse("/* comment ( */\r\n(header \"graphic\" (version \"1.4\")) // x\n"
                            "(text \"literal // /* ( ) and \\\"quotes\\\"\")");
        check(doc.size()==2,"comment handling");
        check(doc[1].children[1].value=="literal // /* ( ) and \"quotes\"","literal string safety");
        check(doc[0].location.line==2,"line tracking");
        rejects("(header"); rejects(")"); rejects("/* unfinished"); rejects("(text \"unfinished)");
        rejects(std::string(300,'(')+std::string(300,')'));
        auto s=bdf::decode(bdf::parse("(header \"graphic\"(version \"1.4\"))"
            "(pin(input)(rect 200 300 216 468)(text \"INPUT\")(text \"A\")(pt 8 168)(rotate270))"));
        check(s.pins[0].point==bdf::Point{208,468},"stored rotated point must not be rotated twice");
        s.pins[0].name="End";
        bool keyword_failed=false;
        try { (void)bdf::emit_verilog(s,"probe"); }
        catch (const std::exception& error) {
            keyword_failed=std::string(error.what()).find("keyword")!=std::string::npos;
        }
        check(keyword_failed,"Quartus rejects differently capitalized Verilog keywords");
        auto ascending=bdf::parse_signal_name("A[0..3]").bits();
        check(ascending.size()==4 && ascending.front().index==0 && ascending.back().index==3,
              "ascending bus bit order");
        auto group=bdf::parse_signal_name("A[7..6], b[2], C").bits();
        check(group.size()==4 && group[0].key()=="a[7]" && group[2].key()=="b[2]" &&
              group[3].key()=="c", "group order and case insensitive bit keys");
        for (const auto& name : {"A[2:0]","A[9999999999999]","A[9000..0]","A[2][1]","A,","A[-1]"}) {
            bool failed=false;
            try { (void)bdf::parse_signal_name(name); } catch (const std::exception&) { failed=true; }
            check(failed,"invalid or unsupported signal name was accepted");
        }
        check(bdf::evaluate_integer("(2 + 1) * 4 - 2 / 2")==11,"integer expression precedence");
        check(bdf::evaluate_integer("lpm_width - 1",{{"LPM_WIDTH","2 + 1"}})==2,"parameter substitution");
        check(bdf::evaluate_integer("17 % 5")==2,"integer remainder");
        for(const auto& expression : {"2147483647+1","2147483648","50000*50000","1/0","1%0", "missing+1", "1; endmodule", "(1+2"}) {
            bool failed=false;
            try { (void)bdf::evaluate_integer(expression); } catch(const std::exception&) { failed=true; }
            check(failed,"invalid integer expression was accepted");
        }
        bool recursive=false;
        try { (void)bdf::evaluate_integer("A",{{"A","B+1"},{"B","A-1"}}); }
        catch(const std::exception&) { recursive=true; }
        check(recursive,"recursive parameters were accepted");
        auto parametric=bdf::decode(bdf::parse("(header \"graphic\"(version \"1.4\"))"
            "(symbol(rect 0 0 80 80)(text \"LPM_COUNTER\")(text \"dut\")"
            "(parameter \"LPM_WIDTH\" \"2+1\" \"width\")"
            "(port(pt 80 40)(output)(text \"q[LPM_WIDTH-1..0]\")))"));
        check(parametric.symbols[0].parameters[0].value=="2+1","BDF parameter value retention");
        auto prepared=bdf::prepare_lpm(parametric.symbols[0],"");
        check(prepared.ports[0].name=="q[2..0]" && prepared.parameters[0].value=="3",
              "LPM port expression resolution");
        std::cout << "Parser checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
