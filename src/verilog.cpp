#include "bdf.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace bdf {
namespace {
[[noreturn]] void unsupported(const std::string& message) {
    throw std::runtime_error("unsupported HDL conversion: " + message);
}
std::string upper(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}
std::string lower(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool on_segment(Point p, const Connector& c) {
    if(p==c.a || p==c.b) return true;
    if(p.x<std::min(c.a.x,c.b.x) || p.x>std::max(c.a.x,c.b.x) ||
       p.y<std::min(c.a.y,c.b.y) || p.y>std::max(c.a.y,c.b.y)) return false;
    // Test horizontal/vertical interiors. Diagonals still join at
    // shared endpoints; a tee on an unsplit diagonal stays disconnected.
    if(c.a.x==c.b.x) return p.x==c.a.x;
    if(c.a.y==c.b.y) return p.y==c.a.y;
    return false;
}
void identifier(const std::string& value) {
    static const std::set<std::string> keywords = {
        "always","and","assign","automatic","begin","buf","bufif0","bufif1","case",
        "casex","casez","cell","cmos","config","deassign","default","defparam","design",
        "disable","edge","else","end","endcase","endconfig","endfunction","endgenerate",
        "endmodule","endprimitive","endspecify","endtable","endtask","event","for","force",
        "forever","fork","function","generate","genvar","highz0","highz1","if","ifnone",
        "incdir","include","initial","inout","input","instance","integer","join","large",
        "liblist","library","localparam","macromodule","medium","module","nand","negedge",
        "nmos","nor","noshowcancelled","not","notif0","notif1","or","output","parameter",
        "pmos","posedge","primitive","pull0","pull1","pulldown","pullup","pulsestyle_onevent",
        "pulsestyle_ondetect","rcmos","real","realtime","reg","release","repeat","rnmos",
        "rpmos","rtran","rtranif0","rtranif1","scalared","showcancelled","signed","small",
        "specify","specparam","strong0","strong1","supply0","supply1","table","task","time",
        "tran","tranif0","tranif1","tri","tri0","tri1","triand","trior","trireg","unsigned",
        "use","uwire","vectored","wait","wand","weak0","weak1","while","wire","wor","xnor","xor"};
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_'))
        unsupported("identifier requires escaping: " + value);
    for (unsigned char c : value) if (!(std::isalnum(c) || c == '_' || c == '$'))
        unsupported("identifier requires escaping: " + value);
    // Quartus rejects End and INPUT as keywords even when capitalization differs.
    if (keywords.count(lower(value))) unsupported("Verilog keyword used as identifier: " + value);
}
struct UnionFind {
    std::vector<int> parents;
    int add() { int id=static_cast<int>(parents.size()); parents.push_back(id); return id; }
    int find(int id) {
        while (parents[id] != id) { parents[id] = parents[parents[id]]; id = parents[id]; }
        return id;
    }
    void join(int a, int b) { a=find(a); b=find(b); if(a!=b) parents[b]=a; }
};
struct Gate {
    const Symbol* symbol;
    std::vector<const Terminal*> inputs;
    const Terminal* output = nullptr;
    std::string op;
    bool invert = false;
    enum class Kind { logic, constant, dff, tff, component, lpm } kind = Kind::logic;
    int value = 0;
    const Terminal *data = nullptr, *clock = nullptr, *clear = nullptr, *preset = nullptr;
    bool sequential() const { return kind==Kind::dff || kind==Kind::tff; }
    bool instance() const { return kind==Kind::component || kind==Kind::lpm; }
};
Gate gate(const Symbol& s, const ComponentLibrary& components) {
    if (s.parameterized && !is_lpm(s.type)) unsupported("parameters on " + s.instance);
    Gate g{&s, {}, nullptr, "", false};
    const auto type = upper(s.type);
    if (is_lpm(s.type)) {
        identifier(s.instance);
        g.kind=Gate::Kind::lpm;
        for (const auto& p : s.ports) if(p.direction=="input") g.inputs.push_back(&p);
        return g;
    }
    if (components.count(s.type)) {
        identifier(s.type);
        identifier(s.instance);
        g.kind=Gate::Kind::component;
        std::map<std::string,std::pair<std::string,std::size_t>> expected;
        for (const auto& p : components.at(s.type).pins) {
            const auto name= parse_signal_name(p.name);
            if (name.terms.size()!=1 || p.direction=="bidir")
                unsupported("component interface on " + s.type);
            if (!expected.emplace(name.terms[0].base,std::make_pair(p.direction,name.bits().size())).second)
                unsupported("duplicate component pin on " + s.type);
        }
        std::set<std::string> seen;
        for (const auto& p : s.ports) {
            const auto name=parse_signal_name(p.name);
            if (name.terms.size()!=1 || !seen.insert(name.terms[0].base).second)
                unsupported("component port shape on " + s.instance);
            const auto found=expected.find(name.terms[0].base);
            if (found==expected.end() || found->second.first!=p.direction ||
                found->second.second!=name.bits().size())
                unsupported("component interface mismatch on " + s.instance + "." + p.name);
            if (p.direction=="input") g.inputs.push_back(&p);
        }
        if (seen.size()!=expected.size()) unsupported("missing component ports on " + s.instance);
        return g;
    }
    if (type=="VCC" || type=="GND") {
        if (s.ports.size()!=1 || s.ports[0].direction!="output" || s.ports[0].name!="1")
            unsupported("constant port shape on " + s.instance);
        g.kind=Gate::Kind::constant; g.value=type=="VCC"; g.output=&s.ports[0]; return g;
    }
    if (type=="DFF" || type=="TFF") {
        std::map<std::string,const Terminal*> ports;
        for (const auto& p : s.ports)
            if (!ports.emplace(upper(p.name),&p).second) unsupported("duplicate port on " + s.instance);
        const std::string data=type=="DFF" ? "D" : "T";
        if (ports.size()!=5 || !ports.count(data) || !ports.count("CLK") ||
            !ports.count("CLRN") || !ports.count("PRN") || !ports.count("Q"))
            unsupported("flip-flop port shape on " + s.instance);
        g.kind=type=="DFF" ? Gate::Kind::dff : Gate::Kind::tff;
        g.output=ports.at("Q"); g.data=ports.at(data); g.clock=ports.at("CLK");
        g.clear=ports.at("CLRN"); g.preset=ports.at("PRN");
        g.inputs={g.data,g.clock,g.clear,g.preset};
        if (g.output->direction!="output") unsupported("Q direction on " + s.instance);
        for (const auto* p : g.inputs) if (p->direction!="input")
            unsupported("flip-flop input direction on " + s.instance);
        return g;
    }
    int expected = 0;
    // BNOR4's input and output bubbles cancel; Quartus exports an AND.
    if (type=="BNOR4") { expected=4; g.op=" & "; }
    else if (type == "NOT" || type == "BUF") { expected=1; g.invert=type=="NOT"; }
    else if (type == "XOR" || type == "XNOR") { expected=2; g.op=" ^ "; g.invert=type=="XNOR"; }
    else {
        for (const auto& prefix : {std::string("AND"),std::string("OR"),std::string("NAND"),
                                   std::string("NOR")}) {
            if (type.rfind(prefix,0)!=0 || type.size()==prefix.size()) continue;
            const auto suffix = type.substr(prefix.size());
            if (!std::all_of(suffix.begin(),suffix.end(),[](unsigned char c){return std::isdigit(c);})) continue;
            try { expected=std::stoi(suffix); } catch (const std::exception&) { expected=0; }
            g.op = prefix == "AND" || prefix == "NAND" ? " & " :
                   prefix == "OR" || prefix == "NOR" ? " | " : " ^ ";
            g.invert=prefix=="NAND" || prefix=="NOR" || prefix=="XNOR";
            break;
        }
        if (expected!=2 && expected!=3 && expected!=4 && expected!=6 && expected!=8 && expected!=12)
            unsupported("symbol " + s.type + " at " + s.instance);
    }
    std::map<std::string,const Terminal*> inputs;
    for (const auto& p : s.ports) {
        if (p.direction=="output" && p.name=="OUT" && !g.output) g.output=&p;
        else if (p.direction=="input" && inputs.emplace(p.name,&p).second) {}
        else unsupported("unexpected or duplicate port " + p.name + " on " + s.instance);
    }
    if (!g.output || inputs.size()!=static_cast<std::size_t>(expected))
        unsupported("port count on " + s.instance);
    for (int i=1; i<=expected; ++i) {
        const std::string name=expected==1 ? "IN" : "IN"+std::to_string(i);
        auto p=inputs.find(name);
        if (p==inputs.end()) unsupported("missing " + name + " on " + s.instance);
        g.inputs.push_back(p->second);
    }
    return g;
}
struct Geometry {
    UnionFind uf;
    std::map<Point,int> points;
    struct Entry { int major,minor,id; };
    std::vector<Entry> by_x,by_y;
    int at(Point p) {
        auto found=points.find(p);
        if (found!=points.end()) return found->second;
        const auto id=uf.add(); points.emplace(p,id); return id;
    }
    int root(Point p) { return uf.find(at(p)); }
    void segment(const Connector& c) { uf.join(at(c.a),at(c.b)); }
    void index() {
        by_x.reserve(points.size()); by_y.reserve(points.size());
        for(const auto& [p,id] : points) {
            by_x.push_back({p.x,p.y,id}); by_y.push_back({p.y,p.x,id});
        }
        auto less=[](const Entry& a,const Entry& b) {
            return a.major<b.major || (a.major==b.major && a.minor<b.minor);
        };
        // by_x already follows the same (x,y) ordering as points.
        std::sort(by_y.begin(),by_y.end(),less);
    }
    void connect(const Connector& c) {
        // Diagonal endpoints were joined by segment(); their interiors never join.
        if(c.a.x!=c.b.x && c.a.y!=c.b.y) return;
        const bool vertical=c.a.x==c.b.x;
        const auto& entries=vertical ? by_x : by_y;
        const int major=vertical ? c.a.x : c.a.y;
        const int lo=vertical ? std::min(c.a.y,c.b.y) : std::min(c.a.x,c.b.x);
        const int hi=vertical ? std::max(c.a.y,c.b.y) : std::max(c.a.x,c.b.x);
        const Entry key{major,lo,0};
        auto found=std::lower_bound(entries.begin(),entries.end(),key,
            [](const Entry& a,const Entry& b) {
                return a.major<b.major || (a.major==b.major && a.minor<b.minor);
            });
        const int anchor=at(c.a);
        for(;found!=entries.end() && found->major==major && found->minor<=hi;++found)
            uf.join(anchor,found->id);
    }
};
SignalName checked_name(const std::string& name) {
    const std::string prefix="<<__$DEF_ALIAS";
    if (name.rfind(prefix,0)==0 && name.size()>prefix.size()+2 &&
        name.substr(name.size()-2)==">>" &&
        std::all_of(name.begin()+static_cast<std::ptrdiff_t>(prefix.size()),name.end()-2,
                    [](unsigned char c){return std::isdigit(c);}))
        return {{{name,false,0,0}}};
    auto result=parse_signal_name(name);
    for (const auto& term : result.terms) identifier(term.base);
    return result;
}
struct Port {
    const Terminal* terminal;
    SignalTerm term;
    std::vector<int> bits;
    bool bus = false;
};
struct Bus {
    std::vector<std::vector<int>> labels;
    std::vector<const Port*> ports;
    std::vector<int> lanes;
};
}
bool is_supported_primitive(const std::string& type) {
    const auto name=upper(type);
    if (name=="VCC" || name=="GND" || name=="DFF" || name=="TFF" ||
        name=="NOT" || name=="BUF" || name=="XOR" || name=="XNOR" || name=="BNOR4" || is_lpm(name)) return true;
    for (const auto* prefix : {"AND","OR","NAND","NOR"})
        for (int arity : {2,3,4,6,8,12})
            if (name==std::string(prefix)+std::to_string(arity)) return true;
    return false;
}
std::string emit_verilog(const Schematic& input, const std::string& module,
                         const ComponentLibrary& components) {
    auto s=input;
    for(auto& symbol : s.symbols) symbol=prepare_lpm(std::move(symbol),s.source_directory);
    identifier(module);
    if (s.version!="1.4" && s.version!="1.3") unsupported("graphic version " + s.version);
    const std::set<std::string> allowed = {"header","pin","symbol","connector","junction","text",
                                          "line","rectangle","ellipse","arc","circle"};
    for (const auto& [tag,count] : s.root_counts) {
        (void)count;
        if (!allowed.count(tag)) unsupported("root element " + tag);
    }
    UnionFind bits;
    Geometry scalar_geometry, bus_geometry;
    std::map<std::string,int> aliases;
    auto named_bit = [&](const SignalBit& bit) {
        const auto key=bit.key();
        auto found=aliases.find(key);
        if (found!=aliases.end()) return found->second;
        const int id=bits.add(); aliases.emplace(key,id); return id;
    };
    auto named_bits = [&](const SignalName& name) {
        std::vector<int> ids;
        for (const auto& bit : name.bits()) ids.push_back(named_bit(bit));
        return ids;
    };
    std::set<std::string> used_names, instances, pin_keys;
    // Only these bus connectors can pass on_segment at a point. Each bucket
    // holds ascending connector indices and a connector is in one bucket per
    // point, so merging them visits candidates in connector order and stops at
    // the first match, keeping the first match and label diagnostics of a scan
    // over every connector without visiting more connectors than that scan.
    std::map<int,std::vector<std::size_t>> bus_columns, bus_rows;
    std::map<Point,std::vector<std::size_t>> bus_ends;
    for (std::size_t i=0; i<s.connectors.size(); ++i) {
        const auto& c=s.connectors[i];
        if (!c.bus) continue;
        if (c.a.x==c.b.x) bus_columns[c.a.x].push_back(i);
        else if (c.a.y==c.b.y) bus_rows[c.a.y].push_back(i);
        else { bus_ends[c.a].push_back(i); bus_ends[c.b].push_back(i); }
    }
    struct Candidates { const std::size_t *at=nullptr, *end=nullptr; };
    auto bucket=[](const auto& index,const auto& key) {
        const auto found=index.find(key);
        if (found==index.end()) return Candidates{};
        return Candidates{found->second.data(),found->second.data()+found->second.size()};
    };
    auto uses_bus=[&](const Terminal& terminal,const SignalTerm& term,std::size_t width) {
        if(width>1) return true;
        Candidates buckets[]={bucket(bus_columns,terminal.point.x),bucket(bus_rows,terminal.point.y),
                              bucket(bus_ends,terminal.point)};
        for (;;) {
            Candidates* next=nullptr;
            for (auto& b : buckets) if (b.at!=b.end && (!next || *b.at<*next->at)) next=&b;
            if (!next) return false;
            const auto& c=s.connectors[*next->at++];
            if (!on_segment(terminal.point,c)) continue;
            if (term.indexed || (!c.label.empty() && checked_name(c.label).bits().size()==1)) return true;
        }
    };
    std::vector<Port> ports;
    for (const auto& p : s.pins) {
        const auto name=checked_name(p.name);
        if (name.terms.size()!=1) unsupported("grouped top-level pin " + p.name);
        const auto& term=name.terms[0];
        identifier(term.base);
        if (!pin_keys.insert(lower(term.base)).second) unsupported("duplicate top-level pin " + p.name);
        used_names.insert(term.base);
        if (p.direction=="bidir") unsupported("bidirectional pin " + p.name);
        auto ids=named_bits(name);
        const bool bus=uses_bus(p,term,ids.size());
        (bus ? bus_geometry : scalar_geometry).at(p.point);
        ports.push_back({&p,term,std::move(ids),bus});
    }
    std::vector<Gate> gates;
    std::map<const Symbol*,std::string> component_instances;
    std::map<const Terminal*,std::vector<int>> gate_bits;
    std::map<const Terminal*,Port> component_ports;
    for (const auto& symbol : s.symbols) {
        if (!instances.insert(lower(symbol.instance)).second) unsupported("duplicate instance " + symbol.instance);
        gates.push_back(gate(symbol,components));
        if (gates.back().instance()) {
            std::string instance="bdf_component_"+symbol.instance;
            while (used_names.count(instance)) instance+='_';
            used_names.insert(instance);
            component_instances[&symbol]=instance;
        }
        for (const auto& p : symbol.ports) {
            if (gates.back().instance()) {
                const auto name=checked_name(p.name);
                // Port names belong to the component, not to the parent's
                // case-insensitive signal namespace.
                std::vector<int> ids;
                const auto width=name.bits().size();
                for (std::size_t i=0; i<width; ++i) ids.push_back(bits.add());
                const bool bus=uses_bus(p,name.terms[0],ids.size());
                (bus ? bus_geometry : scalar_geometry).at(p.point);
                gate_bits[&p]=ids;
                component_ports.emplace(&p,Port{&p,name.terms[0],std::move(ids),bus});
            } else {
                if (gates.back().kind!=Gate::Kind::constant) identifier(p.name);
                scalar_geometry.at(p.point); gate_bits[&p]={bits.add()};
            }
        }
    }
    for (const auto& c : s.connectors) {
        (c.bus ? bus_geometry : scalar_geometry).segment(c);
    }
    // Keep wire and bus geometry separate. A mixed contact selects named bits;
    // it never shorts all lanes together. Standalone junction marks add no point.
    scalar_geometry.index(); bus_geometry.index();
    for (const auto& c : s.connectors)
        (c.bus ? bus_geometry : scalar_geometry).connect(c);
    std::map<int,std::vector<int>> scalar_members;
    std::map<int,Bus> buses;
    for (const auto& p : ports) {
        if (!p.bus) scalar_members[scalar_geometry.root(p.terminal->point)].push_back(p.bits[0]);
        else buses[bus_geometry.root(p.terminal->point)].ports.push_back(&p);
    }
    // Preserve schematic order instead of ordering terminals by heap addresses.
    for (const auto& symbol : s.symbols) for (const auto& terminal : symbol.ports) {
        const auto* p=&terminal;
        const auto& ids=gate_bits.at(p);
        if (component_ports.count(p) && component_ports.at(p).bus)
            buses[bus_geometry.root(p->point)].ports.push_back(&component_ports.at(p));
        else scalar_members[scalar_geometry.root(p->point)].push_back(ids[0]);
    }
    for (const auto& c : s.connectors) {
        if (c.bus) {
            auto& bus=buses[bus_geometry.root(c.a)];
            if (!c.label.empty()) bus.labels.push_back(named_bits(checked_name(c.label)));
        } else {
            auto& members=scalar_members[scalar_geometry.root(c.a)];
            if (!c.label.empty()) {
                const auto ids=named_bits(checked_name(c.label));
                if (ids.size()!=1) unsupported("vector label on scalar wire: " + c.label);
                members.push_back(ids[0]);
            }
        }
    }
    std::set<int> wired_roots;
    std::set<int> wired_bus_roots;
    for (const auto& c : s.connectors) if (!c.bus) wired_roots.insert(scalar_geometry.root(c.a));
    else wired_bus_roots.insert(bus_geometry.root(c.a));
    std::set<const Terminal*> omitted_controls;
    for (const auto& g : gates) if (g.sequential()) for (const auto* p : {g.clear,g.preset}) {
        const auto root=scalar_geometry.root(p->point);
        if (!wired_roots.count(root) && scalar_members[root].size()==1) omitted_controls.insert(p);
    }
    std::set<const Terminal*> omitted_lpm_ports;
    for(const auto& g : gates) if(g.kind==Gate::Kind::lpm) for(const auto& p : g.symbol->ports) {
        const auto& port=component_ports.at(&p);
        if(port.bus) {
            const auto root=bus_geometry.root(p.point);
            if(!wired_bus_roots.count(root) && buses[root].ports.size()==1 && buses[root].labels.empty())
                omitted_lpm_ports.insert(&p);
        } else {
            const auto root=scalar_geometry.root(p.point);
            if(!wired_roots.count(root) && scalar_members[root].size()==1) omitted_lpm_ports.insert(&p);
        }
    }
    for (auto& [root,members] : scalar_members) {
        (void)root;
        if (members.empty()) members.push_back(bits.add());
        for (int id : members) bits.join(members[0],id);
    }
    for (auto& [root,bus] : buses) {
        (void)root;
        if (!bus.labels.empty()) {
            // GDFX_BUS_INFO::find_superset selects a widest named group and
            // checks that every other named connection is included in it.
            auto candidate=std::max_element(bus.labels.begin(),bus.labels.end(),
                [](const auto& a,const auto& b){return a.size()<b.size();});
            const std::set<int> superset(candidate->begin(),candidate->end());
            for (const auto& label : bus.labels) for (int id : label)
                if (!superset.count(id)) unsupported("no superset bus for named connections");
            bus.lanes=*candidate;
        } else if (!bus.ports.empty()) {
            auto selected=bus.ports[0];
            for (const auto* p : bus.ports)
                if (p->terminal->direction=="input") { selected=p; break; }
            bus.lanes=selected->bits;
        } else {
            unsupported("unnamed bus without a vector port");
        }
        for (const auto* p : bus.ports) {
            if (p->bits.size()!=bus.lanes.size())
                unsupported("bus width mismatch at " + p->terminal->name);
            for (std::size_t i=0; i<p->bits.size(); ++i) bits.join(p->bits[i],bus.lanes[i]);
        }
    }
    // Named taps resolve through the bit aliases above. Unnamed taps and
    // labels outside the bus's bit namespace stay separate signals.
    // Connectivity is complete: freeze the existing representative of every bit.
    // Preserve representative IDs and ascending traversal for names and diagnostics.
    for (std::size_t i=0; i<bits.parents.size(); ++i)
        bits.parents[i]=bits.find(static_cast<int>(i));
    const auto& bit_root=bits.parents;
    std::vector<int> roots;
    roots.reserve(bit_root.size());
    std::vector<std::size_t> root_index(bit_root.size());
    for (std::size_t i=0; i<bit_root.size(); ++i) if(bit_root[i]==static_cast<int>(i)) {
        root_index[i]=roots.size(); roots.push_back(static_cast<int>(i));
    }
    std::map<int,std::string> names;
    auto bit_expression = [](const SignalBit& bit) {
        return bit.base + (bit.index ? '['+std::to_string(*bit.index)+']' : "");
    };
    // Prefer input references so directly connected outputs cannot drive inputs.
    for (const auto& direction : {std::string("input"),std::string("output")})
        for (const auto& p : ports) if (p.terminal->direction==direction) {
            const auto port_bits=p.term.bits();
            for (std::size_t i=0; i<p.bits.size(); ++i)
                names.emplace(bit_root[p.bits[i]],bit_expression(port_bits[i]));
        }
    struct PackedBus { std::string name; std::vector<int> lanes; };
    std::vector<PackedBus> packed_buses;
    for (const auto& [root,bus] : buses) {
        if (bus.lanes.size()<2) continue;
        std::set<int> lane_roots;
        bool available=true;
        for (int id : bus.lanes) {
            const auto lane=bit_root[id];
            if (names.count(lane) || !lane_roots.insert(lane).second) available=false;
        }
        if (!available) continue;
        std::string name="bdf_bus_"+std::to_string(root);
        while (used_names.count(name)) name+='_';
        used_names.insert(name);
        for (std::size_t i=0; i<bus.lanes.size(); ++i)
            names.emplace(bit_root[bus.lanes[i]],name+'['+std::to_string(bus.lanes.size()-1-i)+']');
        packed_buses.push_back({std::move(name),bus.lanes});
    }
    std::set<int> internal;
    for (int root : roots) if (!names.count(root)) {
        std::string name="bdf_net_"+std::to_string(root);
        while (used_names.count(name)) name+='_';
        used_names.insert(name); names[root]=name; internal.insert(root);
    }
    auto signal = [&](const Terminal& p) { return names.at(bit_root[gate_bits.at(&p)[0]]); };
    auto connection = [&](const Terminal& p) {
        const auto& ids=gate_bits.at(&p);
        // A concatenation of individual bits can add a delta-cycle update at
        // a module input. Preserve whole-vector connections, as Quartus does.
        if (ids.size()>1) for (const auto& port : ports) {
            if (port.bits.size()!=ids.size()) continue;
            bool same=true;
            for (std::size_t i=0; i<ids.size(); ++i)
                if (bit_root[ids[i]]!=bit_root[port.bits[i]]) { same=false; break; }
            if (same) return port.term.base;
        }
        if (ids.size()>1) for (const auto& bus : packed_buses) {
            if (bus.lanes.size()!=ids.size()) continue;
            bool same=true;
            for (std::size_t i=0; i<ids.size(); ++i)
                if (bit_root[ids[i]]!=bit_root[bus.lanes[i]]) { same=false; break; }
            if (same) return bus.name;
        }
        std::string value;
        for (int id : ids) {
            if (!value.empty()) value+=", ";
            value+=names.at(bit_root[id]);
        }
        return ids.size()==1 ? value : "{"+value+"}";
    };
    struct NetChecks {
        int drivers=0;
        unsigned char visited=0;
        std::vector<int> dependencies;
    };
    std::vector<NetChecks> net_checks(roots.size());
    for (const auto& p : ports) if (p.terminal->direction=="input")
        for (int id : p.bits) ++net_checks[root_index[bit_root[id]]].drivers;
    for (const auto& g : gates) {
        if (g.instance()) {
            for (const auto& p : g.symbol->ports) if (p.direction=="output")
                for (int id : gate_bits.at(&p)) ++net_checks[root_index[bit_root[id]]].drivers;
            continue;
        }
        const auto out=bit_root[gate_bits.at(g.output)[0]];
        ++net_checks[root_index[out]].drivers;
        if (g.kind==Gate::Kind::logic)
            for (const auto* p : g.inputs) net_checks[root_index[out]].dependencies.push_back(bit_root[gate_bits.at(p)[0]]);
    }
    for (int root : roots) if (net_checks[root_index[root]].drivers>1) unsupported("multiple drivers on " + names.at(root));
    for (const auto& g : gates) for (const auto* p : g.inputs) {
        if (omitted_controls.count(p) || g.instance()) continue;
        const auto root=bit_root[gate_bits.at(p)[0]];
        if (net_checks[root_index[root]].drivers==0) unsupported("undriven input " + g.symbol->instance + "." + p->name);
    }
    std::function<void(int)> visit = [&](int root) {
        auto& check=net_checks[root_index[root]];
        if (check.visited==1) unsupported("combinational feedback on " + names.at(root));
        if (check.visited==2) return;
        check.visited=1;
        for (int dependency : check.dependencies) visit(dependency);
        check.visited=2;
    };
    for (int root : roots) visit(root);
    std::map<const Gate*,std::string> states;
    int state_number=0;
    for (const auto& g : gates) if (g.sequential()) {
        std::string name="bdf_state_"+std::to_string(state_number++);
        while (used_names.count(name)) name+='_';
        used_names.insert(name); states[&g]=name;
    }
    std::ostringstream out;
    out << "// Generated by bdf-tool.\nmodule " << module << "(\n";
    for (std::size_t i=0; i<ports.size(); ++i) {
        const auto& p=ports[i];
        out << "    " << p.terminal->direction << " wire ";
        if (p.term.indexed) out << '[' << p.term.left << ':' << p.term.right << "] ";
        out << p.term.base << (i+1<ports.size() ? "," : "") << '\n';
    }
    out << ");\n";
    for (const auto& bus : packed_buses)
        out << "wire [" << bus.lanes.size()-1 << ":0] " << bus.name << ";\n";
    for (int root : internal) out << "wire " << names.at(root) << ";\n";
    for (const auto& [g,state] : states) { (void)g; out << "reg " << state << ";\n"; }
    for (const auto& g : gates) {
        if (g.instance()) {
            out << (g.kind==Gate::Kind::lpm ? lower(g.symbol->type) : g.symbol->type);
            if(g.kind==Gate::Kind::lpm && !g.symbol->parameters.empty()) {
                out << " #(\n";
                for(std::size_t i=0; i<g.symbol->parameters.size(); ++i) {
                    const auto& p=g.symbol->parameters[i];
                    out << "    ." << lower(p.name) << '(' << p.value << ')'
                        << (i+1<g.symbol->parameters.size() ? "," : "") << '\n';
                }
                out << ")";
            }
            out << ' ' << component_instances.at(g.symbol) << " (\n";
            std::vector<const Terminal*> connected;
            for(const auto& p : g.symbol->ports) if(!omitted_lpm_ports.count(&p)) connected.push_back(&p);
            for (std::size_t i=0; i<connected.size(); ++i) {
                const auto& p=*connected[i];
                out << "    ." << component_ports.at(&p).term.base << '(' << connection(p) << ')'
                    << (i+1<connected.size() ? "," : "") << '\n';
            }
            out << ");\n";
            continue;
        }
        if (g.kind==Gate::Kind::constant) {
            out << "assign " << signal(*g.output) << " = 1'b" << g.value << ";\n";
            continue;
        }
        if (g.sequential()) {
            const auto& state=states.at(&g);
            out << "always @(posedge " << signal(*g.clock);
            if (!omitted_controls.count(g.clear)) out << " or negedge " << signal(*g.clear);
            if (!omitted_controls.count(g.preset)) out << " or negedge " << signal(*g.preset);
            out << ") begin\n";
            if (!omitted_controls.count(g.clear))
                out << "    if (!" << signal(*g.clear) << ") " << state << " <= 1'b0; else\n";
            if (!omitted_controls.count(g.preset))
                out << "    if (!" << signal(*g.preset) << ") " << state << " <= 1'b1; else\n";
            out << "    " << state << " <= ";
            if (g.kind==Gate::Kind::tff) out << state << " ^ ";
            out << signal(*g.data) << ";\nend\nassign " << signal(*g.output) << " = " << state << ";\n";
            continue;
        }
        std::string expression;
        for (const auto* p : g.inputs) { if (!expression.empty()) expression+=g.op; expression+=signal(*p); }
        if (g.invert) expression="~(" + expression + ")";
        out << "assign " << signal(*g.output) << " = " << expression << ";\n";
    }
    for (const auto& p : ports) if (p.terminal->direction=="output") {
        const auto port_bits=p.term.bits();
        for (std::size_t i=0; i<p.bits.size(); ++i) {
            const auto output=bit_expression(port_bits[i]);
            const auto& source=names.at(bit_root[p.bits[i]]);
            if (output!=source) out << "assign " << output << " = " << source << ";\n";
        }
    }
    out << "endmodule\n";
    return out.str();
}
} // namespace bdf
