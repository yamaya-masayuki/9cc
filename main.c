#include "9cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

int main(int argc, char **argv) {
    //D("~~~ENTER~~~");

    if (argc != 2) {
        error_exit("引数の個数が正しくありません");
        return 1;
    }

    // トークナイズしてパースする
    token = tokenize(argv[1]);
    program();

    // アセンブリの前半部分を出力
    printf(".intel_syntax noprefix\n");
    printf(".data\n");
    for (int i = 0; code[i]; i++) {
        if (code[i]->kind == ND_GLOBAL_DEF) {
            gen_global_varibale(code[i]);
        }
    }
    printf("\n");

    // 先頭の式から順にコード生成
    printf(".text\n");
    printf(".global _main\n");
    for (int i = 0; code[i]; i++) {
        D("%s", node_description(code[i]));
        GenResult result = gen(code[i]);
        // 式の評価結果としてスタックに一つの値が残っているはずなので、スタック
        // が溢れないようにポップしておく
        if (result == GEN_PUSHED_RESULT) {
            printf("  pop rax\n");
        }
    }
    //D("~~~EXIT~~~");

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
