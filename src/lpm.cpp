#include "bdf.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>

namespace bdf {
namespace {
[[noreturn]] void unsupported(const std::string& message) {
    throw std::runtime_error("unsupported HDL conversion: " + message);
}
std::string upper(std::string value) {
    for (auto& c : value) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}
std::string trim(const std::string& value) {
    const auto start=value.find_first_not_of(" \r\n\t");
    if (start==std::string::npos) return {};
    return value.substr(start,value.find_last_not_of(" \r\n\t")-start+1);
}
class IntegerExpression {
    const std::string& text;
    const std::function<int(const std::string&)>& lookup;
    std::size_t pos=0,depth=0;
    void space() { while(pos<text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos; }
    int bound(std::int64_t value) {
        if (value>std::numeric_limits<int>::max() || value< -std::numeric_limits<int>::max())
            unsupported("integer expression overflow: " + text);
        return static_cast<int>(value);
    }
    int primary() {
        space();
        if (++depth>32 || pos==text.size()) unsupported("invalid integer expression: " + text);
        int value=0;
        const char c=text[pos];
        if (c=='(') {
            ++pos; value=sum(); space();
            if (pos==text.size() || text[pos++]!=')') unsupported("invalid integer expression: " + text);
        } else if(c=='+' || c=='-') {
            ++pos; value=primary(); if(c=='-') value=-value;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            while(pos<text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
                value=bound(static_cast<std::int64_t>(value)*10+(text[pos++]-'0'));
        } else if(std::isalpha(static_cast<unsigned char>(c)) || c=='_') {
            const auto start=pos++;
            while(pos<text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos]=='_')) ++pos;
            value=lookup(upper(text.substr(start,pos-start)));
        } else unsupported("invalid integer expression: " + text);
        --depth; return value;
    }
    int product() {
        int value=primary(); space();
        while(pos<text.size() && (text[pos]=='*' || text[pos]=='/' || text[pos]=='%')) {
            const char op=text[pos++]; const int rhs=primary();
            if ((op=='/' || op=='%') && rhs==0) unsupported("division by zero in integer expression: " + text);
            if (op=='*') value=bound(static_cast<std::int64_t>(value)*rhs);
            else if(op=='/') value/=rhs;
            else value%=rhs;
            space();
        }
        return value;
    }
    int sum() {
        int value=product(); space();
        while(pos<text.size() && (text[pos]=='+' || text[pos]=='-')) {
            const char op=text[pos++]; const int rhs=product();
            value=bound(static_cast<std::int64_t>(value)+(op=='+' ? rhs : -static_cast<std::int64_t>(rhs)));
            space();
        }
        return value;
    }
public:
    IntegerExpression(const std::string& source,const std::function<int(const std::string&)>& names)
        :text(source),lookup(names) {}
    int run() {
        if(text.size()>4096) unsupported("integer expression exceeds 4096 characters");
        const int result=sum(); space();
        if(pos!=text.size()) unsupported("invalid integer expression: " + text);
        return result;
    }
};
std::string string_value(const std::string& value) {
    auto doc=parse("(value "+value+")");
    if (doc.size()!=1 || doc[0].children.size()!=2 || !doc[0].children[1].quoted)
        unsupported("expected quoted LPM parameter: " + value);
    const auto& result=doc[0].children[1].value;
    for(unsigned char c : result) if(c<32) unsupported("control character in LPM parameter");
    return result;
}
std::string verilog_string(const std::string& value) {
    std::string result="\"";
    for(char c : value) { if(c=='"' || c=='\\') result+='\\'; result+=c; }
    return result+'"';
}
}
int evaluate_integer(const std::string& expression,const std::map<std::string,std::string>& parameters) {
    std::set<std::string> active;
    std::function<int(const std::string&)> lookup;
    lookup=[&](const std::string& name) {
        const auto found=parameters.find(name);
        if(found==parameters.end()) unsupported("unknown integer parameter " + name);
        if(active.size()>32 || !active.insert(name).second) unsupported("recursive integer parameter " + name);
        const int value=IntegerExpression(found->second,lookup).run();
        active.erase(name); return value;
    };
    return IntegerExpression(expression,lookup).run();
}
bool is_lpm(const std::string& type) {
    const auto name=upper(type);
    return name=="LPM_COUNTER" || name=="LPM_ROM";
}
Symbol prepare_lpm(Symbol symbol,const std::string& source_directory) {
    if(!is_lpm(symbol.type)) return symbol;
    const bool counter=upper(symbol.type)=="LPM_COUNTER";
    const std::set<std::string> numeric=counter ? std::set<std::string>{"LPM_WIDTH","LPM_MODULUS"} :
        std::set<std::string>{"LPM_WIDTH","LPM_WIDTHAD","LPM_NUMWORDS"};
    const std::set<std::string> strings=counter ?
        std::set<std::string>{"LPM_DIRECTION","LPM_AVALUE","LPM_SVALUE","LPM_PVALUE","LPM_PORT_UPDOWN","LPM_TYPE","LPM_HINT"} :
        std::set<std::string>{"LPM_ADDRESS_CONTROL","LPM_OUTDATA","LPM_FILE","LPM_TYPE","LPM_HINT","INTENDED_DEVICE_FAMILY"};
    std::map<std::string,std::string> values;
    for(const auto& p : symbol.parameters) {
        const auto name=upper(p.name);
        if(!numeric.count(name) && !strings.count(name)) unsupported("LPM parameter " + p.name);
        if(!values.emplace(name,trim(p.value)).second) unsupported("duplicate LPM parameter " + p.name);
    }
    auto number=[&](const std::string& name,int fallback) {
        if(!values.count(name) || values.at(name).empty()) return fallback;
        return evaluate_integer(values.at(name),values);
    };
    const int width=number("LPM_WIDTH",0),widthad=counter ? 0 : number("LPM_WIDTHAD",0);
    if(width<1 || width>4096 || (!counter && (widthad<1 || widthad>30))) unsupported("invalid LPM width on " + symbol.instance);
    if(counter && number("LPM_MODULUS",0)<0) unsupported("negative LPM modulus");
    if(!counter) {
        const auto words=number("LPM_NUMWORDS",0);
        if(words<0 || words>(std::int64_t{1}<<widthad) || (words>0 && words<=(std::int64_t{1}<<(widthad-1))))
            unsupported("invalid LPM_NUMWORDS on " + symbol.instance);
    }
    std::map<std::string,std::string> constants;
    for(const auto& name : numeric)
        if(values.count(name) && !values.at(name).empty()) constants[name]=std::to_string(number(name,0));
    constants["LPM_WIDTH"]=std::to_string(width);
    if(!counter) constants["LPM_WIDTHAD"]=std::to_string(widthad);
    symbol.parameters.clear();
    for(const auto& [name,raw] : values) {
        if(raw.empty()) continue;
        std::string value;
        if(numeric.count(name)) value=constants.at(name);
        else {
            auto text=string_value(raw);
            if(name=="LPM_DIRECTION" && text!="UP" && text!="DOWN" && text!="DEFAULT" && text!="UNUSED") unsupported("invalid LPM_DIRECTION");
            if((name=="LPM_ADDRESS_CONTROL" || name=="LPM_OUTDATA") && text!="REGISTERED" && text!="UNREGISTERED") unsupported("invalid LPM register mode");
            if(name=="LPM_FILE" && !text.empty() && text!="UNUSED" && !source_directory.empty()) {
                std::filesystem::path file(text);
                if(file.is_relative()) file=std::filesystem::path(source_directory)/file;
                if(!std::filesystem::is_regular_file(file)) unsupported("memory file not found: " + file.string());
                text=std::filesystem::absolute(file).lexically_normal().string();
            }
            value=verilog_string(text);
        }
        symbol.parameters.push_back({name,value,{}});
    }
    std::map<std::string,std::pair<std::string,int>> expected;
    if(counter) {
        for(const auto* name : {"clock","clk_en","cnt_en","updown","aclr","aset","aload","sclr","sset","sload","cin"})
            expected[name]={"input",0};
        expected["data"]={"input",width}; expected["q"]={"output",width};
        expected["cout"]={"output",0}; expected["eq"]={"output",16};
    } else {
        expected["address"]={"input",widthad}; expected["q"]={"output",width};
        for(const auto* name : {"inclock","outclock","memenab"}) expected[name]={"input",0};
    }
    std::set<std::string> seen;
    for(auto& port : symbol.ports) {
        const auto bracket=port.name.find('[');
        const auto base=port.name.substr(0,bracket);
        const auto found=expected.find(base);
        if(found==expected.end() || !seen.insert(base).second || found->second.first!=port.direction)
            unsupported("LPM port shape on " + symbol.instance + "." + port.name);
        if(found->second.second==0) {
            if(bracket!=std::string::npos) unsupported("indexed scalar LPM port " + port.name);
            continue;
        }
        if(bracket==std::string::npos || port.name.back()!=']') unsupported("missing LPM vector range " + port.name);
        const auto range=port.name.substr(bracket+1,port.name.size()-bracket-2);
        const auto separator=range.find("..");
        if(separator==std::string::npos) unsupported("invalid LPM vector range " + port.name);
        const int left=evaluate_integer(range.substr(0,separator),constants);
        const int right=evaluate_integer(range.substr(separator+2),constants);
        if(left<0 || right<0 || std::abs(static_cast<long long>(left)-right)+1!=found->second.second)
            unsupported("LPM port width mismatch on " + port.name);
        port.name=base+'['+std::to_string(left)+".."+std::to_string(right)+']';
    }
    if(!seen.count("q") || (!counter && !seen.count("address"))) unsupported("missing LPM data interface");
    return symbol;
}
} // namespace bdf
