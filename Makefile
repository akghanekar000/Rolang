#include "rolang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Node *parse_program(Token *tokens, int count);
void run_program(Node *program);

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "could not open file: %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "memory error\n");
        exit(1);
    }
    fread(buf, 1, (size_t)size, fp);
    buf[size] = '\0';
    fclose(fp);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <sourcefile>\n", argv[0]);
        return 1;
    }

    char *source = read_file(argv[1]);
    int token_count = 0;
    Token *tokens = tokenize(source, &token_count);
    Node *program = parse_program(tokens, token_count);
    run_program(program);

    free_node(program);
    free_tokens(tokens, token_count);
    free(source);
    return 0;
}
