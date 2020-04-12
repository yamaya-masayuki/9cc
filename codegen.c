#include "9cc.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static GenResult gen_impl(Node *);

/*
 * 与えられたノードが変数を指しているときに、その変数のアドレスを計算して、それ
 * をスタックにプッシュします。
 * それ以外の場合にはエラーを表示します。これにより`(a+1)=2`のような式が排除さ
 * れることになります。
 */
void gen_address_to_local_variable(Node *node) {
    if (node->kind != ND_LVAR) {
        error_exit("代入の左辺値が変数ではありません(var)。%s", node_description(node));
    }

    // 1. RBPからオフセット分減算する
    printf("  mov rax, rbp   # var\n");
    printf("  sub rax, %-4d  # var\n", node->offset);

    // 2. 結果（変数のアドレス）をスタックに積む
    printf("  push rax       # var\n");
}

/*
 * グローバル変数の参照
 */
void gen_address_to_global_variable(const Node *node) {
    printf("  mov rax, [_%s@GOTPCREL + rip]\n", node_name(node));
    printf("  push rax\n");
}

void gen_push_value_indirected_by_adderss(const Node *node) {
    if (node->type->type == ARRAY) {
        return;
    }
    printf("  pop rax        # push value\n");
    printf("  mov rax, [rax] # push value\n");
    printf("  push rax       # push value\n");
}

void gen_assign(const Node *node) {
    printf("  # Assign {{{\n");
    /*
     * 変数への代入
     * - 左辺の変数のアドレスをスタックに置く ~ gen_address_to_local_variable()
     * - 右辺を評価し結果をスタックに置く ~ gen_impl()
     * - スタックをポップし右辺値をrbxへ
     * - スタックをポップし変数アドレスをraxへ
     * - raxアドレスにrbx値を書く
     * - rbx値をスタックに置く
     */
    switch (node->lhs->kind) {
    case ND_DEREF:
        // 直接rhsをコード生成するのがミソ
        gen_impl(node->lhs->rhs);
        break;
    case ND_LVAR:
        // スタックにLHSのアドレスを入れたままにしておく
        gen_address_to_local_variable(node->lhs);
        break;
    case ND_GLOBAL_REF:
        gen_address_to_global_variable(node->lhs);
        break;
    default:
        error_exit("代入の左辺値は変数またはデリファレンス演算子でなければなりません。%s", node_description(node->lhs));
        break;
    }
    GenResult result = gen_impl(node->rhs); // スタックに右辺の評価結果が積まれている
    assert(result == GEN_PUSHED_RESULT); // ここでのgen_implは必ずスタックに結果をプッシュしなければならない
    printf("  pop rbx        # assign\n"); // 右辺(の結果)
    printf("  pop rax        # assign\n"); // 左辺(のアドレス)

    // 左辺(のアドレス)に右辺の値をいれる
    if (node->lhs->kind == ND_GLOBAL_REF) {
        printf("  mov DWORD PTR [rax], ebx # assign\n");
    } else {
        printf("  mov [rax], rbx # assign\n");
    }

    printf("  push rbx       # assign\n"); // 右辺の値をスタックに積む

    printf("  # }}} Assign\n");
}

static const char *ArgRegsiters[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

void gen_fun(Node *node) {
    static char buffer[1024];

    for (int i = 0; i < node->block->len; ++i) {
        GenResult result = gen_impl((Node *)node->block->data[i]);
        assert(result == GEN_PUSHED_RESULT);
        printf("  pop rax\n");
        printf("  mov %s, rax\n", ArgRegsiters[i]);
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
            error_exit("代入の左辺値が変数ではありません(args)。%s", node_description(arg));
        printf("  mov qword ptr [rbp - %d], %s  # argument %d\n", arg->offset, ArgRegsiters[i], i);
    }

    // ブロック部分: node->lhsにはND_BLOCKが格納されている
    for (int i = 0; i < node->lhs->block->len; ++i) {
        GenResult resutl = gen_impl(node->lhs->block->data[i]);
    }

    // エピローグ
    printf("  pop rbp  # epilogue\n");
    printf("  ret      # epilogue\n");
}

static int nested = 0;

GenResult gen_impl(Node *node) {
    static int label_sequence_no = 0;
    GenResult result;

    D("%s, nested=%d", node_description(node), nested);
    nested++;

    switch (node->kind) {
    case ND_NUM:
        printf("  push %-4d      # constant\n", node->val);
        nested--;
        return GEN_PUSHED_RESULT;
    case ND_LVAR:
        printf("  # variable {{{ type=%s\n", type_description(node->type));
        /*
         * ローカル変数の参照
         * - 左辺の変数のアドレスをスタックに置く ~ gen_address_to_local_variable()
         * - 配列ではない場合に限り...
         *  * スタックをポップする
         *  * その値をアドレスとみなし参照先の値を取得する)
         *  - 配列は"初期化済のポインタ変数"なので特別扱いする
         */
        gen_address_to_local_variable(node);
        gen_push_value_indirected_by_adderss(node);
        printf("  # }}} variable\n");
        nested--;
        return GEN_PUSHED_RESULT;
    case ND_GLOBAL_DEF:
        return GEN_DONT_PUSHED_RESULT; // 何もせずreturn
    case ND_GLOBAL_REF:
        gen_address_to_global_variable(node);
        gen_push_value_indirected_by_adderss(node);
        nested--;
        return GEN_PUSHED_RESULT;
    case ND_ASSIGN:
        gen_assign(node);
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
        printf("  pop rax\n");
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
        printf("  pop rax\n");
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
        printf("  # Address {{{\n");
        gen_address_to_local_variable(node->rhs);
        nested--;
        printf("  # }}} Address\n");
        return GEN_PUSHED_RESULT;
    case ND_DEREF:
        printf("  # Dereference {{{\n");
        gen_impl(node->rhs); // 右辺値としてコンパイルする
        printf("  pop rax         # dereference(outside)\n");
        printf("  mov rax, [rax]  # dereference(outside)\n");
        printf("  push rax        # dereference(outside)\n");
        printf("  # }}} Dereference\n");
        nested--;
        return GEN_PUSHED_RESULT;
    default:
        break;
        // through
    }

    /*
     * 二項演算子系
     */ 
    int ptr_offset = 1;
    if (node_hands_is_treat_pointer(node)) {
        ptr_offset = 4; // int* のとき
        if (node_hands_is_pointer_variable_many(node)) {
            ptr_offset = 8; // int **以上の時
        }
    }

    // (1 + p)のように左手に定数がくる場合は処理順を逆にする
    // 後続のptr_offset計算のため
    if (ptr_offset != 1 && node->lhs->kind == ND_NUM) {
        gen_impl(node->rhs);
        gen_impl(node->lhs);
    } else {
        gen_impl(node->lhs);
        gen_impl(node->rhs);
    }

    // スタックに積まれている非演算数を取り出す
    printf("  pop rdi       # binary operator\n"); // 右手
    printf("  pop rax       # binary operator\n"); // 左手

    // ポインタの演算のために右手(rdi)をデータサイズ倍する
    printf("  mov rbx, %-4d # Compute pointer\n", ptr_offset);
    printf("  imul rdi, rbx # Compute pointer\n"); // rdi = rdi * rbx

    switch (node->kind) {
    case ND_ADD:
        printf("  add rax, rdi  # Addition\n");
        break;
    case ND_SUB:
        printf("  sub rax, rdi  # Subtraction\n");
        break;
    case ND_MUL:
        printf("  imul rdi      # Multiplication\n");
        break;
    case ND_DIV:
        printf("  cqo           # Division\n");
        printf("  idiv rdi      # Division\n");
        break;
    case ND_GREATER:
        printf("  cmp rax, rdi  # Greator\n");
        printf("  setl al       # Greator\n");
        printf("  movzx rax, al # Greator\n");
        break;
    case ND_GREATER_EQUAL:
        printf("  cmp rax, rdi  # Greator Equal\n");
        printf("  setle al      # Greator Equal\n");
        printf("  movzx rax, al # Greator Equal\n");
        break;
    case ND_EQUAL:
        printf("  cmp rax, rdi  # Equal\n");
        printf("  sete al       # Equal\n");
        printf("  movzx rax, al # Equal\n");
        break;
    case ND_NOT_EQUAL:
        printf("  cmp rax, rdi  # Not equal\n");
        printf("  setne al      # Not equal\n");
        printf("  movzx rax, al # Not equal\n");
        break;
    default:
        break;
        // through
    }

    printf("  push rax  # gen_impl's LAST\n");
    nested--;
    return GEN_PUSHED_RESULT;
}

void gen_global_varibale(const Node *node)
{
    printf("  .comm _%s, %d\n", node_name(node), type_byte_size(node->type));
}

GenResult gen(Node *node) {
    nested = 0;
    return gen_impl(node);
}
