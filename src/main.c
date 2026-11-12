#include "rolang.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    double number;
    char *string;
    bool is_number;
    bool is_string;
    bool is_function;
    Node *function_node;
} Value;

typedef struct {
    char *name;
    Value value;
} Variable;

typedef struct {
    Variable *items;
    int count;
    int capacity;
} Environment;

static void env_set(Environment *env, const char *name, Value value) {
    for (int i = 0; i < env->count; ++i) {
        if (strcmp(env->items[i].name, name) == 0) {
            env->items[i].value = value;
            return;
        }
    }
    if (env->count >= env->capacity) {
        env->capacity = env->capacity == 0 ? 8 : env->capacity * 2;
        env->items = realloc(env->items, env->capacity * sizeof(Variable));
    }
    env->items[env->count].name = dup_string(name);
    env->items[env->count].value = value;
    env->count++;
}

static Value *env_get(Environment *env, const char *name) {
    for (int i = 0; i < env->count; ++i) {
        if (strcmp(env->items[i].name, name) == 0) {
            return &env->items[i].value;
        }
    }
    return NULL;
}

static Value eval_expression(Node *node, Environment *env);

static Value eval_binary(Node *node, Environment *env) {
    Value left = eval_expression(node->left, env);
    Value right = eval_expression(node->right, env);
    Value result = {0};
    result.is_number = true;
    result.number = 0.0;

    switch (node->op) {
        case '+': result.number = left.number + right.number; break;
        case '-': result.number = left.number - right.number; break;
        case '*': result.number = left.number * right.number; break;
        case '/': result.number = left.number / right.number; break;
        case '%': result.number = fmod(left.number, right.number); break;
        case '<': result.number = left.number < right.number ? 1.0 : 0.0; break;
        case '>': result.number = left.number > right.number ? 1.0 : 0.0; break;
        case '=': result.number = left.number == right.number ? 1.0 : 0.0; break;
        default: fprintf(stderr, "unsupported operator %c\n", node->op); exit(1);
    }
    return result;
}

static Value eval_expression(Node *node, Environment *env) {
    if (!node) {
        Value v = {0};
        v.is_number = true;
        v.number = 0.0;
        return v;
    }

    switch (node->type) {
        case NODE_LITERAL:
            if (node->string) {
                Value v = {0};
                v.is_string = true;
                v.string = dup_string(node->string);
                return v;
            }
            {
                Value v = {0};
                v.is_number = true;
                v.number = node->number;
                return v;
            }
        case NODE_IDENT: {
            Value *v = env_get(env, node->name);
            if (!v) {
                fprintf(stderr, "undefined variable '%s'\n", node->name);
                exit(1);
            }
            return *v;
        }
        case NODE_BINARY:
            return eval_binary(node, env);
        case NODE_CALL: {
            Value callee = eval_expression(node->callee, env);
            if (!callee.is_function) {
                fprintf(stderr, "call target is not a function\n");
                exit(1);
            }
            Environment local = {0};
            Node *func = callee.function_node;
            if (func->param_count != node->argc) {
                fprintf(stderr, "wrong argument count\n");
                exit(1);
            }
            for (int i = 0; i < node->argc; ++i) {
                Value arg = eval_expression(node->args[i], env);
                env_set(&local, func->params[i], arg);
            }
            Value result = {0};
            for (int i = 0; i < func->body->stmt_count; ++i) {
                Node *stmt = func->body->stmts[i];
                if (stmt->type == NODE_RETURN) {
                    result = eval_expression(stmt->left, &local);
                    break;
                }
                if (stmt->type == NODE_PRINT) {
                    Value v = eval_expression(stmt->left, &local);
                    printf("%s\n", v.is_string ? v.string : "");
                }
            }
            return result;
        }
        case NODE_UNARY:
            {
                Value operand = eval_expression(node->left, env);
                Value result = {0};
                result.is_number = true;
                result.number = (node->op == '-') ? -operand.number : operand.number;
                return result;
            }
        default:
            fprintf(stderr, "unsupported expression node type %d\n", node->type);
            exit(1);
    }
}

static void exec_stmt(Node *node, Environment *env) {
    if (!node) return;

    switch (node->type) {
        case NODE_PROGRAM:
        case NODE_BLOCK:
            for (int i = 0; i < node->stmt_count; ++i) {
                exec_stmt(node->stmts[i], env);
            }
            break;
        case NODE_DECLARE: {
            Value v = eval_expression(node->right, env);
            env_set(env, node->name, v);
            break;
        }
        case NODE_ASSIGN: {
            Value v = eval_expression(node->right, env);
            Value *existing = env_get(env, node->name);
            if (!existing) {
                fprintf(stderr, "undefined variable '%s'\n", node->name);
                exit(1);
            }
            *existing = v;
            break;
        }
        case NODE_PRINT: {
            Value v = eval_expression(node->left, env);
            if (v.is_string) {
                printf("%s\n", v.string);
            } else {
                printf("%g\n", v.number);
            }
            break;
        }
        case NODE_FUNCTION: {
            Value v = {0};
            v.is_function = true;
            v.function_node = node;
            env_set(env, node->name, v);
            break;
        }
        case NODE_IF: {
            Value cond = eval_expression(node->left, env);
            if (cond.number != 0.0) {
                exec_stmt(node->body, env);
            } else if (node->else_body) {
                exec_stmt(node->else_body, env);
            }
            break;
        }
        case NODE_WHILE: {
            while (eval_expression(node->left, env).number != 0.0) {
                exec_stmt(node->body, env);
            }
            break;
        }
        case NODE_RETURN: {
            if (node->left) {
                Value v = eval_expression(node->left, env);
                env_set(env, "__return__", v);
            }
            break;
        }
        default:
            fprintf(stderr, "unsupported statement type %d\n", node->type);
            exit(1);
    }
}

void run_program(Node *program) {
    Environment env = {0};
    exec_stmt(program, &env);
}
