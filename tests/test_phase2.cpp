#include "bdf_tape.hpp"
#include "project_context.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace fs=std::filesystem;
using bdf::experimental::TapeDocument;
void check(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
template<class F> bool rejects(F fn) {try {fn();return false;} catch(const std::exception&) {return true;}}
std::uint32_t compare_node(const bdf::Node& node,const TapeDocument& tape,std::uint32_t index) {
    const auto& flat=tape.nodes.at(index);
    check(node.list==(flat.kind==bdf::experimental::TapeKind::list),"list kind mismatch");
    check(node.quoted==(flat.kind==bdf::experimental::TapeKind::quoted),"quote kind mismatch");
    check(node.value==tape.text(index),"token text mismatch");
    check(node.location.line==flat.line && node.location.column==flat.column,"token location mismatch");
    check(node.children.size()==flat.child_count,"child count mismatch");
    auto next=index+1;
    for(const auto& child:node.children) next=compare_node(child,tape,next);
    check(next==flat.next,"matching-list index mismatch");
    return next;
}
void differential(const std::string& text) {
    bool old_ok=true,new_ok=true;bdf::Document tree;TapeDocument tape;
    try {tree=bdf::parse(text);} catch(const std::exception&) {old_ok=false;}
    try {tape=bdf::experimental::parse_tape(text);} catch(const std::exception&) {new_ok=false;}
    check(old_ok==new_ok,"lexer acceptance differs");
    if(!old_ok) return;
    std::uint32_t index=0;
    for(const auto& root:tree) index=compare_node(root,tape,index);
    check(index==tape.nodes.size(),"extra tape nodes");
    bdf::Schematic a,b;
    try {a=bdf::decode(tree);} catch(const std::exception&) {old_ok=false;}
    try {b=bdf::experimental::decode_tape(tape);} catch(const std::exception&) {new_ok=false;}
    check(old_ok==new_ok,"decoder acceptance differs");
    if(old_ok) check(bdf::inspect_json(a)==bdf::inspect_json(b),"decoded schematic differs");
}
void write(const fs::path& path,const std::string& text) {
    std::ofstream out(path,std::ios::binary);out<<text;out.close();check(bool(out),"fixture write failed");
}
std::string wire() {
    return "(header \"graphic\"(version \"1.4\"))"
           "(pin(input)(rect 0 0 168 16)(text \"INPUT\")(text \"A\")(pt 0 8))"
           "(pin(output)(rect 120 0 288 16)(text \"OUTPUT\")(text \"Y\")(pt 0 8))"
           "(connector(pt 0 8)(pt 120 8))";
}
std::string wrapper(const std::string& type) {
    auto text=wire();text.erase(text.find("(connector"));
    return text+"(symbol(rect 40 0 80 16)(text \""+type+"\")(text \"dut\")"
           "(port(input)(pt 0 8)(text \"A\"))(port(output)(pt 40 8)(text \"Y\")))"
           "(connector(pt 0 8)(pt 40 8))(connector(pt 80 8)(pt 120 8))";
}
std::string inverter() {
    auto text=wire();text.erase(text.find("(connector"));
    return text+"(symbol(rect 40 0 80 16)(text \"NOT\")(text \"dut\")"
           "(port(input)(pt 0 8)(text \"IN\"))(port(output)(pt 40 8)(text \"OUT\")))"
           "(connector(pt 0 8)(pt 40 8))(connector(pt 80 8)(pt 120 8))";
}
int main(int argc,char** argv) {
    fs::path fixtures;
    try {
        const std::vector<std::string> tricky={
            "", "()", "()()", "(a(b)(c))", "A/* x */B", "A// x\nB", "/*unfinished",
            "(header", ")", "(text \"unfinished)", "(a \"q\\\" r\\\\s\\n\")",
            "/* comment ( */\r\n(header \"graphic\"(version \"1.4\"))// x\n(text \"/* ) //\")",
            std::string(257,'(')+std::string(257,')'),std::string(258,'(')+std::string(258,')'),
            wire()+"(drawing(line(pt 1 2)(pt 3 4))",wire()+"(drawing(text \"oops))",
            "(header \"graphic\"(version \"1.4\")(version \"1.4\"))",
            "(header \"graphic\"(version \"1.4\"))(connector(pt 1 2)(pt 3 4)(pt 5 6))",
            "(header \"graphic\"(version \"1.4\"))(pin(input)(output)(rect 0 0 1 1)(pt 0 0)(text \"x\")(text \"A\"))"};
        for(const auto& text:tricky) differential(text);
        for(int i=1;i<argc;++i) differential(bdf::read_file(argv[i]));
        check(rejects([] {bdf::experimental::index_component_names({"/probe/Leaf.bdf","/probe/leaf.bdf"});}),
              "case-insensitive ambiguity accepted");
        std::mt19937 random(20260930);
        const std::string alphabet="() \n\t\r\"\\/abc012*";
        for(int trial=0;trial<4000;++trial) {
            std::string value;const auto length=random()%240;
            for(unsigned i=0;i<length;++i) value+=alphabet[random()%alphabet.size()];
            differential(value);
        }
        fixtures=fs::temp_directory_path()/ ("bdf-context-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(fs::create_directory(fixtures),"fixture directory exists");
        const auto leaf=fixtures/"leaf.bdf",middle=fixtures/"middle.bdf",top=fixtures/"top.bdf",spare=fixtures/"spare.bdf";
        auto same_size_wire=wire();same_size_wire.resize(inverter().size(),' ');
        write(leaf,same_size_wire);write(middle,wrapper("leaf"));write(top,wrapper("middle"));write(spare,wire());
        bdf::experimental::ProjectContext context(fixtures.string());
        auto baseline=context.convert(top.string(),"probe");
        check(baseline==bdf::emit_verilog_project(top.string(),"probe"),"fresh hierarchy differs");
        check(context.convert(top.string(),"probe")==baseline,"cache output differs");
        check(context.stats().output_hits==1 && context.stats().files_parsed==3,"cache did extra work");
        (void)context.convert(middle.string(),"middle");(void)context.convert(leaf.string(),"leaf");
        const auto unrelated=context.convert(spare.string(),"spare");
        const auto parsed=context.stats().files_parsed,invalid=context.stats().outputs_invalidated;
        const auto stamp=fs::last_write_time(leaf);const auto original_size=fs::file_size(leaf);
        write(leaf,inverter());fs::last_write_time(leaf,stamp);
        check(fs::file_size(leaf)==original_size,"invalidation fixture must preserve file size");
        context.refresh();
        check(context.stats().outputs_invalidated==invalid+3,"transitive dependency invalidation failed");
        const auto changed=context.convert(top.string(),"probe");
        check(changed!=baseline && changed==bdf::emit_verilog_project(top.string(),"probe"),"changed child not rebuilt");
        check(context.stats().files_parsed==parsed+1,"unchanged parent was reparsed");
        const auto hits=context.stats().output_hits;
        check(context.convert(spare.string(),"spare")==unrelated && context.stats().output_hits==hits+1,"unrelated cache lost");
        fs::rename(leaf,fixtures/"renamed.bdf");context.refresh();
        check(rejects([&]{context.convert(top.string(),"probe");}),"removed component remained cached");
        write(leaf,wire());context.refresh();
        check(context.convert(top.string(),"probe")==baseline,"added component not found");
        auto alias=context.convert(top.string(),"alternate");
        check(alias.find("module alternate(")!=std::string::npos,"module name cache key missing");
        write(leaf,inverter());
        bdf::experimental::ProjectContext live(fixtures.string(),{},bdf::experimental::RefreshPolicy::before_conversion);
        (void)live.convert(top.string(),"live");write(leaf,wire());
        check(live.convert(top.string(),"live")==bdf::emit_verilog_project(top.string(),"live"),"live refresh used stale content");
        const auto library=fixtures/"library";fs::create_directory(library);
        bdf::experimental::ProjectContext with_library(fixtures.string(),{library.string()});
        (void)with_library.convert(top.string(),"probe");fs::remove(library);
        check(rejects([&]{with_library.refresh();}),"missing library accepted");
        check(rejects([&]{with_library.convert(top.string(),"probe");}),"failed refresh returned cached HDL");
        fs::create_directory(library);with_library.refresh();
        check(with_library.convert(top.string(),"probe")==baseline,"context failed to recover");
        fs::remove_all(fixtures);
        std::cout<<"Tape: "<<argc-1<<" corpus files, "<<tricky.size()<<" lexical/decoder cases, 4000 seeded malformed inputs.\n"
                 <<"Context: cache reuse, transitive/content invalidation, unaffected parents, additions/removals, module keys, live mode, failed-refresh recovery passed.\n";
        return 0;
    } catch(const std::exception& error) {
        if(!fixtures.empty()) fs::remove_all(fixtures);
        std::cerr<<error.what()<<'\n';return 1;
    }
}
