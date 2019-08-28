#include "9cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        error_exit("引数の個数が正しくありません");
        return 1;
    }

    // トークナイズしてパースする
    token = tokenize(argv[1]);
    program();

    // アセンブリの前半部分を出力
    printf(".intel_syntax noprefix\n");
    printf(".global _main\n");
    printf("_main:\n");

    // プロローグ
    // 変数26個分の領域を確保する
    printf("  push rbp\n");
    printf("  mov rbp, rsp\n");
    printf("  sub rsp, 208\n");

    // 先頭の式から順にコード生成
    for (int i = 0; code[i]; i++) {
        gen(code[i]);
        fprintf(stderr, "main: end for loop\n");
        // 式の評価結果としてスタックに一つの値が残っているはずなので、スタック
        // が溢れないようにポップしておく
        printf("  pop rax\n");
    }

    // エピローグ
    // 最後の式の結果がRAXに残っているのでそれが返り値になる
    printf("  mov rsp, rbp\n");
    printf("  pop rbp\n");
    printf("  ret\n");

    return 0;
}

// エラーを報告するための関数
void error_exit(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}
