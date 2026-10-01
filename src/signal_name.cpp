#include "bdf.hpp"
#include <cctype>
#include <limits>
#include <stdexcept>

namespace bdf {
namespace {
constexpr std::size_t max_bits = 4096;
[[noreturn]] void fail(const std::string& source) {
    throw std::runtime_error("unsupported signal name: " + source);
}
}
std::string SignalBit::key() const {
    std::string value = base;
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (index) value += '[' + std::to_string(*index) + ']';
    return value;
}
std::vector<SignalBit> SignalTerm::bits() const {
    if (!indexed) return {{base,std::nullopt}};
    const auto width = left > right ? static_cast<long long>(left)-right+1 :
                                     static_cast<long long>(right)-left+1;
    if (width > static_cast<long long>(max_bits)) fail(base + ": range exceeds 4096 bits");
    std::vector<SignalBit> result;
    for (int i=left;;) {
        result.push_back({base,i});
        if (i==right) break;
        i += left < right ? 1 : -1;
    }
    return result;
}
std::vector<SignalBit> SignalName::bits() const {
    std::vector<SignalBit> result;
    for (const auto& term : terms) {
        auto bits = term.bits();
        if (bits.size() > max_bits-result.size()) fail("group exceeds 4096 bits");
        result.insert(result.end(),bits.begin(),bits.end());
    }
    return result;
}
SignalName parse_signal_name(const std::string& source) {
    std::size_t pos=0;
    auto space = [&] { while (pos<source.size() && std::isspace(static_cast<unsigned char>(source[pos]))) ++pos; };
    auto number = [&] {
        space();
        const auto start=pos;
        int value=0;
        while (pos<source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) {
            const int digit=source[pos++]-'0';
            if (value>(std::numeric_limits<int>::max()-digit)/10) fail(source);
            value=value*10+digit;
        }
        if (pos==start) fail(source);
        space(); return value;
    };
    SignalName result;
    do {
        space(); const auto start=pos;
        if (pos==source.size() || !(std::isalpha(static_cast<unsigned char>(source[pos])) || source[pos]=='_'))
            fail(source);
        ++pos;
        while (pos<source.size() && (std::isalnum(static_cast<unsigned char>(source[pos])) ||
                                    source[pos]=='_' || source[pos]=='$')) ++pos;
        SignalTerm term{source.substr(start,pos-start),false,0,0};
        space();
        if (pos<source.size() && source[pos]=='[') {
            ++pos; term.indexed=true; term.left=term.right=number();
            if (pos+1<source.size() && source[pos]=='.' && source[pos+1]=='.') {
                pos+=2; term.right=number();
            }
            if (pos==source.size() || source[pos++]!=']') fail(source);
            space();
        }
        result.terms.push_back(std::move(term));
        if (pos==source.size()) break;
        if (source[pos++]!=',') fail(source);
        if (pos==source.size()) fail(source);
    } while (true);
    (void)result.bits(); // Enforce resource bounds before exposing a name.
    return result;
}
} // namespace bdf
