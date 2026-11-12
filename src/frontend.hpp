#ifndef ROLANGE_FRONTEND_HPP
#define ROLANGE_FRONTEND_HPP

#include <string>
#include <vector>

namespace rolang {

enum class TokenKind { Name, Number, String, Keyword, Operator, Newline, Indent, Dedent, End };
struct Token {
    TokenKind kind;
    std::string text;
    double number = 0.0;
    int line = 1;
    int column = 1;
};

enum class NodeKind { Program, Block, Function, If, While, Return, Pass, Assign, Expression, Literal, Name, Unary, Binary, Call };
struct Node {
    NodeKind kind;
    int line;
    std::string text{};
    double number = 0.0;
    std::vector<std::string> params{};
    std::vector<Node> children{};
    bool string_literal = false;
    int column = 1;
};

std::vector<Token> tokenize(const std::string& source);
Node parse(const std::vector<Token>& tokens);
std::string ast_to_string(const Node& node);
std::string token_kind_name(TokenKind kind);
std::string node_kind_name(NodeKind kind);

} // namespace rolang
#endif
