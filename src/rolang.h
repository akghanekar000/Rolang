#ifndef ROLANG_H
#define ROLANG_H

#include <stdbool.h>
#include <stddef.h>

#define ROLANG_MAX_ARGS 32

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,
    TOK_KEYWORD,
    TOK_SYMBOL,
    TOK_NEWLINE
} TokenType;

typedef enum {
    KW_LET,
    KW_FN,
    KW_IF,
    KW_ELSE,
    KW_WHILE,
    KW_RETURN,
    KW_PRINT,
    KW_TRUE,
    KW_FALSE,
    KW_NULL,
    KW_AND,
    KW_OR,
    KW_NOT
} Keyword;

typedef struct {
    TokenType type;
    Keyword keyword;
    char *text;
    double number;
    int line;
    int column;
} Token;

typedef enum {
    NODE_LITERAL,
    NODE_IDENT,
    NODE_BINARY,
    NODE_UNARY,
    NODE_CALL,
    NODE_DECLARE,
    NODE_ASSIGN,
    NODE_BLOCK,
    NODE_IF,
    NODE_WHILE,
    NODE_RETURN,
    NODE_FUNCTION,
    NODE_PRINT,
    NODE_PROGRAM
} NodeType;

typedef struct Node Node;

struct Node {
    NodeType type;
    int line;
    char op;
    char *name;
    double number;
    char *string;
    Node *left;
    Node *right;
    Node *body;
    Node *else_body;
    Node *callee;
    Node **args;
    int argc;
    char **params;
    int param_count;
    Node **stmts;
    int stmt_count;
};

Token *tokenize(const char *source, int *count);
void free_tokens(Token *tokens, int count);

Node *new_literal(double value, int line);
Node *new_string_literal(const char *text, int line);
Node *new_ident(const char *name, int line);
Node *new_unary(char op, Node *operand, int line);
Node *new_binary(char op, Node *left, Node *right, int line);
Node *new_call(Node *callee, Node **args, int argc, int line);
Node *new_declare(const char *name, Node *value, int line);
Node *new_assign(const char *name, Node *value, int line);
Node *new_block(Node **stmts, int count, int line);
Node *new_if(Node *condition, Node *then_branch, Node *else_branch, int line);
Node *new_while(Node *condition, Node *body, int line);
Node *new_return(Node *value, int line);
Node *new_function(const char *name, char **params, int param_count, Node *body, int line);
Node *new_program(Node **stmts, int count, int line);
Node *new_print(Node *expr, int line);
void free_node(Node *node);

char *dup_string(const char *text);

#endif
