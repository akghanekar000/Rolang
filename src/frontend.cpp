#include "frontend.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace rolang {
namespace {
const std::unordered_set<std::string> keywords = {"def", "return", "if", "else", "while", "pass", "and", "or", "not", "True", "False", "None"};
const std::vector<std::string> operators = {"**", "//", "==", "!=", "<=", ">=", "+", "-", "*", "/", "%", "<", ">", "=", "(", ")", ",", ":"};

bool starts(const std::string& s, std::size_t at, const std::string& value) { return s.compare(at, value.size(), value) == 0; }
std::string location(int line, int column) { return std::to_string(line) + ":" + std::to_string(column); }

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {
        if (tokens_.empty() || tokens_.back().kind != TokenKind::End)
            throw std::runtime_error("parser received an incomplete token stream");
    }
    Node program() {
        Node root{NodeKind::Program, peek().line};
        root.column = peek().column;
        while (peek().kind != TokenKind::End) {
            if (match(TokenKind::Newline)) continue;
            root.children.push_back(statement());
        }
        validate_integer_literals(root);
        return root;
    }
private:
    const std::vector<Token>& tokens_;
    std::size_t pos_ = 0;
    int function_depth_ = 0;
    static void validate_integer_literals(const Node& node) {
        if (node.integer_literal && node.text == "9223372036854775808")
            throw std::runtime_error(location(node.line, node.column) + ": integer literal out of range");
        for (const auto& child : node.children) validate_integer_literals(child);
    }
    const Token& peek(std::size_t offset = 0) const { return tokens_[std::min(pos_ + offset, tokens_.size() - 1)]; }
    Token take() { Token t = peek(); if (pos_ + 1 < tokens_.size()) ++pos_; return t; }
    bool match(TokenKind kind) { if (peek().kind == kind) { take(); return true; } return false; }
    bool match_text(const std::string& text) { if (peek().text == text) { take(); return true; } return false; }
    Token expect(TokenKind kind, const std::string& what) {
        if (peek().kind == kind) return take();
        throw std::runtime_error(location(peek().line, peek().column) + ": expected " + what + ", got '" + peek().text + "'");
    }
    Token expect_text(const std::string& text) {
        if (peek().text == text) return take();
        throw std::runtime_error(location(peek().line, peek().column) + ": expected '" + text + "', got '" + peek().text + "'");
    }
    Node block(int line, int column) {
        expect(TokenKind::Newline, "newline after ':'");
        expect(TokenKind::Indent, "an indented block");
        Node n{NodeKind::Block, line};
        while (peek().kind != TokenKind::Dedent && peek().kind != TokenKind::End) {
            if (match(TokenKind::Newline)) continue;
            n.children.push_back(statement());
        }
        expect(TokenKind::Dedent, "end of indented block");
        if (n.children.empty()) throw std::runtime_error(location(line, column) + ": empty block; add 'pass'");
        return n;
    }
    Node statement() {
        Token start = peek();
        if (match_text("def")) {
            Token name = expect(TokenKind::Name, "function name");
            expect_text("(");
            Node n{NodeKind::Function, start.line}; n.text = name.text;
            n.column = start.column;
            if (peek().text != ")") {
                while (true) {
                    n.params.push_back(expect(TokenKind::Name, "parameter name").text);
                    if (!match_text(",")) break;
                    if (peek().text == ")") break;
                }
            }
            expect_text(")"); expect_text(":");
            for (std::size_t i = 0; i < n.params.size(); ++i)
                for (std::size_t j = i + 1; j < n.params.size(); ++j)
                    if (n.params[i] == n.params[j]) throw std::runtime_error(location(name.line, name.column) + ": duplicate parameter");
            ++function_depth_;
            n.children.push_back(block(start.line, start.column));
            --function_depth_;
            return n;
        }
        if (match_text("if")) {
            Node n{NodeKind::If, start.line}; n.column = start.column; n.children.push_back(expression()); expect_text(":"); n.children.push_back(block(start.line, start.column));
            if (match_text("else")) { expect_text(":"); n.children.push_back(block(start.line, start.column)); }
            return n;
        }
        if (match_text("while")) {
            Node n{NodeKind::While, start.line}; n.column = start.column; n.children.push_back(expression()); expect_text(":"); n.children.push_back(block(start.line, start.column)); return n;
        }
        if (match_text("return")) {
            if (function_depth_ == 0)
                throw std::runtime_error(location(start.line, start.column) + ": return outside a function");
            Node n{NodeKind::Return, start.line};
            n.column = start.column;
            if (peek().kind != TokenKind::Newline) n.children.push_back(expression());
            expect(TokenKind::Newline, "end of line"); return n;
        }
        if (match_text("pass")) { expect(TokenKind::Newline, "end of line"); Node n{NodeKind::Pass, start.line}; n.column = start.column; return n; }
        if (peek().kind == TokenKind::Name && peek(1).text == "=") {
            Node n{NodeKind::Assign, start.line}; n.column = start.column; n.text = take().text; take(); n.children.push_back(expression());
            expect(TokenKind::Newline, "end of line"); return n;
        }
        Node n{NodeKind::Expression, start.line}; n.column = start.column; n.children.push_back(expression());
        expect(TokenKind::Newline, "end of line"); return n;
    }
    int precedence(const Token& t) const {
        if (t.text == "or") return 1;
        if (t.text == "and") return 2;
        if (t.text == "==" || t.text == "!=") return 3;
        if (t.text == "<" || t.text == "<=" || t.text == ">" || t.text == ">=") return 4;
        if (t.text == "+" || t.text == "-") return 5;
        if (t.text == "*" || t.text == "/" || t.text == "//" || t.text == "%") return 6;
        if (t.text == "**") return 8;
        return -1;
    }
    Node expression(int min_precedence = 0) {
        Token t = take();
        Node left{NodeKind::Literal, t.line};
        left.column = t.column;
        if (t.kind == TokenKind::Number) {
            left.number = t.number;
            left.integer = t.integer;
            left.integer_literal = t.integer_literal;
            if (t.integer_literal && t.text == "9223372036854775808") left.text = t.text;
        }
        else if (t.kind == TokenKind::String) { left.text = t.text; left.kind = NodeKind::Literal; left.number = 0; left.string_literal = true; }
        else if (t.kind == TokenKind::Name) { left.kind = NodeKind::Name; left.text = t.text; }
        else if (t.text == "True" || t.text == "False" || t.text == "None") { left.kind = NodeKind::Literal; left.text = t.text; }
        else if (t.text == "not" || t.text == "-" || t.text == "+") {
            left.kind = NodeKind::Unary;
            left.text = t.text;
            left.children.push_back(expression(t.text == "not" ? 3 : 7));
            if (t.text == "-" && left.children[0].integer_literal &&
                left.children[0].text == "9223372036854775808") {
                left.kind = NodeKind::Literal;
                left.text.clear();
                left.integer = std::numeric_limits<std::int64_t>::min();
                left.integer_literal = true;
                left.children.clear();
            }
        }
        else if (t.text == "(") { left = expression(); expect_text(")"); }
        else throw std::runtime_error(location(t.line, t.column) + ": expected expression, got '" + t.text + "'");
        while (true) {
            if (peek().text == "(" && 9 >= min_precedence) {
                Token call = take(); Node n{NodeKind::Call, call.line}; n.column = call.column; n.children.push_back(left);
                if (peek().text != ")") {
                    while (true) {
                        n.children.push_back(expression());
                        if (!match_text(",")) break;
                        if (peek().text == ")") break;
                    }
                }
                expect_text(")"); left = std::move(n); continue;
            }
            int p = precedence(peek()); if (p < min_precedence) break;
            Token op = take(); Node n{NodeKind::Binary, op.line}; n.column = op.column; n.text = op.text;
            n.children.push_back(left); n.children.push_back(expression(p + (op.text == "**" ? 0 : 1))); left = std::move(n);
        }
        return left;
    }
};

void ast_print(const Node& n, std::ostringstream& out, int depth) {
    out << std::string(static_cast<std::size_t>(depth) * 2, ' ') << node_kind_name(n.kind) << " line=" << n.line;
    if (n.kind == NodeKind::Literal && n.integer_literal) out << " value=" << n.integer;
    else if (n.kind == NodeKind::Literal && n.string_literal) out << " value=" << std::quoted(n.text);
    else if (!n.text.empty()) out << " value=" << std::quoted(n.text);
    if (n.kind == NodeKind::Literal && n.text.empty() && !n.string_literal && !n.integer_literal) out << " value=" << n.number;
    if (!n.params.empty()) { out << " params=["; for (std::size_t i=0;i<n.params.size();++i) { if(i) out << ", "; out << n.params[i]; } out << "]"; }
    out << '\n'; for (const auto& child : n.children) ast_print(child, out, depth + 1);
}
} // namespace

std::vector<Token> tokenize(const std::string& source) {
    std::vector<Token> out;
    std::vector<int> indents{0};
    std::vector<Token> open_parens;
    int line_no = 0;
    std::istringstream input(source); std::string line;
    while (std::getline(input, line)) {
        ++line_no;
        std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        if (open_parens.empty()) {
            for (std::size_t j = 0; j < first; ++j) {
                if (line[j] == '\t')
                    throw std::runtime_error(location(line_no, static_cast<int>(j) + 1) + ": tabs are not allowed in indentation");
            }
            int indent = static_cast<int>(first);
            if (indent > indents.back()) { indents.push_back(indent); out.push_back({TokenKind::Indent, "<indent>", 0, line_no, 1}); }
            else if (indent < indents.back()) {
                while (indents.size() > 1 && indent < indents.back()) { indents.pop_back(); out.push_back({TokenKind::Dedent, "<dedent>", 0, line_no, 1}); }
                if (indent != indents.back()) throw std::runtime_error(location(line_no, static_cast<int>(first)+1) + ": inconsistent indentation");
            }
        }
        std::size_t i = open_parens.empty() ? first : 0;
        while (i < line.size()) {
            unsigned char ch = static_cast<unsigned char>(line[i]);
            if (std::isspace(ch)) { ++i; continue; }
            if (line[i] == '#') break;
            int col = static_cast<int>(i + 1);
            if (line[i] == '\'' || line[i] == '"') {
                char quote = line[i++]; std::string value; bool closed = false;
                while (i < line.size()) {
                    char c = line[i++]; if (c == quote) { closed = true; break; }
                    if (c == '\\') {
                        if (i == line.size())
                            throw std::runtime_error(location(line_no, col) + ": incomplete escape sequence in string literal");
                        char e = line[i++]; switch (e) { case 'n': value += '\n'; break; case 't': value += '\t'; break; case 'r': value += '\r'; break; case '\\': value += '\\'; break; case '\'': value += '\''; break; case '"': value += '"'; break; default: throw std::runtime_error(location(line_no, static_cast<int>(i)) + ": invalid escape sequence"); }
                    } else value += c;
                }
                if (!closed) throw std::runtime_error(location(line_no, col) + ": unterminated string literal");
                out.push_back({TokenKind::String, value, 0, line_no, col}); continue;
            }
            if (std::isdigit(ch) || (line[i] == '.' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i+1])))) {
                const std::size_t begin = i;
                while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
                if (i < line.size() && line[i] == '.') {
                    ++i;
                    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
                }
                if (i < line.size() && (line[i] == 'e' || line[i] == 'E')) {
                    ++i;
                    if (i < line.size() && (line[i] == '+' || line[i] == '-')) ++i;
                    const std::size_t exponent_start = i;
                    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
                    if (i == exponent_start)
                        throw std::runtime_error(location(line_no, col) + ": malformed numeric literal");
                }
                if (i < line.size() && (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_' || line[i] == '.'))
                    throw std::runtime_error(location(line_no, col) + ": malformed numeric literal");
                std::string text = line.substr(begin, i-begin);
                const bool integer_literal = text.find_first_of(".eE") == std::string::npos;
                Token token{TokenKind::Number, text, 0.0, line_no, col};
                token.integer_literal = integer_literal;
                if (integer_literal) {
                    constexpr std::uint64_t max_magnitude =
                        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
                    std::uint64_t magnitude = 0;
                    for (char digit : text) {
                        const unsigned int value = static_cast<unsigned int>(digit - '0');
                        if (magnitude > (max_magnitude - value) / 10)
                            throw std::runtime_error(location(line_no, col) + ": integer literal out of range");
                        magnitude = magnitude * 10 + value;
                    }
                    if (magnitude <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                        token.integer = static_cast<std::int64_t>(magnitude);
                } else {
                    char* end = nullptr;
                    token.number = std::strtod(text.c_str(), &end);
                    if (end != text.c_str() + text.size() || !std::isfinite(token.number))
                        throw std::runtime_error(location(line_no, col) + ": malformed numeric literal");
                }
                out.push_back(std::move(token)); continue;
            }
            if (std::isalpha(ch) || line[i] == '_') {
                std::size_t begin = i++;
                while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;
                std::string text = line.substr(begin, i-begin);
                out.push_back({keywords.count(text) ? TokenKind::Keyword : TokenKind::Name, text, 0, line_no, col}); continue;
            }
            bool found = false;
            for (const auto& op : operators) if (starts(line, i, op)) {
                out.push_back({TokenKind::Operator, op, 0, line_no, col});
                if (op == "(") open_parens.push_back({TokenKind::Operator, op, 0, line_no, col});
                else if (op == ")") {
                    if (open_parens.empty())
                        throw std::runtime_error(location(line_no, col) + ": unmatched ')'");
                    open_parens.pop_back();
                }
                i += op.size(); found = true; break;
            }
            if (!found) throw std::runtime_error(location(line_no, col) + ": unexpected character '" + line[i] + "'");
        }
        if (open_parens.empty()) out.push_back({TokenKind::Newline, "<newline>", 0, line_no, static_cast<int>(line.size()+1)});
    }
    if (!open_parens.empty()) {
        const Token& open = open_parens.back();
        throw std::runtime_error(location(open.line, open.column) + ": unclosed '('");
    }
    if (out.empty() || out.back().kind != TokenKind::Newline) out.push_back({TokenKind::Newline, "<newline>", 0, line_no ? line_no : 1, 1});
    while (indents.size() > 1) { indents.pop_back(); out.push_back({TokenKind::Dedent, "<dedent>", 0, line_no+1, 1}); }
    out.push_back({TokenKind::End, "<eof>", 0, line_no+1, 1}); return out;
}

Node parse(const std::vector<Token>& tokens) { return Parser(tokens).program(); }
std::string token_kind_name(TokenKind k) {
    switch(k) { case TokenKind::Name:return "NAME"; case TokenKind::Number:return "NUMBER"; case TokenKind::String:return "STRING"; case TokenKind::Keyword:return "KEYWORD"; case TokenKind::Operator:return "OP"; case TokenKind::Newline:return "NEWLINE"; case TokenKind::Indent:return "INDENT"; case TokenKind::Dedent:return "DEDENT"; case TokenKind::End:return "EOF"; } return "?";
}
std::string node_kind_name(NodeKind k) {
    switch(k) { case NodeKind::Program:return "Program"; case NodeKind::Block:return "Block"; case NodeKind::Function:return "Function"; case NodeKind::If:return "If"; case NodeKind::While:return "While"; case NodeKind::Return:return "Return"; case NodeKind::Pass:return "Pass"; case NodeKind::Assign:return "Assign"; case NodeKind::Expression:return "Expression"; case NodeKind::Literal:return "Literal"; case NodeKind::Name:return "Name"; case NodeKind::Unary:return "Unary"; case NodeKind::Binary:return "Binary"; case NodeKind::Call:return "Call"; } return "?";
}
std::string ast_to_string(const Node& node) { std::ostringstream out; ast_print(node, out, 0); return out.str(); }
} // namespace rolang
