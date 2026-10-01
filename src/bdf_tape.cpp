#include "bdf_tape.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace bdf::experimental {
namespace {
constexpr auto escaped_length=std::numeric_limits<std::uint32_t>::max();
class TapeParser {
    TapeDocument doc;
    std::size_t pos=0;
    std::uint32_t line=1,column=1;
    std::array<std::uint32_t,257> stack{};
    std::size_t depth=0;
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(std::to_string(line)+":"+std::to_string(column)+": "+message);
    }
    char peek(std::size_t ahead=0) const { return pos+ahead<doc.source.size() ? doc.source[pos+ahead] : '\0'; }
    char take() {
        const char c=doc.source[pos++];
        if(c=='\n') {++line;column=1;} else ++column;
        return c;
    }
    void skip() {
        while(pos<doc.source.size()) {
            if(std::isspace(static_cast<unsigned char>(peek()))) {take();continue;}
            if(peek()=='/' && peek(1)=='/') {
                while(pos<doc.source.size() && take()!='\n') {}
            } else if(peek()=='/' && peek(1)=='*') {
                take();take();
                while(pos<doc.source.size() && !(peek()=='*' && peek(1)=='/')) take();
                if(pos==doc.source.size()) fail("unterminated block comment");
                take();take();
            } else break;
        }
    }
public:
    explicit TapeParser(std::string source) {doc.source=std::move(source);}
    TapeDocument run() {
        if(doc.source.size()>64*1024*1024) fail("input exceeds 64 MiB");
        // Estimate only initial capacity; unusual token densities still grow safely.
        doc.nodes.reserve(std::min<std::size_t>(doc.source.size()/8+1,65536));
        for(;;) {
            skip();
            if(pos==doc.source.size()) {
                if(depth) fail("unterminated list");
                break;
            }
            if(peek()==')') {
                if(!depth) fail("unexpected closing parenthesis");
                take();doc.nodes[stack[--depth]].next=static_cast<std::uint32_t>(doc.nodes.size());
                continue;
            }
            if(depth>256) fail("nesting exceeds 256 levels");
            const auto index=static_cast<std::uint32_t>(doc.nodes.size());
            if(depth) ++doc.nodes[stack[depth-1]].child_count;
            TapeNode node;
            node.line=line;node.column=column;node.offset=static_cast<std::uint32_t>(pos);
            if(peek()=='(') {
                take();node.kind=TapeKind::list;
                doc.nodes.push_back(node);stack[depth++]=index;continue;
            }
            if(peek()=='"') {
                take();node.kind=TapeKind::quoted;
                const auto start=pos;
                bool owns=false;std::string decoded;
                while(pos<doc.source.size() && peek()!='"') {
                    const auto before=pos;
                    char c=take();
                    if(c=='\\' && (peek()=='"' || peek()=='\\')) {
                        if(!owns) {decoded.assign(doc.source,start,before-start);owns=true;}
                        c=take();
                    }
                    if(owns) decoded+=c;
                }
                if(pos==doc.source.size()) fail("unterminated quoted string");
                if(owns) {
                    node.offset=static_cast<std::uint32_t>(doc.escaped.size());node.length=escaped_length;
                    doc.escaped.push_back(std::move(decoded));
                } else {node.offset=static_cast<std::uint32_t>(start);node.length=static_cast<std::uint32_t>(pos-start);}
                take();
            } else {
                const auto start=pos;
                while(pos<doc.source.size() && !std::isspace(static_cast<unsigned char>(peek())) &&
                      peek()!='(' && peek()!=')' && peek()!='"' &&
                      !(peek()=='/' && (peek(1)=='*' || peek(1)=='/'))) take();
                if(pos==start) fail("unexpected character");
                node.length=static_cast<std::uint32_t>(pos-start);
            }
            node.next=index+1;doc.nodes.push_back(node);
        }
        return std::move(doc);
    }
};

struct View {
    const TapeDocument& doc;
    std::uint32_t index;
    const TapeNode& node() const {return doc.nodes[index];}
    Location location() const {return {node().line,node().column};}
    std::string_view tag() const {return doc.tag(index);}
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(std::to_string(node().line)+":"+std::to_string(node().column)+": "+message);
    }
    View child(std::size_t ordinal) const {
        if(node().kind!=TapeKind::list || ordinal>=node().child_count) fail("missing atom");
        auto at=index+1;
        while(ordinal--) at=doc.nodes[at].next;
        return {doc,at};
    }
    std::string atom(std::size_t ordinal) const {
        const auto value=child(ordinal);
        if(value.node().kind==TapeKind::list) fail("missing atom");
        return std::string(doc.text(value.index));
    }
    int integer(std::size_t ordinal) const {
        const auto item=child(ordinal);
        if(item.node().kind==TapeKind::list) fail("missing atom");
        const auto span=doc.text(item.index);
        int fast_value=0;
        const auto parsed=std::from_chars(span.data(),span.data()+span.size(),fast_value);
        if(parsed.ec==std::errc{} && parsed.ptr==span.data()+span.size()) return fast_value;
        // Preserve stoll's whitespace/plus acceptance and exact error categories.
        const auto text=std::string(span);std::size_t consumed=0;long long value;
        try {value=std::stoll(text,&consumed);}
        catch(const std::exception&) {fail("invalid integer: "+text);}
        if(consumed!=text.size() || value<std::numeric_limits<int>::min() || value>std::numeric_limits<int>::max())
            fail("invalid coordinate: "+text);
        return static_cast<int>(value);
    }
};
enum class Tag {other,rect,pt,text,version,input,output,bidir,port,parameter,unused,bus};
Tag classify(std::string_view text) {
    if(text=="rect") return Tag::rect;
    if(text=="pt") return Tag::pt;
    if(text=="text") return Tag::text;
    if(text=="version") return Tag::version;
    if(text=="input") return Tag::input;
    if(text=="output") return Tag::output;
    if(text=="bidir") return Tag::bidir;
    if(text=="port") return Tag::port;
    if(text=="parameter") return Tag::parameter;
    if(text=="unused") return Tag::unused;
    if(text=="bus") return Tag::bus;
    return Tag::other;
}
struct Fields {
    const TapeDocument& doc;
    std::uint32_t owner,rect=0,version=0,rect_count=0,version_count=0,text_count=0,point_count=0;
    std::array<std::uint32_t,2> texts{},points{};
    unsigned directions=0;bool unused=false,bus=false;
    std::vector<std::uint32_t> ports,parameters;
    explicit Fields(View value):doc(value.doc),owner(value.index) {
        for(auto at=owner+1;at<value.node().next;at=doc.nodes[at].next) {
            switch(classify(doc.tag(at))) {
            case Tag::rect: rect=at;++rect_count;break;
            case Tag::version: version=at;++version_count;break;
            case Tag::text: if(text_count<2) texts[text_count]=at;++text_count;break;
            case Tag::pt: if(point_count<2) points[point_count]=at;++point_count;break;
            case Tag::input: directions|=1;break;
            case Tag::output: directions|=2;break;
            case Tag::bidir: directions|=4;break;
            case Tag::port: ports.push_back(at);break;
            case Tag::parameter: parameters.push_back(at);break;
            case Tag::unused: unused=true;break;
            case Tag::bus: bus=true;break;
            default: break;
            }
        }
    }
    View required(std::string_view tag) const {
        const auto count=tag=="rect" ? rect_count : tag=="version" ? version_count : point_count;
        const auto at=tag=="rect" ? rect : tag=="version" ? version : points[0];
        if(count!=1) View{doc,owner}.fail("expected exactly one "+std::string(tag)+" in "+std::string(doc.tag(owner)));
        return {doc,at};
    }
    std::string direction() const {
        if(!directions) View{doc,owner}.fail("missing port direction");
        if(directions&(directions-1)) View{doc,owner}.fail("multiple port directions");
        return directions==1 ? "input" : directions==2 ? "output" : "bidir";
    }
};
Point point(View view) {
    if(view.node().child_count!=3) view.fail("point requires two coordinates");
    return {view.integer(1),view.integer(2)};
}
Point absolute_point(View view,const Fields& fields) {
    const auto rect=fields.required("rect");
    if(rect.node().child_count!=5) rect.fail("rectangle requires four coordinates");
    const auto p=point(fields.required("pt"));
    const auto x=static_cast<long long>(rect.integer(1))+p.x,y=static_cast<long long>(rect.integer(2))+p.y;
    if(x<std::numeric_limits<int>::min() || x>std::numeric_limits<int>::max() ||
       y<std::numeric_limits<int>::min() || y>std::numeric_limits<int>::max()) view.fail("coordinate overflow");
    return {static_cast<int>(x),static_cast<int>(y)};
}
} // namespace

std::string_view TapeDocument::text(std::uint32_t index) const {
    const auto& node=nodes[index];
    if(node.length==escaped_length) return escaped[node.offset];
    return std::string_view(source.data()+node.offset,node.length);
}
std::string_view TapeDocument::tag(std::uint32_t index) const {
    const auto& node=nodes[index];
    if(node.kind!=TapeKind::list || !node.child_count || nodes[index+1].kind!=TapeKind::atom) return {};
    return text(index+1);
}
TapeDocument parse_tape(std::string source) {return TapeParser(std::move(source)).run();}
Schematic decode_tape(const TapeDocument& doc) {
    Schematic result;bool header_seen=false;
    for(std::uint32_t at=0;at<doc.nodes.size();at=doc.nodes[at].next) {
        const View node{doc,at};const auto tag=node.tag();
        if(tag.empty()) node.fail("expected a tagged root list");
        ++result.root_counts[std::string(tag)];
        if(tag=="header") {
            if(header_seen || node.atom(1)!="graphic") node.fail("expected one graphic header");
            header_seen=true;const Fields fields(node);result.version=fields.required("version").atom(1);
        } else if(tag=="pin") {
            const Fields fields(node);
            if(fields.text_count<2) node.fail("pin requires direction text and signal name");
            result.pins.push_back({View{doc,fields.texts[1]}.atom(1),fields.direction(),absolute_point(node,fields),node.location()});
        } else if(tag=="symbol") {
            const Fields fields(node);
            if(fields.text_count<2) node.fail("symbol requires type and instance text");
            const auto rect=fields.required("rect");const long long x=rect.integer(1),y=rect.integer(2);
            Symbol symbol{View{doc,fields.texts[0]}.atom(1),View{doc,fields.texts[1]}.atom(1),{},
                          !fields.parameters.empty(),node.location(),{}};
            for(const auto parameter:fields.parameters) {
                const View p{doc,parameter};symbol.parameters.push_back({p.atom(1),p.atom(2),p.location()});
            }
            for(const auto port:fields.ports) {
                const View pnode{doc,port};const Fields pfields(pnode);
                if(!pfields.text_count) pnode.fail("port requires a name");
                const auto p=point(pfields.required("pt"));
                if(x+p.x<std::numeric_limits<int>::min() || x+p.x>std::numeric_limits<int>::max() ||
                   y+p.y<std::numeric_limits<int>::min() || y+p.y>std::numeric_limits<int>::max()) pnode.fail("coordinate overflow");
                symbol.ports.push_back({View{doc,pfields.texts[0]}.atom(1),pfields.direction(),
                    {static_cast<int>(x+p.x),static_cast<int>(y+p.y)},pnode.location(),pfields.unused});
            }
            result.symbols.push_back(std::move(symbol));
        } else if(tag=="connector") {
            const Fields fields(node);
            if(fields.point_count!=2) node.fail("connector requires exactly two points");
            if(fields.text_count>1) node.fail("connector has multiple labels");
            result.connectors.push_back({point({doc,fields.points[0]}),point({doc,fields.points[1]}),
                fields.text_count ? View{doc,fields.texts[0]}.atom(1) : "",fields.bus,node.location()});
        } else if(tag=="junction") {
            const Fields fields(node);result.junctions.push_back(point(fields.required("pt")));
        }
    }
    if(!header_seen) throw std::runtime_error("missing graphic header");
    return result;
}
Schematic load_tape(const std::string& path) {return decode_tape(parse_tape(read_file(path)));}
} // namespace bdf::experimental
