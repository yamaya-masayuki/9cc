#include "9cc.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static GenResult gen_impl(Node *);

void gen_push(char *reg) {
    printf("  push %s\n", reg);
}

void gen_pop(char *reg) {
    printf("  pop %s\n", reg);
}

/*
 * 与えられたノードが変数を指しているときに、その変数のアドレスを計算して、それ
 * をスタックにプッシュします。
 * それ以外の場合にはエラーを表示します。これにより`(a+1)=2`のような式が排除さ
 * れることになります。
 */
void gen_lval(Node *node) {
    // 0. デリファレンスの場合、ND_LVARに到達するまでの回数（すなわち`*`）を数
    //    える
    int dereferences = 0;
    while (node->kind == ND_DEREF) {
        dereferences++;
        node = node->rhs;
    }

    if (node->kind != ND_LVAR)
        error_exit("代入の左辺値が変数ではありません(lvalue)。%s", node_descripion(node));

    // 1. RBPからオフセット分減算する
    printf("  mov rax, rbp   # lvalue\n");
    printf("  sub rax, %-4d  # lvalue\n", node->offset);

    // 2. 結果（変数のアドレス）をスタックに積む
    printf("  push rax       # lvalue\n");

    // 3. デリファレンスの場合は変数の実体のアドレスに到達するまでスタックに積
    //    む
    for (int i = 0; i < dereferences; ++i) {
        printf("  pop rax        # lvalue(dereference)\n");
        printf("  mov rax, [rax] # lvalue(dereference)\n");
        printf("  push rax       # lvalue\n");
    }
}

static const char *ArgRegsiters[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

void gen_fun(Node *node) {
    static char buffer[1024];

    for (int i = 0; i < node->block->len; ++i) {
        Node *argNode = (Node *)node->block->data[i];
        if (argNode->kind == ND_NUM) { // 整数定数の場合
            printf("  mov %s, %d\n", ArgRegsiters[i], argNode->val);
        } else if (argNode->kind == ND_LVAR) { // ローカル変数の場合
            gen_lval(argNode);
            gen_pop("rax");
            printf("  mov %s, [rax]\n", ArgRegsiters[i]);
        }
    }

    size_t len = MIN(node->identLength,
                  sizeof(buffer) / sizeof(buffer[0]) - 1);
    memcpy(buffer, node->ident, len);
    buffer[len] = '\0';
    printf("  call _%s\n", buffer); // RIPをスタックに置いてlabelにジャンプ
}

void gen_fun_impl(Node *node) {
    static char name[1024];
    size_t len = MIN(node->identLength,
                  sizeof(name) / sizeof(name[0]) - 1);
    memcpy(name, node->ident, len);
    name[len] = '\0';

    // 関数ラベル
    printf("_%s:\n", name);

    const bool is_main = (strcmp(name, "main") == 0);
    const int slots = node->block->len + 1; // +1はベースポインタ格納分

    // プロローグ
    printf("  push rbp      # prologue\n");
    printf("  mov rbp, rsp  # prologue\n");
    printf("  xor eax, eax  # prologue\n"); // mov eax, 0 と同じ

    const int stack_size = is_main ? 256 : (slots * 8);
    printf("  sub rsp, %-4d # prologue\n", stack_size); // スタックサイズ

    // 仮引数部分
    for (int i = 0; i < node->block->len; ++i) {
        Node *arg = (Node *)node->block->data[i];
        if (arg->kind != ND_LVAR)
            error_exit("代入の左辺値が変数ではありません(args)。%s", node_descripion(arg));
        printf("  mov qword ptr [rbp - %d], %s  # argument %d\n", arg->offset, ArgRegsiters[i], i);
    }

    // ブロック部分: node->lhsにはND_BLOCKが格納されている
    for (int i = 0; i < node->lhs->block->len; ++i) {
        GenResult resutl = gen_impl(node->lhs->block->data[i]);
        // gen_implが結果をスタックに積んだ場合は取り除いてRAXに入れ関数の戻り
        // 値とする
        if (resutl == GEN_PUSHED_RESULT) {
            gen_pop("rax");
        }
    }

    // エピローグ
    printf("  pop rbp\n");
    printf("  ret\n");
}

static int nested = 0;

GenResult gen_impl(Node *node) {
    static int label_sequence_no = 0;
    GenResult result;

    D("%s, nested=%d", node_descripion(node), nested);
    nested++;

    switch (node->kind) {
    case ND_NUM:
        printf("  push %-4d      # constant\n", node->val);
        nested--;
        return GEN_PUSHED_RESULT;
    case ND_LVAR:
        printf("  # Left value {{{\n");
        /*
         * 変数の参照
         * - 左辺の変数のアドレスをスタックに置く ~ gen_lval()
         * - スタックをポップする
         * - その値をアドレスとみなし参照先の値を取得する
         */
        gen_lval(node);
        printf("  pop rax        # lvalue(outside)\n");
        printf("  mov rax, [rax] # lvalue(outside)\n");
        printf("  push rax       # lvalue(outside)\n");
        printf("  # }}} Left value\n");
        nested--;
        return GEN_PUSHED_RESULT;

    case ND_ASSIGN:
        printf("  # Assign {{{\n");
        /*
         * 変数への代入
         * - 左辺の変数のアドレスをスタックに置く ~ gen_lval()
         * - 右辺を評価し結果をスタックに置く ~ gen_impl()
         * - スタックをポップし右辺値をrbxへ
         * - スタックをポップし変数アドレスをraxへ
         * - raxアドレスにrbx値を書く
         * - rbx値をスタックに置く
         */
        gen_lval(node->lhs); // スタックにLHSのアドレスを入れたままにしておく
        result = gen_impl(node->rhs); // スタックに右辺の評価結果が積まれている
        assert(result == GEN_PUSHED_RESULT); // ここでのgen_implは必ずスタックに結果をプッシュしなければならない
        printf("  pop rbx        # assign\n"); // 右辺(の結果)
        printf("  pop rax        # assign\n"); // 左辺(のアドレス)
        printf("  mov [rax], rbx # assign\n"); // 左辺(のアドレス)に右辺の値をいれる
        printf("  push rbx       # assign\n"); // 右辺の値をスタックに積む
        printf("  # }}} Assign\n");
        nested--;
        return GEN_PUSHED_RESULT;

    case ND_RETURN:
        printf("  # return {{{\n");
        /*
         * return文
         * - gen_impl()の結果はスタックに置かれる
         * - スタックポップしてraxに
         * - 関数からリターンする
         */
        result = gen_impl(node->lhs);
        assert(result == GEN_PUSHED_RESULT); // ここでのgen_implは必ずスタックに結果をプッシュしなければならない
        printf("  pop rax      # epilogue\n"); // 戻り値をRAXにいれる
        printf("  mov rsp, rbp # epilogue\n"); // スタックポインタを復帰
        printf("  pop rbp      # epilogue\n"); // ベースポインタを復帰する
        printf("  ret          # epilogue\n"); // スタックをポップしてそのアドレスにジャンプ
        nested--;
        printf("  # }}} return\n");
        return GEN_DONT_PUSHED_RESULT;

    case ND_IF:
        printf("  # If {{{\n");
        gen_impl(node->condition);
        printf("  pop rax    # condition\n");
        printf("  cmp rax, 0 # condition\n");
        if (node->rhs) {
            // elseがある場合
            printf("  je .Lelse%08d\n", label_sequence_no);
            gen_impl(node->lhs);
            printf("  jmp .Lend%08d\n", label_sequence_no);
            printf(".Lelse%08d:\n", label_sequence_no);
            gen_impl(node->rhs);
            printf(".Lend%08d:\n", label_sequence_no);
        } else {
            // elseがない場合
            printf("  je .Lend%08d\n", label_sequence_no);
            gen_impl(node->lhs);
            printf(".Lend%08d:\n", label_sequence_no);
        }
        label_sequence_no++;
        nested--;
        printf("  # }}} If\n");
        return GEN_PUSHED_RESULT;
    case ND_WHILE:
        printf(".Lbegin%08d:\n", label_sequence_no);
        gen_impl(node->condition);
        gen_pop("rax");
        printf("  cmp rax, 0\n");
        printf("  je .Lend%08d\n", label_sequence_no);
        gen_impl(node->lhs);
        printf("  jmp .Lbegin%08d\n", label_sequence_no);
        printf(".Lend%08d:\n", label_sequence_no);
        label_sequence_no++;
        nested--;
        return GEN_PUSHED_RESULT;
    case ND_FOR:
        if (node->block->data[0]) {
            gen_impl(node->block->data[0]);
        }
        printf(".Lbegin%08d:\n", label_sequence_no);
        if (node->block->data[1]) {
            gen_impl(node->block->data[1]);
        }
        gen_pop("rax");
        printf("  cmp rax, 0\n");
        printf("  je .Lend%08d\n", label_sequence_no);
        gen_impl(node->lhs);
        if (node->block->data[2]) {
            gen_impl(node->block->data[2]);
        }
        printf("  jmp .Lbegin%08d\n", label_sequence_no);
        printf(".Lend%08d:\n", label_sequence_no);
        label_sequence_no++;
        nested--;
        return GEN_PUSHED_RESULT;
    case ND_BLOCK:
        for (int i = 0; i < node->block->len; ++i) {
            gen_impl(node->block->data[i]);
        }
        nested--;
        return GEN_PUSHED_RESULT;
    case ND_FUN:
        printf("  # Function Calling {{{\n");
        /*
         * 関数呼び出し
         * - 関数へのジャンプを生成する
         * - 戻り値が格納されているRAXをスタックに積む
         */
        gen_fun(node);
        printf("  push rax # Return value\n");
        nested--;
        printf("  # }}} Function Calling\n");
        return GEN_PUSHED_RESULT;
    case ND_FUN_IMPL:
        printf("  # Function Implementation {{{\n");
        /*
         * 関数実装
         * - 関数の本体コードを生成する
         * - 評価結果はない
         */
        gen_fun_impl(node);
        nested--;
        printf("  # }}} Function Implementation\n");
        return GEN_DONT_PUSHED_RESULT;
    case ND_ADDR:
        gen_lval(node->rhs);
        nested--;
        return GEN_PUSHED_RESULT;
    case ND_DEREF:
        gen_impl(node->rhs);
        gen_pop("rax");
        printf("  mov rax, [rax]\n");
        gen_push("rax");
        nested--;
        return GEN_PUSHED_RESULT;
    default:
        break;
        // through
    }

    gen_impl(node->lhs);
    gen_impl(node->rhs);

    // 二項演算子系
    gen_pop("rdi");
    gen_pop("rax");

    switch (node->kind) {
    case ND_ADD:
        printf("  add rax, rdi\n");
        break;
    case ND_SUB:
        printf("  sub rax, rdi\n");
        break;
    case ND_MUL:
        printf("  imul rdi\n");
        break;
    case ND_DIV:
        printf("  cqo\n");
        printf("  idiv rdi\n");
        break;
    case ND_GREATER:
        printf("  cmp rax, rdi\n");
        printf("  setl al\n");
        printf("  movzx rax, al\n");
        break;
    case ND_GREATER_EQUAL:
        printf("  cmp rax, rdi\n");
        printf("  setle al\n");
        printf("  movzx rax, al\n");
        break;
    case ND_EQUAL:
        printf("  cmp rax, rdi\n");
        printf("  sete al\n");
        printf("  movzx rax, al\n");
        break;
    case ND_NOT_EQUAL:
        printf("  cmp rax, rdi\n");
        printf("  setne al\n");
        printf("  movzx rax, al\n");
        break;
    default:
        break;
        // through
    }

    gen_push("rax");
    nested--;
    return GEN_PUSHED_RESULT;
}

GenResult gen(Node *node) {
    nested = 0;
    return gen_impl(node);
}
