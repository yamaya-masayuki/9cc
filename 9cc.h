// 抽象構文木のノードの種類
typedef enum {
    ND_ADD, // +
    ND_SUB, // -
    ND_MUL, // *
    ND_DIV, // /
    ND_NUM, // 整数
} NodeKind;

typedef struct Node {
    NodeKind kind;    // 演算子かND_NUM
    struct Node *lhs; // 左辺
    struct Node *rhs; // 右辺
    int val;          // kindがND_NUMの場合のみ使う
} Node;

// トークンの種類
typedef enum {
    TK_RESERVED, // 記号
    TK_NUM,      // 整数トークン
    TK_EOF,      // 入力の終わりを表すトークン
} TokenKind;

// トークンの型
typedef struct Token {
    TokenKind kind;     // トークン種別
    struct Token *next; // 次のトークン
    int val;            // tyがTK_NUMの場合、その数値
    char *str;          // トークン文字列
    char *input;        // トークン文字列（エラーメッセージ用）
} Token;

extern Token *token;

extern Token *tokenize(char *p);
extern void error_exit(char *fmt, ...);
extern Node *expr();
extern void gen(Node *node);
