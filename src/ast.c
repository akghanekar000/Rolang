#include "rolang.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_keyword_text(const char *text, Keyword *keyword) {
    struct {
        const char *name;
        Keyword kw;
    } keywords[] = {
        {"let", KW_LET},
        {"fn", KW_FN},
        {"if", KW_IF},
        {"else", KW_ELSE},
        {"while", KW_WHILE},
        {"return", KW_RETURN},
        {"print", KW_PRINT},
        {"true", KW_TRUE},
        {"false", KW_FALSE},
        {"null", KW_NULL},
        {"and", KW_AND},
        {"or", KW_OR},
        {"not", KW_NOT},
        {NULL, 0}
    };

    for (int i = 0; keywords[i].name; ++i) {
        if (strcmp(text, keywords[i].name) == 0) {
            *keyword = keywords[i].kw;
            return true;
        }
    }
    return false;
}

static void push_token(Token **tokens, int *count, int *capacity, Token token) {
    if (*count >= *capacity) {
        *capacity = (*capacity == 0) ? 16 : *capacity * 2;
        *tokens = realloc(*tokens, *capacity * sizeof(Token));
        if (!*tokens) {
            fprintf(stderr, "memory error\n");
            exit(1);
        }
    }
    (*tokens)[(*count)++] = token;
}

Token *tokenize(const char *source, int *count) {
    Token *tokens = NULL;
    int capacity = 0;
    int line = 1;
    int col = 1;
    const char *p = source;

    while (*p) {
        if (isspace((unsigned char)*p)) {
            if (*p == '\n') {
                Token t = {TOK_NEWLINE, 0, dup_string("\n"), 0.0, line, col};
                push_token(&tokens, count, &capacity, t);
                line++;
                col = 1;
            } else {
                col++;
            }
            p++;
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *start = p;
            while (isalnum((unsigned char)*p) || *p == '_') {
                p++;
            }
            size_t length = (size_t)(p - start);
            char *text = malloc(length + 1);
            memcpy(text, start, length);
            text[length] = '\0';

            Keyword kw = 0;
            TokenType type = TOK_IDENT;
            if (is_keyword_text(text, &kw)) {
                type = TOK_KEYWORD;
            }

            Token t = {
                .type = type,
                .keyword = kw,
                .text = text,
                .number = 0.0,
                .line = line,
                .column = col
            };
            push_token(&tokens, count, &capacity, t);
            col += (int)length;
            continue;
        }

        if (isdigit((unsigned char)*p)) {
            const char *start = p;
            while (isdigit((unsigned char)*p)) {
                p++;
            }
            if (*p == '.') {
                p++;
                while (isdigit((unsigned char)*p)) {
                    p++;
                }
            }
            size_t length = (size_t)(p - start);
            char *text = malloc(length + 1);
            memcpy(text, start, length);
            text[length] = '\0';

            Token t = {TOK_NUMBER, 0, text, strtod(text, NULL), line, col};
            push_token(&tokens, count, &capacity, t);
            col += (int)length;
            continue;
        }

        if (*p == '"' || *p == '\'') {
            char quote = *p;
            const char *start = ++p;
            char buffer[4096];
            size_t idx = 0;
            while (*p && *p != quote) {
                if (*p == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                        case 'n': buffer[idx++] = '\n'; break;
                        case 't': buffer[idx++] = '\t'; break;
                        case 'r': buffer[idx++] = '\r'; break;
                        case '\\': buffer[idx++] = '\\'; break;
                        case '\"': buffer[idx++] = '"'; break;
                        case '\'': buffer[idx++] = '\''; break;
                        default: buffer[idx++] = *p; break;
                    }
                    p++;
                    continue;
                }
                buffer[idx++] = *p++;
            }
            if (*p != quote) {
                fprintf(stderr, "unterminated string at line %d\n", line);
                exit(1);
            }
            buffer[idx] = '\0';
            p++;
            Token t = {TOK_STRING, 0, dup_string(buffer), 0.0, line, col};
            push_token(&tokens, count, &capacity, t);
            col += (int)(p - start) + 2;
            continue;
        }

        const char *two = p;
        if (*p == '=' && p[1] == '=') {
            Token t = {TOK_SYMBOL, 0, dup_string("=="), 0.0, line, col};
            push_token(&tokens, count, &capacity, t);
            p += 2; col += 2; continue;
        }
        if (*p == '!' && p[1] == '=') {
            Token t = {TOK_SYMBOL, 0, dup_string("!="), 0.0, line, col};
            push_token(&tokens, count, &capacity, t);
            p += 2; col += 2; continue;
        }
        if (*p == '<' && p[1] == '=') {
            Token t = {TOK_SYMBOL, 0, dup_string("<="), 0.0, line, col};
            push_token(&tokens, count, &capacity, t);
            p += 2; col += 2; continue;
        }
        if (*p == '>' && p[1] == '=') {
            Token t = {TOK_SYMBOL, 0, dup_string(">="), 0.0, line, col};
            push_token(&tokens, count, &capacity, t);
            p += 2; col += 2; continue;
        }
        if (*p == '&' && p[1] == '&') {
            Token t = {TOK_SYMBOL, 0, dup_string("&&"), 0.0, line, col};
            push_token(&tokens, count, &capacity, t);
            p += 2; col += 2; continue;
        }
        if (*p == '|' && p[1] == '|') {
            Token t = {TOK_SYMBOL, 0, dup_string("||"), 0.0, line, col};
            push_token(&tokens, count, &capacity, t);
            p += 2; col += 2; continue;
        }

        if (strchr("{}();=+\-*/%<>!", *p)) {
            char *sym = malloc(2);
            sym[0] = *p;
            sym[1] = '\0';
            Token t = {TOK_SYMBOL, 0, sym, 0.0, line, col};
            push_token(&tokens, count, &capacity, t);
            p++; col++; continue;
        }

        fprintf(stderr, "unexpected character '%c' at line %d:%d\n", *p, line, col);
        exit(1);
    }

    Token eof_token = {TOK_EOF, 0, dup_string("<eof>"), 0.0, line, col};
    push_token(&tokens, count, &capacity, eof_token);
    return tokens;
}

void free_tokens(Token *tokens, int count) {
    if (!tokens) return;
    for (int i = 0; i < count; ++i) {
        free(tokens[i].text);
    }
    free(tokens);
}
