/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>

// this should be enough
static char buf[4096] = {};
static char code_buf[4096 + 128] = {}; // a little larger than `buf`
static char *code_format =
"#include <stdio.h>\n"
"int main() { "
"  unsigned result = %s; "
"  printf(\"%%u\", result); "
"  return 0; "
"}";

static char* pbuf;
static int buf_len = sizeof(buf);

static char* ops[] = { "+", "-", "*", "+", "-", "*", "/" };
static int n_op = (int)(sizeof(ops) / sizeof(ops[0]));

static void gen_rand_expr() {
  int choose = rand() % 4;

  if ((pbuf - buf) > (buf_len / 10))
    choose = 0;
  
  switch(choose) {
    case 0: {
      uint32_t num = rand();
      pbuf += sprintf(pbuf, "%uu", num);
    } break;
    case 1: {
      pbuf += sprintf(pbuf, " ( ");
      gen_rand_expr();
      pbuf += sprintf(pbuf, " ) ");
    } break;
    default: {    // 2 and 3
      gen_rand_expr();
      pbuf += sprintf(pbuf, " %s ", ops[rand() % n_op]);
      gen_rand_expr();
    }
  }
}

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }

  int i;
  for (i = 0; i < loop; i ++) {
    pbuf = buf;
    gen_rand_expr();

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
    if (ret != 0) continue;

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    int result;
    ret = fscanf(fp, "%d", &result);
    pclose(fp);

    printf("%u %s\n", result, buf);
  }
  return 0;
}
