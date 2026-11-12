#include "rolang.h"

#include <stdlib.h>
#include <string.h>

char *dup_string(const char *text) {
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

static Node *new_node(NodeType type, int line) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        fprintf(stderr, "memory error\n");
        exit(1);
    }
    node->type = type;
    node->line = line;
    return node;
}

Node *new_literal(double value, int line) {
    Node *node = new_node(NODE_LITERAL, line);
    node->number = value;
    return node;
}

Node *new_string_literal(const char *text, int line) {
    Node *node = new_node(NODE_LITERAL, line);
    node->string = dup_string(text);
    return node;
}

Node *new_ident(const char *name, int line) {
    Node *node = new_node(NODE_IDENT, line);
    node->name = dup_string(name);
    return node;
}

Node *new_unary(char op, Node *operand, int line) {
    Node *node = new_node(NODE_UNARY, line);
    node->op = op;
    node->left = operand;
    return node;
}

Node *new_binary(char op, Node *left, Node *right, int line) {
    Node *node = new_node(NODE_BINARY, line);
    node->op = op;
    node->left = left;
    node->right = right;
    return node;
}

Node *new_call(Node *callee, Node **args, int argc, int line) {
    Node *node = new_node(NODE_CALL, line);
    node->callee = callee;
    node->args = args;
    node->argc = argc;
    return node;
}

Node *new_declare(const char *name, Node *value, int line) {
    Node *node = new_node(NODE_DECLARE, line);
    node->name = dup_string(name);
    node->right = value;
    return node;
}

Node *new_assign(const char *name, Node *value, int line) {
    Node *node = new_node(NODE_ASSIGN, line);
    node->name = dup_string(name);
    node->right = value;
    return node;
}

Node *new_block(Node **stmts, int count, int line) {
    Node *node = new_node(NODE_BLOCK, line);
    node->stmts = stmts;
    node->stmt_count = count;
    return node;
}

Node *new_if(Node *condition, Node *then_branch, Node *else_branch, int line) {
    Node *node = new_node(NODE_IF, line);
    node->left = condition;
    node->body = then_branch;
    node->else_body = else_branch;
    return node;
}

Node *new_while(Node *condition, Node *body, int line) {
    Node *node = new_node(NODE_WHILE, line);
    node->left = condition;
    node->body = body;
    return node;
}

Node *new_return(Node *value, int line) {
    Node *node = new_node(NODE_RETURN, line);
    node->left = value;
    return node;
}

Node *new_function(const char *name, char **params, int param_count, Node *body, int line) {
    Node *node = new_node(NODE_FUNCTION, line);
    node->name = dup_string(name);
    node->params = params;
    node->param_count = param_count;
    node->body = body;
    return node;
}

Node *new_program(Node **stmts, int count, int line) {
    Node *node = new_node(NODE_PROGRAM, line);
    node->stmts = stmts;
    node->stmt_count = count;
    return node;
}

Node *new_print(Node *expr, int line) {
    Node *node = new_node(NODE_PRINT, line);
    node->left = expr;
    return node;
}

static void free_node_list(Node **items, int count) {
    if (!items) return;
    for (int i = 0; i < count; ++i) {
        free_node(items[i]);
    }
    free(items);
}

void free_node(Node *node) {
    if (!node) return;
    if (node->name) free(node->name);
    if (node->string) free(node->string);
    if (node->left) free_node(node->left);
    if (node->right) free_node(node->right);
    if (node->body) free_node(node->body);
    if (node->else_body) free_node(node->else_body);
    if (node->callee) free_node(node->callee);
    if (node->args) {
        for (int i = 0; i < node->argc; ++i) {
            free_node(node->args[i]);
        }
        free(node->args);
    }
    if (node->params) {
        for (int i = 0; i < node->param_count; ++i) {
            free(node->params[i]);
        }
        free(node->params);
    }
    if (node->stmts) {
        free_node_list(node->stmts, node->stmt_count);
    }
    free(node);
}
