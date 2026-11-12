#include "rolang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Token *tokens;
    int index;
    int count;
} Parser;

static Token parser_peek(Parser *p) {
    if (p->index >= p->count) return p->tokens[p->count - 1];
    return p->tokens[p->index];
}

static Token parser_advance(Parser *p) {
    Token tok = parser_peek(p);
    if (p->index < p->count - 1) {
        p->index++;
    }
    return tok;
}

static bool parser_match(Parser *p, TokenType type) {
    if (parser_peek(p).type == type) {
        parser_advance(p);
        return true;
    }
    return false;
}

static Token parser_expect(Parser *p, TokenType type, const char *message) {
    Token tok = parser_peek(p);
    if (tok.type == type) {
        return parser_advance(p);
    }
    fprintf(stderr, "%d:%d: %s (got %d)\n", tok.line, tok.column, message, tok.type);
    exit(1);
}

static Node *parse_expression(Parser *p);

static Node *parse_primary(Parser *p) {
    Token tok = parser_peek(p);
    parser_advance(p);

    if (tok.type == TOK_NUMBER) {
        return new_literal(tok.number, tok.line);
    }
    if (tok.type == TOK_STRING) {
        return new_string_literal(tok.text, tok.line);
    }
    if (tok.type == TOK_IDENT) {
        return new_ident(tok.text, tok.line);
    }
    if (tok.type == TOK_KEYWORD) {
        if (tok.keyword == KW_TRUE) return new_literal(1.0, tok.line);
        if (tok.keyword == KW_FALSE) return new_literal(0.0, tok.line);
        if (tok.keyword == KW_NULL) return new_literal(0.0, tok.line);
    }
    if (tok.type == TOK_SYMBOL && strcmp(tok.text, "(") == 0) {
        Node *expr = parse_expression(p);
        parser_expect(p, TOK_SYMBOL, "expected ')' after expression");
        return expr;
    }

    fprintf(stderr, "%d:%d: expected expression\n", tok.line, tok.column);
    exit(1);
}

static Node *parse_call(Parser *p, Node *callee) {
    Node **args = NULL;
    int argc = 0;
    if (!parser_match(p, TOK_SYMBOL)) {
        return callee;
    }
    // this parser uses a simple raw symbol check because the actual token is already consumed above
    if (strcmp(p->tokens[p->index - 1].text, "(") != 0) {
        // not a call, restore index by one step? this branch should not happen in normal flow
    }
    if (!parser_match(p, TOK_SYMBOL)) {
        // no brackets; the call must have been consumed already
    }
    // direct guard for the next token; the function is designed to be concise and safe in CLI usage
    if (parser_peek(p).type == TOK_SYMBOL && strcmp(parser_peek(p).text, ")") == 0) {
        parser_advance(p);
        return new_call(callee, args, argc, callee->line);
    }
    while (true) {
        Node *arg = parse_expression(p);
        args = realloc(args, (argc + 1) * sizeof(Node *));
        args[argc++] = arg;
        if (!parser_match(p, TOK_SYMBOL)) {
            break;
        }
        if (strcmp(p->tokens[p->index - 1].text, ",") != 0) {
            break;
        }
    }
    parser_expect(p, TOK_SYMBOL, "expected ')' after arguments");
    return new_call(callee, args, argc, callee->line);
}

static int precedence_of(const char *text) {
    if (!text) return 0;
    if (strcmp(text, "||") == 0) return 1;
    if (strcmp(text, "&&") == 0) return 2;
    if (strcmp(text, "==") == 0 || strcmp(text, "!=") == 0) return 3;
    if (strcmp(text, "<") == 0 || strcmp(text, "<=") == 0 || strcmp(text, ">") == 0 || strcmp(text, ">=") == 0) return 4;
    if (strcmp(text, "+") == 0 || strcmp(text, "-") == 0) return 5;
    if (strcmp(text, "*") == 0 || strcmp(text, "/") == 0 || strcmp(text, "%") == 0) return 6;
    return 0;
}

static Node *parse_unary(Parser *p) {
    Token tok = parser_peek(p);
    if (tok.type == TOK_KEYWORD && tok.keyword == KW_NOT) {
        parser_advance(p);
        return new_unary('!', parse_unary(p), tok.line);
    }
    if (tok.type == TOK_SYMBOL && (strcmp(tok.text, "-") == 0 || strcmp(tok.text, "+") == 0)) {
        parser_advance(p);
        return new_unary(tok.text[0], parse_unary(p), tok.line);
    }
    return parse_primary(p);
}

static Node *parse_expression(Parser *p) {
    Node *left = parse_unary(p);
    while (true) {
        Token tok = parser_peek(p);
        if (tok.type != TOK_SYMBOL) break;
        int prec = precedence_of(tok.text);
        if (prec == 0) break;
        if (strcmp(tok.text, "(") == 0) {
            left = parse_call(p, left);
            continue;
        }
        parser_advance(p);
        Node *right = parse_unary(p);
        left = new_binary(tok.text[0], left, right, tok.line);
    }
    return left;
}

static Node *parse_block(Parser *p) {
    Token open = parser_expect(p, TOK_SYMBOL, "expected '{' to open block");
    if (strcmp(open.text, "{") != 0) {
        fprintf(stderr, "%d:%d: expected '{'\n", open.line, open.column);
        exit(1);
    }

    Node **stmts = NULL;
    int count = 0;
    while (parser_peek(p).type != TOK_EOF && !(parser_peek(p).type == TOK_SYMBOL && strcmp(parser_peek(p).text, "}") == 0)) {
        if (parser_peek(p).type == TOK_NEWLINE) {
            parser_advance(p);
            continue;
        }
        Node *stmt = NULL;
        Token t = parser_peek(p);

        if (t.type == TOK_KEYWORD && t.keyword == KW_LET) {
            parser_advance(p);
            Token nameTok = parser_expect(p, TOK_IDENT, "expected variable name");
            parser_expect(p, TOK_SYMBOL, "expected '=' after identifier");
            Node *value = parse_expression(p);
            parser_expect(p, TOK_SYMBOL, "expected ';' after declaration");
            stmt = new_declare(nameTok.text, value, nameTok.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_PRINT) {
            parser_advance(p);
            parser_expect(p, TOK_SYMBOL, "expected '(' after print");
            Node *arg = parse_expression(p);
            parser_expect(p, TOK_SYMBOL, "expected ')' after print arg");
            stmt = new_print(arg, t.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_RETURN) {
            parser_advance(p);
            Node *value = NULL;
            if (!(parser_peek(p).type == TOK_SYMBOL && strcmp(parser_peek(p).text, ";") == 0)) {
                value = parse_expression(p);
            }
            parser_expect(p, TOK_SYMBOL, "expected ';' after return");
            stmt = new_return(value, t.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_IF) {
            parser_advance(p);
            Node *cond = parse_expression(p);
            Node *then_branch = parse_block(p);
            Node *else_branch = NULL;
            if (parser_peek(p).type == TOK_KEYWORD && parser_peek(p).keyword == KW_ELSE) {
                parser_advance(p);
                else_branch = parse_block(p);
            }
            stmt = new_if(cond, then_branch, else_branch, t.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_WHILE) {
            parser_advance(p);
            Node *cond = parse_expression(p);
            Node *body = parse_block(p);
            stmt = new_while(cond, body, t.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_FN) {
            parser_advance(p);
            Token fn_name = parser_expect(p, TOK_IDENT, "expected function name");
            parser_expect(p, TOK_SYMBOL, "expected '(' after function name");
            char **params = NULL;
            int param_count = 0;
            if (!(parser_peek(p).type == TOK_SYMBOL && strcmp(parser_peek(p).text, ")") == 0)) {
                while (true) {
                    Token p_name = parser_expect(p, TOK_IDENT, "expected parameter name");
                    params = realloc(params, (param_count + 1) * sizeof(char *));
                    params[param_count++] = dup_string(p_name.text);
                    if (parser_peek(p).type == TOK_SYMBOL && strcmp(parser_peek(p).text, ",") == 0) {
                        parser_advance(p);
                        continue;
                    }
                    break;
                }
            }
            parser_expect(p, TOK_SYMBOL, "expected ')' after parameters");
            Node *body = parse_block(p);
            stmt = new_function(fn_name.text, params, param_count, body, fn_name.line);
        } else {
            Node *expr = parse_expression(p);
            if (parser_peek(p).type == TOK_SYMBOL && strcmp(parser_peek(p).text, "=") == 0) {
                parser_advance(p);
                Node *value = parse_expression(p);
                stmt = new_assign(expr->name, value, expr->line);
                free_node(expr);
            } else {
                stmt = expr;
            }
            if (parser_peek(p).type == TOK_SYMBOL && strcmp(parser_peek(p).text, ";") == 0) {
                parser_advance(p);
            }
        }

        stmts = realloc(stmts, (count + 1) * sizeof(Node *));
        stmts[count++] = stmt;
    }

    parser_expect(p, TOK_SYMBOL, "expected '}' to close block");
    return new_block(stmts, count, open.line);
}

Node *parse_program(Token *tokens, int count) {
    Parser p = {tokens, 0, count};
    Node **stmts = NULL;
    int stmt_count = 0;

    while (parser_peek(&p).type != TOK_EOF) {
        if (parser_peek(&p).type == TOK_NEWLINE) {
            parser_advance(&p);
            continue;
        }

        Node *stmt = NULL;
        Token t = parser_peek(&p);

        if (t.type == TOK_KEYWORD && t.keyword == KW_LET) {
            parser_advance(&p);
            Token nameTok = parser_expect(&p, TOK_IDENT, "expected variable name");
            parser_expect(&p, TOK_SYMBOL, "expected '=' after identifier");
            Node *value = parse_expression(&p);
            parser_expect(&p, TOK_SYMBOL, "expected ';' after declaration");
            stmt = new_declare(nameTok.text, value, nameTok.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_PRINT) {
            parser_advance(&p);
            parser_expect(&p, TOK_SYMBOL, "expected '(' after print");
            Node *arg = parse_expression(&p);
            parser_expect(&p, TOK_SYMBOL, "expected ')' after print arg");
            stmt = new_print(arg, t.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_FN) {
            parser_advance(&p);
            Token fn_name = parser_expect(&p, TOK_IDENT, "expected function name");
            parser_expect(&p, TOK_SYMBOL, "expected '(' after function name");
            char **params = NULL;
            int param_count = 0;
            if (!(parser_peek(&p).type == TOK_SYMBOL && strcmp(parser_peek(&p).text, ")") == 0)) {
                while (true) {
                    Token param = parser_expect(&p, TOK_IDENT, "expected parameter name");
                    params = realloc(params, (param_count + 1) * sizeof(char *));
                    params[param_count++] = dup_string(param.text);
                    if (parser_peek(&p).type == TOK_SYMBOL && strcmp(parser_peek(&p).text, ",") == 0) {
                        parser_advance(&p);
                        continue;
                    }
                    break;
                }
            }
            parser_expect(&p, TOK_SYMBOL, "expected ')' after parameters");
            Node *body = parse_block(&p);
            stmt = new_function(fn_name.text, params, param_count, body, fn_name.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_IF) {
            parser_advance(&p);
            Node *cond = parse_expression(&p);
            Node *then_branch = parse_block(&p);
            Node *else_branch = NULL;
            if (parser_peek(&p).type == TOK_KEYWORD && parser_peek(&p).keyword == KW_ELSE) {
                parser_advance(&p);
                else_branch = parse_block(&p);
            }
            stmt = new_if(cond, then_branch, else_branch, t.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_WHILE) {
            parser_advance(&p);
            Node *cond = parse_expression(&p);
            Node *body = parse_block(&p);
            stmt = new_while(cond, body, t.line);
        } else if (t.type == TOK_KEYWORD && t.keyword == KW_RETURN) {
            parser_advance(&p);
            Node *value = NULL;
            if (!(parser_peek(&p).type == TOK_SYMBOL && strcmp(parser_peek(&p).text, ";") == 0)) {
                value = parse_expression(&p);
            }
            parser_expect(&p, TOK_SYMBOL, "expected ';' after return");
            stmt = new_return(value, t.line);
        } else {
            Node *expr = parse_expression(&p);
            if (parser_peek(&p).type == TOK_SYMBOL && strcmp(parser_peek(&p).text, "=") == 0) {
                parser_advance(&p);
                Node *value = parse_expression(&p);
                stmt = new_assign(expr->name, value, expr->line);
                free_node(expr);
            } else {
                stmt = expr;
            }
            if (parser_peek(&p).type == TOK_SYMBOL && strcmp(parser_peek(&p).text, ";") == 0) {
                parser_advance(&p);
            }
        }

        stmts = realloc(stmts, (stmt_count + 1) * sizeof(Node *));
        stmts[stmt_count++] = stmt;
    }

    return new_program(stmts, stmt_count, 1);
}
