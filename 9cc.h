#include "vector.h"
#include "map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// MINマクロ
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// 型
typedef struct Type {
    enum TypeKind { INT, PTR, ARRAY } type; // 型の種別
    struct Type *ptr_to;    // typeがPTRの時だけ有効
    int num_elements;       // 配列の要素数
} Type;

static inline const char* type_description(Type *type) {
    static const char* description[] = {
        "INT", "PTR", "ARRAY"
    };
    static char buffer[1024];

    if (!type) {
        return "null.";
    }

    sprintf(buffer, "%-5s %-14p %d",
            description[type->type],
            type->ptr_to,
            type->num_elements);
    return buffer;
}

static inline bool type_equal(Type *lhs, Type *rhs) {
    if (lhs == rhs) return true;
    if (!(lhs && rhs)) return false;
    return lhs->type == rhs->type &&
           lhs->num_elements == rhs->num_elements &&
           type_equal(lhs->ptr_to, rhs->ptr_to);
}

/**
 * ポインタの数を返す
 *
 * \param type_info 型情報
 * \return ポインタの数。ダブルポインタなら2。
 */
static inline int type_number_of_pointers(const Type *type_info) {
    int n = 0;
    if (type_info && type_info->type == PTR) {
        for (Type *t = type_info->ptr_to; t; t = t->ptr_to) {
            n++;
        }
    }
    return n;
}

/**
 * 型のバイトサイズを得る
 *
 * \param type_info 型情報
 * \return バイトサイズ
 */
static inline int type_byte_size(const Type *type_info) {
    switch (type_info->type) {
    case INT:
        return 4;
    case PTR:
        return 8;
    case ARRAY:
        return 4 * type_info->num_elements; // INTしかないので4
    }
    assert(false); // ここには来ないはず
    return 0;
}

/**
 * 型がポインタまたは配列かどうか
 *
 * \param type_info 型情報
 * \return ポインタまたは配列なら真
 */
static inline bool type_is_pointer_or_array(const Type* type_info) {
    assert(type_info);
    return type_info->type == PTR || type_info->type == ARRAY;
}

static inline Type* new_type(enum TypeKind tk) {
    Type* type = calloc(1, sizeof(Type));
    type->type = tk;
    return type;
}

static inline Type* new_ptr_type(Type* original_type) {
    Type* type = calloc(1, sizeof(Type));
    type->type = PTR;
    type->ptr_to = original_type;
    return type;
}

static inline Type* new_array_type(int n, Type* original_type) {
    Type* type = calloc(1, sizeof(Type));
    type->type = ARRAY;
    type->ptr_to = original_type;
    type->num_elements = n;
    return type;
}

// 抽象構文木のノードの種類
typedef enum {
    ND_ADD, // +
    ND_SUB, // -
    ND_MUL, // *
    ND_DIV, // /
    ND_GREATER, // >
    ND_GREATER_EQUAL, // >=
    ND_EQUAL, // ==
    ND_NOT_EQUAL, // !=
    ND_ASSIGN,  // =
    ND_RETURN, // return
    ND_IF, // if
    ND_ELSE, // else
    ND_WHILE, // while
    ND_FOR, // for
    ND_LVAR,    // ローカル変数
    ND_GLOBAL_DEF,  // グローバル変数宣言
    ND_GLOBAL_REF,  // グローバル変数参照
    ND_BLOCK,    // ブロック
    ND_FUN, // 関数
    ND_FUN_IMPL, // 関数定義
    ND_NUM, // 整数
    ND_ADDR, // アドレス取得演算子
    ND_DEREF, // アドレス参照演算子
} NodeKind;

static inline const char* node_kind_descripion(NodeKind kind) {
    static const char* description[] = {
        "ADD", // +
        "SUB", // -
        "MUL", // *
        "DIV", // /
        "GREATER", // >
        "GREATER_EQUAL", // >=
        "EQUAL", // ==
        "NOT_EQUAL", // !=
        "ASSIGN",  // =
        "RETURN", // return
        "IF", // if
        "ELSE", // else
        "WHILE", // while
        "FOR", // for
        "LVAR",    // ローカル変数
        "GLOBAL_DEF",   // グローバル変数
        "GLOBAL_REF",   // グローバル変数
        "BLOCK",    // ブロック
        "FUN", // 関数
        "FUN_IMPL", // 関数定義
        "NUM", // 整数
        "ADDR", // アドレス取得演算子
        "DEREF", // アドレス参照演算子
    };
    return description[kind];
}

typedef struct Node {
    NodeKind kind;      // 種別
    struct Node *lhs;   // 左辺
    struct Node *rhs;   // 右辺
    struct Node *condition; // 条件(ifの場合のみ)
    Vector *block;      // ブロック
    int val;            // kindがND_NUMの場合はその値、kindがND_FUNの場合、関数呼び出し確定済かどうかを示すフラグ値
    char *ident;        // kindがND_FUNの場合のみ使う(関数名)
    int identLength;    // 上記の長さ   
    int offset;         // kindがND_LVARの場合のみ使う
    Type *type;         // 型情報
} Node;

static inline const char* node_name_copy(const Node* node, char* buffer, int size) {
    buffer[0] = '\0';
    const int n = MIN(size - 1, node->identLength);
    memcpy(buffer, node->ident, n);
    buffer[n] = '\0';
    return buffer;
}

static inline const char* node_name(const Node* node) {
    static char buffer[1024];

    node_name_copy(node, buffer, sizeof(buffer));
    return buffer;
}

static inline const char* node_description(Node *node) {
    static char buffer[1024];
    static char tmp[1024];

    if (!node) {
        return "(nil)";
    }

    tmp[0] = '\0';
    if (node->kind == ND_FUN || node->kind == ND_FUN_IMPL || node->kind == ND_LVAR) {
        node_name_copy(node, tmp, sizeof(tmp));
    } else if (node->kind == ND_NUM) {
        const int n = sprintf(tmp, "num:%d", node->val);
        tmp[n] = '\0';
    }

    sprintf(buffer, "%-12s '%-6s' {%-s} %3d %14p/%14p/%14p",
            node_kind_descripion(node->kind),
            tmp,
            type_description(node->type),
            node->offset,
            node->lhs,
            node->rhs,
            node->condition);
    return buffer;
}

static inline int node_num_pointers(Node *node) {
    if (node->kind == ND_LVAR) {
        return type_number_of_pointers(node->type);
    }
    return 0;
}

// ポインタとして扱うかどうか
static inline bool node_is_treat_pointer(Node *node) {
    if (node->kind == ND_LVAR) {
        return type_is_pointer_or_array(node->type);
    }
    return false;
}

static inline bool node_hands_is_treat_pointer(Node *node) {
    return node_is_treat_pointer(node->lhs) || node_is_treat_pointer(node->rhs);
}

static inline bool node_is_pointer_variable_many(Node *node) {
    return node_num_pointers(node) > 1;
}

static inline bool node_hands_is_pointer_variable_many(Node *node) {
    return node_is_pointer_variable_many(node->lhs) || node_is_pointer_variable_many(node->rhs);
}

// トークンの種類
typedef enum {
    TK_RESERVED,    // 記号
    TK_IDENT,       // 識別子
    TK_RETURN,      // return文
    TK_IF,          // if文
    TK_ELSE,        // else文
    TK_WHILE,       // while文
    TK_FOR,         // for文
    TK_NUM,         // 整数トークン
    TK_INT,         // "int"と言う名前の型
    TK_SIZEOF,      // sizeof
    TK_EOF,         // 入力の終わりを表すトークン
} TokenKind;

static inline const char *token_kind_description(TokenKind kind) {
    switch (kind) {
    case TK_RESERVED:    // 記号
        return "RESERVED";
    case TK_IDENT:       // 識別子
        return "IDENT";
    case TK_RETURN:      // return文
        return "RETURN";
    case TK_IF:          // if文
        return "IF";
    case TK_ELSE:        // else文
        return "ELSE";
    case TK_WHILE:       // while文
        return "WHILE";
    case TK_FOR:         // for文
        return "FOR";
    case TK_NUM:         // 整数トークン
        return "NUM";
    case TK_INT:         // "int"と言う名前の型
        return "INT";
    case TK_SIZEOF:
        return "SIZEOF";
    case TK_EOF:         // 入力の終わりを表すトークン
        return "EOF";
    }
    return "*Unrecoginzed*";
}

// トークンの型
typedef struct Token {
    TokenKind kind;     // トークン種別
    struct Token *next; // 次のトークン
    int val;            // tyがTK_NUMの場合、その数値
    char *str;          // トークン文字列
    int len;            // トークン文字列の長さ
    char *input;        // トークン文字列（エラーメッセージ用）
} Token;

static inline const char *token_description(Token *token) {
    static char buffer[1024];
    static char tmp[1024];

    if (!token) {
        return "null";
    }

    const int n = MIN(sizeof(tmp) - 1, token->len);
    memcpy(tmp, token->str, n);
    tmp[n] = '\0';
    
    sprintf(buffer, "Token: %s, `%s` %p",
            token_kind_description(token->kind),
            tmp,
            token->next);
    return buffer;
}

static inline char *token_name_copy(Token *token) {
    char *name = NULL;
    if (token) {
        // 変数名を確保する
        name = calloc(1, token->len + 1);
        memcpy(name, token->str, token->len);
    }
    return name;
}

typedef enum {
    GEN_PUSHED_RESULT,
    GEN_DONT_PUSHED_RESULT,
} GenResult;

extern Token *token;

extern Token *tokenize(char *p);
extern void error_exit(char *fmt, ...);
extern void program();
extern void gen_global_varibale(const Node *node);
extern GenResult gen(Node *node);
extern Node *code[];

#define D(fmt, ...) \
    fprintf(stderr, ("🐝 %s[%s#%d] " fmt "\n"), __PRETTY_FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__)

#define D_INT(v) \
    do { D(#v "=%d", (v)); fflush(stderr); } while (0)

#define D_NODE(n) \
    do { D(#n "=%s", node_description(n)); fflush(stderr); } while (0)

#define D_TOKEN(t) \
    do { D(#t "=%s", token_description(t)); fflush(stderr); } while (0)

#define D_TYPE(t) \
    do { D(#t "=%s", type_description(t)); fflush(stderr); } while (0)
