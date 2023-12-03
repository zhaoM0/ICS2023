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

#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

#define DEBUG_MODE

enum {
  TK_NOTYPE = 255, 
  TK_EQ     = 0,
  TK_NEQ    = 1,
  TK_AND    = 2,
  TK_OR     = 3,
  TK_DEC    = 4,
  TK_HEX    = 5,
  TK_REG    = 6,
  TK_DEREF  = 7,
  TK_LBRA   = 40,   // `(`
  TK_RBRA   = 41,   // ')`
  TK_ADD    = 43,   // `+`
  TK_MUL    = 42,   // `*`
  TK_SUB    = 45,   // `-`
  TK_DIV    = 47,   // `/`
  TK_MOD    = 37,   // `%`
};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {
  {" +",       TK_NOTYPE},  // spaces
  {"\\+",      '+'},        // plus
  {"\\-",      '-'},        // sub
  {"\\*",      '*'},        // mul
  {"/",        '/'},        // div
  {"%",        '%'},        // mod
  {"\\(",      '('},        // left bracket
  {"\\)",      ')'},        // right bracket
  {"==",       TK_EQ},      // equal
  {"!=",       TK_NEQ},     // not equal
  {"&&",       TK_AND},     // and
  {"\\|\\|",   TK_OR},      // or
  {"[0-9]+u?", TK_DEC},     // dec
  {"0x[0-9a-fA-F]{1,8}", TK_HEX},  // hex
  {"\\$[a-z0-9]+", TK_REG}, // register
};

// operator priority table
static int op_priority[256]= {
  [TK_MUL] = 1,
  [TK_DIV] = 1,
  [TK_MOD] = 1,
  [TK_ADD] = 2,
  [TK_SUB] = 2,
  [TK_EQ]  = 3,
  [TK_NEQ] = 3,
  [TK_AND] = 4,
  [TK_OR]  = 4,
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[4096] __attribute__((used)) = {};
static int nr_token __attribute__((used)) = 0;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

      #ifdef DEBUG_MODE
        printf("match rules[%d] = \"%s\" at position %d with len %d: %.*s\n",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);
      #endif

        position += substr_len;

        /* skip all whitespace */
        if (rules[i].token_type == TK_NOTYPE) 
          continue;

        /* save token class and lexemes */
        tokens[nr_token].type = rules[i].token_type;

        if (tokens[nr_token].type == TK_DEC || tokens[nr_token].type == TK_DEC || tokens[nr_token].type == TK_REG) {
          Assert(substr_len < 32, "number overflow\n"); 
          strncpy(tokens[nr_token].str, substr_start, substr_len);
          *(tokens[nr_token].str + substr_len) = '\0'; 
        }

        ++ nr_token;

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

// Simulate stack (just for counting)
// The stack variable must be initialized to zero.
static void push_stack(int* sval) { 
  ++(*sval);
}
static bool pop_stack(int* sval) {
  --(*sval);
  if (*sval < 0) return false;
  else           return true;
}

// check sub-expression head and tail.
static bool check_parentheses(ssize_t st, ssize_t ed) {
  int stn = 0;    // init stack 

  if (tokens[st].type != '(' || tokens[ed].type != ')')
    return false;
  
  for (ssize_t cur = st + 1; cur < ed; ++cur) {
    switch (tokens[cur].type) {
      case '(': push_stack(&stn); break;
      case ')': if (!pop_stack(&stn)) return false; break;
    }
  }

  if (stn == 0) return true;
  else          return false;
}

void display_expr(ssize_t st, ssize_t ed);

// find main opearator
static ssize_t find_mainop(ssize_t st, ssize_t ed) {
  ssize_t mop = -1;
  int mop_pr = 0;
  int stn = 0;

  for (int i = st; i <= ed; ++i) {
    switch (tokens[i].type) {
      case '(': 
        push_stack(&stn); break;
      case ')': 
        if (!pop_stack(&stn)) { return -1; } break;
      case TK_HEX: case TK_DEC: case TK_REG: 
        { /* nothing to do*/ } break;
      default: /* operator */
        if (stn == 0 && op_priority[tokens[i].type] >= mop_pr) {
          mop = i;
          mop_pr = op_priority[tokens[i].type]; 
        }
    }
  }
  
  return mop;
}

#ifdef DEBUG_MODE
void display_expr(ssize_t st, ssize_t ed) {
  for (int i = st; i <= ed; ++i) {
    switch (tokens[i].type) {
      case TK_DEC: case TK_HEX: case TK_REG:
        printf("%s", tokens[i].str); break;
      default:
        printf("%c", tokens[i].type);
    }
  }
}
#endif

static word_t eval(ssize_t st, ssize_t ed, bool* success) {
  word_t ret = -1;

  if (st > ed) {
    /* Bad expression */
    *success = false;
    Assert(0, "[%s] bad expression\n", __FILE__);

  } else if (st == ed) {
    /* Single token.
     * For now this token should be a number.
     */
    switch (tokens[st].type) {
      case TK_DEC: 
      case TK_HEX: ret = strtoul(tokens[st].str, NULL, 0); break;
      case TK_REG: ret = isa_reg_str2val(tokens[st].str + 1, success); break; // skip `$`
      default: 
        *success = false;
        Assert(0, "[%s] unrecognized number token\n", __FILE__);
    }

  } else if (check_parentheses(st, ed) == true) {
    /* The expression is surround by a matched pair of parentheses.
     * If that is the case, just throw away the parentheses.
     */
    ret = eval(st + 1, ed - 1, success);

  } else {
    /* The expression that the leftmost and rightmost are not both
     * parentheses. Just like this `1 + (2 * 3)`
     */
    ssize_t mop = find_mainop(st, ed);
    Assert(mop > 0, "[%s] main operator search failed\n", __FILE__);

    bool lexpr = false, rexpr = false;
    word_t lval, rval;
    
    lval = eval(st, mop - 1, &lexpr);
    rval = eval(mop + 1, ed, &rexpr);
    Assert((lexpr && rexpr), "[%s] illegal right expression\n", __FILE__);

    switch (tokens[mop].type) {
      case '+': ret = lval + rval; break;
      case '-': ret = lval - rval; break; 
      case '*': ret = lval * rval; break;
      case '/': ret = lval / rval; break;
      case '%': ret = lval % rval; break;
      default: Assert(0, "[%s] main operator is invalid.\n", __FILE__);
    }
  }

#ifdef DEBUG_MODE
  display_expr(st, ed);
  printf(" = %u\n", ret);
#endif

  *success = true;
  return ret;
}


word_t expr(char *e, bool *success) {
  
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  /* TODO: Insert codes to evaluate the expression. */
  return eval(0, nr_token - 1, success);
}
