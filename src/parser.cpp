#include "bdf.hpp"
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace bdf {
namespace {
class Parser {
    const std::string& source;
    std::size_t pos = 0;
    Location loc;
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(std::to_string(loc.line) + ":" +
                                 std::to_string(loc.column) + ": " + message);
    }
    char peek(std::size_t ahead = 0) const {
        return pos + ahead < source.size() ? source[pos + ahead] : '\0';
    }
    char take() {
        char c = source[pos++];
        if (c == '\n') { ++loc.line; loc.column = 1; } else ++loc.column;
        return c;
    }
    void skip() {
        while (pos < source.size()) {
            if (std::isspace(static_cast<unsigned char>(peek()))) { take(); continue; }
            if (peek() == '/' && peek(1) == '/') {
                while (pos < source.size() && take() != '\n') {}
            } else if (peek() == '/' && peek(1) == '*') {
                take(); take();
                while (pos < source.size() && !(peek() == '*' && peek(1) == '/')) take();
                if (pos == source.size()) fail("unterminated block comment");
                take(); take();
            } else break;
        }
    }
    Node expression(std::size_t depth) {
        if (depth > 256) fail("nesting exceeds 256 levels");
        skip();
        if (pos == source.size()) fail("unexpected end of file");
        Node result;
        result.location = loc;
        if (peek() == '(') {
            take(); result.list = true; skip();
            while (pos < source.size() && peek() != ')') {
                result.children.push_back(expression(depth + 1)); skip();
            }
            if (pos == source.size()) fail("unterminated list");
            take();
        } else if (peek() == ')') fail("unexpected closing parenthesis");
        else if (peek() == '"') {
            take(); result.quoted = true;
            while (pos < source.size() && peek() != '"') {
                char c = take();
                if (c == '\\' && (peek() == '"' || peek() == '\\')) c = take();
                result.value += c;
            }
            if (pos == source.size()) fail("unterminated quoted string");
            take();
        } else {
            while (pos < source.size() && !std::isspace(static_cast<unsigned char>(peek())) &&
                   peek() != '(' && peek() != ')' && peek() != '"' &&
                   !(peek() == '/' && (peek(1) == '*' || peek(1) == '/'))) {
                result.value += take();
            }
            if (result.value.empty()) fail("unexpected character");
        }
        return result;
    }
public:
    explicit Parser(const std::string& s) : source(s) {}
    Document run() {
        if (source.size() > 64 * 1024 * 1024) fail("input exceeds 64 MiB");
        Document result;
        skip();
        while (pos < source.size()) { result.push_back(expression(0)); skip(); }
        return result;
    }
};
}
std::string Node::tag() const {
    return list && !children.empty() && !children[0].list && !children[0].quoted
        ? children[0].value : "";
}
Document parse(const std::string& source) { return Parser(source).run(); }
std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot read file: " + path);
    const auto size = input.tellg();
    if (size < 0 || size > 64 * 1024 * 1024)
        throw std::runtime_error("file exceeds 64 MiB or size unavailable: " + path);
    std::string result(static_cast<std::size_t>(size),'\0');
    if (!input.seekg(0)) throw std::runtime_error("cannot seek file: " + path);
    if (!result.empty() && !input.read(result.data(),static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("file changed or read failed: " + path);
    return result;
}
std::vector<const Node*> children(const Node& node, const std::string& tag) {
    std::vector<const Node*> result;
    for (const auto& child : node.children) if (child.tag() == tag) result.push_back(&child);
    return result;
}
const Node& required_child(const Node& node, const std::string& tag) {
    auto matches = children(node, tag);
    if (matches.size() != 1) throw std::runtime_error(
        std::to_string(node.location.line) + ":" + std::to_string(node.location.column) +
        ": expected exactly one " + tag + " in " + node.tag());
    return *matches.front();
}
} // namespace bdf
