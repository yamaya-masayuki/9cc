#include "9cc.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

// ローカル変数の型
typedef struct LVar LVar;
struct LVar {
    LVar *next; // 次の変数かNULL
    char *name; // 変数の名前
    int len;    // 名前の長さ
    int offset; // RBPからのオフセット
};

LVar *locals;   // ローカル変数のリストの先頭

// 変数を名前で検索する。見つからなかった場合はNULLを返す。
LVar *find_lvar(Token *token) {
    for (LVar *var = locals; var; var = var->next)
        if (var->len == token->len && memcmp(token->str, var->name, var->len) == 0)
            return var;
    return NULL;
}

// 現在着目しているトークン
Token *token;

Node *new_node(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

Node *new_node_num(int val) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_NUM;
    node->val = val;
    return node;
}

bool consume_and_next(char* op, bool must_to_next) {
    if (token->kind == TK_RESERVED &&
        strlen(op) == token->len &&
        memcmp(token->str, op, token->len) == 0) {
        if (must_to_next) {
            token = token->next;
        }
        return true;
    }
    return false;
}


bool consume(char* op) {
    return consume_and_next(op, true);
}

Token* consume_by_kind(TokenKind kind) {
    if (token->kind == kind) {
        Token* t = token;
        token = token->next;
        return t;
    }
    return NULL;
}

Token* consume_ident() {
    return consume_by_kind(TK_IDENT);
}

// 次のトークンが期待している記号のときには、トークンを1つ読み進める。
// それ以外の場合にはエラーを報告する。
void expect(char op) {
    if (token->kind != TK_RESERVED || token->str[0] != op)
        error_exit("'%c'ではありません", op);
    token = token->next;
}

// 次のトークンが数値の場合、トークンを1つ読み進めてその数値を返す。
// それ以外の場合にはエラーを報告する。
int expect_number() {
    if (token->kind != TK_NUM)
        error_exit("数ではありません");
    int val = token->val;
    token = token->next;
    return val;
}

bool at_eof() {
    if (token == NULL) {
        return false;
    }
    return token->kind == TK_EOF;
}

// 新しいトークンを作成してcurに繋げる
Token *new_token(TokenKind kind, Token *cur, char *str, int len) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->str = str;
    tok->len = len;
    cur->next = tok;
    return tok;
}

// 前方宣言
Node *mul();

Node *add() {
    Node *node = mul();

    for (;;) {
        if (consume("+"))
            node = new_node(ND_ADD, node, mul());
        else if (consume("-"))
            node = new_node(ND_SUB, node, mul());
        else
            return node;
    }
}

Node *relational() {
    Node * node = add();

    for (;;) {
        if (consume("<"))
            node = new_node(ND_GREATER, node, add());
        else if (consume("<="))
            node = new_node(ND_GREATER_EQUAL, node, add());
        else if (consume(">")) {
            node = new_node(ND_GREATER, node, add());
            Node *tmp = node->lhs;
            node->lhs = node->rhs;
            node->rhs = tmp;
        }
        else if (consume(">=")) {
            node = new_node(ND_GREATER_EQUAL, node, add());
            Node *tmp = node->lhs;
            node->lhs = node->rhs;
            node->rhs = tmp;
        }
        else
            return node;
    }
}

Node *equality() {
    Node * node = relational();

    for (;;) {
        if (consume("=="))
            node = new_node(ND_EQUAL, node, relational());
        else if (consume("!="))
            node = new_node(ND_NOT_EQUAL, node, relational());
        else
            return node;
    }
}

Node *assign() {
    Node * node = equality();
    if (consume("="))
        node = new_node(ND_ASSIGN, node, equality());
    return node;
}

Node *expr() {
    return assign();
}

Token* peek(TokenKind kind) {
    for (Token* t = token; t != NULL && t->kind != TK_EOF; t = t->next) {
        if (t->kind == kind) {
            return t;
        }
    }
    return NULL;
}

Token* equal(Token* t, TokenKind kind, const char *str) {
    if (t->kind == kind && memcmp(t->str, str, strlen(str)) == 0) {
        return t;
    }
    return NULL;
}

Node *stmt() {
    Node *node;
    if (consume_by_kind(TK_IF)) { // if
        node = new_node(ND_IF, NULL, NULL);
        node->condition = expr();
        node->lhs = stmt();
        Token* maybe_else = peek(TK_ELSE);
        if (maybe_else != NULL) { // else
            token = maybe_else->next; // elseトークンをスキップ: consume関数がやってること
            node->rhs = stmt();
        }
        return node;
    } else if (consume_by_kind(TK_WHILE)) { // while
        node = new_node(ND_WHILE, NULL, NULL);
        node->condition = expr();
        node->lhs = stmt();
        return node;
    } else if (consume_by_kind(TK_FOR)) { // for
        if (consume("(")) {
            Vector *v = new_vec();
            Node * e = NULL;

            e = expr();
            vec_push(v, e);
            if (!consume(";")) {
                error_exit("';'ではないトークンです: %d %s", token->kind, token->str);
            }
            e = expr();
            vec_push(v, e);
            if (!consume(";")) {
                error_exit("';'ではないトークンです: %d %s", token->kind, token->str);
            }
            e = expr();
            vec_push(v, e);
            if (!consume(")")) {
                error_exit("')'ではないトークンです: %d %s", token->kind, token->str);
            }

            node = new_node(ND_FOR, NULL, NULL);
            node->block = v;
            node->lhs = stmt();
        }
        return node;
    } else if (consume("{")) { // ブロック
        Vector *vec = new_vec();
        do {
            vec_push(vec, stmt());
        } while (!consume("}"));
        node = new_node(ND_BLOCK, NULL, NULL);
        node->block = vec;
        return node; // ここでreturnするので文末の';'は不要
    } else if (consume_by_kind(TK_RETURN)) {
        node = new_node(ND_RETURN, expr(), NULL);
    } else {
        node = expr();
        // ノードが関数ノードで次がブロックの場合は関数定義ノード
        if (node->kind == ND_FUN && consume_and_next("{", false)) {
            if (node->val == 1)
                error_exit("関数呼び出しと関数定義の記述が曖昧です: %d %s", token->kind, token->str);
            node->kind = ND_FUN_IMPL;
            node->lhs = stmt();
            return node;
        }
    }

    if (!consume(";")) {
        error_exit("';'ではないトークンです: %d %s", token->kind, token->str);
    }

    return node;
}

// 文を格納する配列
Node *code[100];
static int statement_index = 0;

void program() {
    statement_index = 0;
    while (!at_eof()) {
        Node *node = stmt();
        code[statement_index] = node;
#if 0
        // ノードがブロックの場合、直前のノードを調べて、
        // 関数呼び出しノードであれば、関数定義ノードに書き換える
        if (node->kind == ND_BLOCK && statement_index != 0) {
            Node *maybeFun = code[statement_index - 1];
            if (maybeFun->kind == ND_FUN && maybeFun->val == 0) {
                maybeFun->kind = ND_FUN_IMPL;
            }
        }
#endif
        statement_index++;
    }
    code[statement_index] = NULL;
}

// 前方宣言
Node *unary();
Node *pointer();

Node *mul() {
    Node *node = unary();

    for (;;) {
        if (consume("*"))
            node = new_node(ND_MUL, node, unary());
        else if (consume("/"))
            node = new_node(ND_DIV, node, unary());
        else
            return node;
    }
}

Node *local_var(Token* t) {
    Node * node = new_node(ND_LVAR, NULL, NULL);
    LVar *lvar = find_lvar(t);
    if (lvar == NULL) {
        // LVarをnewする
        lvar = calloc(1, sizeof(LVar));
        lvar->next = locals;
        lvar->name = t->str;
        lvar->len = t->len;
        lvar->offset = (locals == NULL ? 0 : locals->offset) + 8;
        locals = lvar;
    }
    node->offset = lvar->offset;
    return node;
}

Node *term() {
    // 次のトークンが'('なら、"(" expr ")"のはず
    if (consume("(")) {
        Node *node = expr();
        expect(')');
        return node;
    }

    // 識別子
    Token* t = consume_ident();
    if (t != NULL) {
        // `()`を先読みしてあれば関数ノードを作成する
        Token* openParen = equal(t->next, TK_RESERVED, "(");
        if (openParen != NULL) {
            Token *closeParen = NULL;
            Vector *args = new_vec();
            for (Token* at = openParen->next; at != NULL; at = at->next) {
                if (at->kind == TK_IDENT) {
                    vec_push(args, local_var(at));
                } else if (at->kind == TK_NUM) {
                    vec_push(args, new_node_num(at->val));
                    at->val = 1; // 関数呼び出し確定とする
                } else {
                    closeParen = equal(at, TK_RESERVED, ")");
                    if (closeParen != NULL) {
                        break;
                    } else if (equal(at, TK_RESERVED, ",") == NULL) {
                        error_exit("関数呼び出しシンタックスエラー: %s\n", at->str);
                    }
                }
            }
            token = closeParen->next;
            Node *node = new_node(ND_FUN, NULL, NULL);
            node->ident = t->str;
            node->identLength = t->len;
            node->block = args;
            return node;
        }
        // ローカル変数
        return local_var(t);
    }

    Node *node = pointer();
    if (node) {
        return node;
    }

    // そうでなければ数値のはず
    return new_node_num(expect_number());
}

Node *unary() {
    if (consume("+"))
        return term();
    if (consume("-"))
        return new_node(ND_SUB, new_node_num(0), term());
    return term();
}

Node *pointer() {
    if (consume("&"))
        // オペランドについてruiさんの文書ではlhsだが他の演算子との整合性を考慮
        // してrhsにする
        return new_node(ND_ADDR, NULL, unary());
    else if (consume("*"))
        return new_node(ND_DEREF, NULL, unary());
    return NULL;
}

int is_alnum(char c) {
    return ('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        (c == '_');
}

// 入力文字列pをトークナイズしてそれを返す
Token* tokenize(char *p) {
    Token head;
    head.next = NULL;
    Token *cur = &head;

    while (*p) {
        // 空白文字をスキップ
        if (isspace(*p)) {
            p++;
            continue;
        }

        // return文
        if (strncmp(p, "return", 6) == 0 && !is_alnum(p[6])) {
            cur = new_token(TK_RETURN, cur, p, 6);
            p += 6;
            continue;
        }

        // if文
        if (strncmp(p, "if", 2) == 0 && !is_alnum(p[2])) {
            cur = new_token(TK_IF, cur, p, 2);
            p += 2;
            continue;
        }

        // else文
        if (strncmp(p, "else", 4) == 0 && !is_alnum(p[4])) {
            cur = new_token(TK_ELSE, cur, p, 4);
            p += 4;
            continue;
        }

        // while文
        if (strncmp(p, "while", 5) == 0 && !is_alnum(p[5])) {
            cur = new_token(TK_WHILE, cur, p, 5);
            p += 5;
            continue;
        }

        // for文
        if (strncmp(p, "for", 3) == 0 && !is_alnum(p[3])) {
            cur = new_token(TK_FOR, cur, p, 3);
            p += 3;
            continue;
        }

        // 関係演算子
        char *q = p + 1;
        if (*q) {
            if (*p == '>' || *p == '<') {
                if (*q == '=') {
                    cur = new_token(TK_RESERVED, cur, p++, 2);
                } else {
                    cur = new_token(TK_RESERVED, cur, p, 1);
                }
                p++;
                continue;
            }
            if (strncmp(p, "==", 2) == 0 || strncmp(p, "!=", 2) == 0) {
                cur = new_token(TK_RESERVED, cur, p, 2);
                p += 2;
                continue;
            }
        }

        if (*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '(' || *p == ')' || *p == '=' || *p == ';' || *p == '{' || *p == '}' || *p == ',' || *p == '&') {
            cur = new_token(TK_RESERVED, cur, p++, 1);
            continue;
        }

        // 数値
        if (isdigit(*p)) {
            cur = new_token(TK_NUM, cur, p, 1);
            cur->val = strtol(p, &p, 10);
            continue;
        }

        // ローカル変数
        char *s = p;
        while ('a' <= *s && *s <= 'z') {
            s++;
        }
        if (s != p) {
            const int length = s - p;
            cur = new_token(TK_IDENT, cur, p, length);
            cur->len = length;
            p = s;
            continue;
        }

        error_exit("トークナイズできません");
    }

    new_token(TK_EOF, cur, p, 1);
    return head.next;
}
