#include "bdf_tape.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace bdf::experimental {
namespace {
constexpr auto escaped_length=std::numeric_limits<std::uint32_t>::max();
class SpanParser {
    TapeDocument doc;
    std::size_t pos=0;
    std::uint32_t line=1,column=1;
    std::array<std::uint32_t,257> stack{};
    std::size_t depth=0;
    std::array<unsigned char,256> classes{};
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(std::to_string(line)+":"+std::to_string(column)+": "+message);
    }
    char peek(std::size_t ahead=0) const { return pos+ahead<doc.source.size() ? doc.source[pos+ahead] : '\0'; }
    char take() {
        const char c=doc.source[pos++];
        if(c=='\n') {++line;column=1;} else ++column;
        return c;
    }
    // Source locations are updated once per run, using bounded newline searches.
    void advance(std::size_t end) {
        auto start=pos;
        while(start<end) {
            const auto* found=static_cast<const char*>(std::memchr(doc.source.data()+start,'\n',end-start));
            if(!found) break;
            ++line;column=1;start=static_cast<std::size_t>(found-doc.source.data())+1;
        }
        column+=static_cast<std::uint32_t>(end-start);pos=end;
    }
    void skip() {
        while(pos<doc.source.size()) {
            if(classes[static_cast<unsigned char>(peek())]&1) {
                const auto* cur=doc.source.data()+pos;
                const auto* end=doc.source.data()+doc.source.size();
                do {++cur;} while(cur!=end && (classes[static_cast<unsigned char>(*cur)]&1));
                advance(static_cast<std::size_t>(cur-doc.source.data()));continue;
            }
            if(peek()=='/' && peek(1)=='/') {
                const auto end=doc.source.find('\n',pos+2);
                advance(end==std::string::npos?doc.source.size():end+1);
            } else if(peek()=='/' && peek(1)=='*') {
                const auto end=doc.source.find("*/",pos+2);
                if(end==std::string::npos) {advance(doc.source.size());fail("unterminated block comment");}
                advance(end+2);
            } else break;
        }
    }
public:
    explicit SpanParser(std::string source) {
        doc.source=std::move(source);
        // Capture exactly the same C-locale classification as the reference.
        for(unsigned i=0;i<256;++i) classes[i]=std::isspace(static_cast<unsigned char>(i))?1:0;
        for(unsigned char c:{'(',')','"'})classes[c]|=2;
    }
    TapeDocument run() {
        if(doc.source.size()>64*1024*1024) fail("input exceeds 64 MiB");
        doc.nodes.reserve(std::min<std::size_t>(doc.source.size()/8+1,65536));
        for(;;) {
            skip();
            if(pos==doc.source.size()) {if(depth) fail("unterminated list");break;}
            if(peek()==')') {
                if(!depth) fail("unexpected closing parenthesis");
                take();doc.nodes[stack[--depth]].next=static_cast<std::uint32_t>(doc.nodes.size());continue;
            }
            if(depth>256) fail("nesting exceeds 256 levels");
            const auto index=static_cast<std::uint32_t>(doc.nodes.size());
            if(depth) ++doc.nodes[stack[depth-1]].child_count;
            TapeNode node;node.line=line;node.column=column;node.offset=static_cast<std::uint32_t>(pos);
            if(peek()=='(') {take();node.kind=TapeKind::list;doc.nodes.push_back(node);stack[depth++]=index;continue;}
            if(peek()=='"') {
                take();node.kind=TapeKind::quoted;const auto start=pos;
                bool owns=false;std::string decoded;
                for(;;) {
                    // Scan the ordinary span without per-byte position updates.
                    const auto* cur=doc.source.data()+pos;
                    const auto* end=doc.source.data()+doc.source.size();
                    while(cur!=end && *cur!='"' && *cur!='\\')++cur;
                    const auto boundary=static_cast<std::size_t>(cur-doc.source.data());
                    if(owns)decoded.append(doc.source,pos,boundary-pos);
                    advance(boundary);
                    if(pos==doc.source.size())fail("unterminated quoted string");
                    if(peek()=='"')break;
                    const auto before=pos;char c=take();
                    if(peek()=='"' || peek()=='\\') {
                        if(!owns){decoded.assign(doc.source,start,before-start);owns=true;}
                        c=take();
                    }
                    if(owns)decoded+=c;
                }
                if(owns){node.offset=static_cast<std::uint32_t>(doc.escaped.size());node.length=escaped_length;doc.escaped.push_back(std::move(decoded));}
                else {node.offset=static_cast<std::uint32_t>(start);node.length=static_cast<std::uint32_t>(pos-start);}
                take();
            } else {
                const auto start=pos;
                const auto* cur=doc.source.data()+pos;
                const auto* end=doc.source.data()+doc.source.size();
                while(cur!=end && !classes[static_cast<unsigned char>(*cur)] &&
                      !(*cur=='/' && cur+1!=end && (cur[1]=='*' || cur[1]=='/')))++cur;
                pos=static_cast<std::size_t>(cur-doc.source.data());
                if(pos==start)fail("unexpected character");
                column+=static_cast<std::uint32_t>(pos-start);node.length=static_cast<std::uint32_t>(pos-start);
            }
            node.next=index+1;doc.nodes.push_back(node);
        }
        return std::move(doc);
    }
};
}
TapeDocument parse_span_tape(std::string source) {
    // Avoid table setup for tiny files; keep the original scanner as fallback.
    if(source.size()<1024) return parse_tape(std::move(source));
    return SpanParser(std::move(source)).run();
}
}
