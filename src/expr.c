#include "expr.h"
#include "fmt.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Trimmed from cePort/src/integrate.c: tokenizer, parser, AST utilities and
 * constant-folding simplifier. Symbolic differentiation/integration and the
 * debug printer are not carried over here; the integration rules live in
 * symbolic.c and the text printer (ast_to_string) is at the end of this file. */

// ======================
// TOKENIZER
// ======================

typedef enum {
    TOK_NUMBER,
    TOK_SYMBOL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MULT,
    TOK_DIV,
    TOK_POW,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_SIN,
    TOK_COS,
    TOK_TAN,
    TOK_EXP,
    TOK_LN,
    TOK_LOG,
    TOK_SQRT,
    TOK_ABS,
    TOK_EOF,
    TOK_INVALID
} TokenType;

typedef struct {
    TokenType type;
    union {
        double num_val;
        char sym_name[32];
    } value;
} Token;

static int is_variable_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

// Tokenize an expression string into a token array
// Returns the number of tokens generated
static int tokenize(const char* expr, Token* tokens, int max_tokens) {
    int token_count = 0;
    int i = 0;
    int len = strlen(expr);

    while (i < len && token_count < max_tokens) {
        // Skip whitespace
        while (i < len && isspace((unsigned char)expr[i])) i++;
        if (i >= len) break;

        // Check for numbers
        if (isdigit((unsigned char)expr[i]) ||
            (expr[i] == '.' && i + 1 < len && isdigit((unsigned char)expr[i+1]))) {
            char num_str[32];
            int j = 0;
            while (i < len && (isdigit((unsigned char)expr[i]) || expr[i] == '.') && j < 31) {
                num_str[j++] = expr[i++];
            }
            num_str[j] = '\0';
            tokens[token_count].type = TOK_NUMBER;
            tokens[token_count].value.num_val = atof(num_str);
            token_count++;
            continue;
        }

        // Check for functions and symbols
        if (isalpha((unsigned char)expr[i])) {
            char sym[32];
            int j = 0;
            while (i < len && is_variable_char(expr[i]) && j < 31) {
                sym[j++] = expr[i++];
            }
            sym[j] = '\0';

            // Check if it's a known function
            if (strcmp(sym, "sin") == 0) {
                tokens[token_count].type = TOK_SIN;
            } else if (strcmp(sym, "cos") == 0) {
                tokens[token_count].type = TOK_COS;
            } else if (strcmp(sym, "tan") == 0) {
                tokens[token_count].type = TOK_TAN;
            } else if (strcmp(sym, "exp") == 0) {
                tokens[token_count].type = TOK_EXP;
            } else if (strcmp(sym, "ln") == 0) {
                tokens[token_count].type = TOK_LN;
            } else if (strcmp(sym, "log") == 0) {
                tokens[token_count].type = TOK_LOG;
            } else if (strcmp(sym, "sqrt") == 0) {
                tokens[token_count].type = TOK_SQRT;
            } else if (strcmp(sym, "abs") == 0) {
                tokens[token_count].type = TOK_ABS;
            } else {
                tokens[token_count].type = TOK_SYMBOL;
                strcpy(tokens[token_count].value.sym_name, sym);
            }
            token_count++;
            continue;
        }

        // Check for operators and delimiters
        switch (expr[i]) {
            case '+':
                tokens[token_count].type = TOK_PLUS;
                break;
            case '-':
                tokens[token_count].type = TOK_MINUS;
                break;
            case '*':
                tokens[token_count].type = TOK_MULT;
                break;
            case '/':
                tokens[token_count].type = TOK_DIV;
                break;
            case '^':
                tokens[token_count].type = TOK_POW;
                break;
            case '(':
                tokens[token_count].type = TOK_LPAREN;
                break;
            case ')':
                tokens[token_count].type = TOK_RPAREN;
                break;
            case ',':
                tokens[token_count].type = TOK_COMMA;
                break;
            default:
                tokens[token_count].type = TOK_INVALID;
        }
        token_count++;
        i++;
    }

    if (token_count < max_tokens) {
        tokens[token_count].type = TOK_EOF;
        token_count++;
    }

    return token_count;
}

// ======================
// AST NODE CREATION
// ======================

ASTNode* ast_create_num(double value) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_NUM;
    node->data.num_value = value;
    return node;
}

ASTNode* ast_create_sym(const char* name) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_SYM;
    strncpy(node->data.sym_name, name, 31);
    node->data.sym_name[31] = '\0';
    return node;
}

ASTNode* ast_create_op(NodeType op_type, ASTNode* left, ASTNode* right) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = op_type;

    ChildNode* left_child = (ChildNode*)malloc(sizeof(ChildNode));
    left_child->node = left;
    left_child->next = NULL;

    ChildNode* right_child = (ChildNode*)malloc(sizeof(ChildNode));
    right_child->node = right;
    right_child->next = left_child;

    node->data.children = right_child;
    return node;
}

ASTNode* ast_create_func(NodeType func_type, ASTNode* arg) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = func_type;

    ChildNode* arg_child = (ChildNode*)malloc(sizeof(ChildNode));
    arg_child->node = arg;
    arg_child->next = NULL;

    node->data.children = arg_child;
    return node;
}

// ======================
// PARSER (precedence-climbing recursive descent)
// ======================

static int get_precedence(TokenType type) {
    switch (type) {
        case TOK_PLUS:
        case TOK_MINUS:
            return 5;
        case TOK_MULT:
        case TOK_DIV:
            return 10;
        case TOK_POW:
            return 15;
        default:
            return 0;
    }
}

static int is_right_associative(TokenType type) {
    return type == TOK_POW;
}

// Parser state for recursive descent
typedef struct {
    Token* tokens;
    int pos;
    int token_count;
} Parser;

static Token* parser_current(Parser* p) {
    if (p->pos < p->token_count) {
        return &p->tokens[p->pos];
    }
    return NULL;
}

static void parser_advance(Parser* p) {
    p->pos++;
}

static ASTNode* parse_expression(Parser* p);
static ASTNode* parse_primary(Parser* p);

static ASTNode* parse_primary(Parser* p) {
    Token* tok = parser_current(p);
    if (!tok) return NULL;

    // Unary plus / minus
    if (tok->type == TOK_MINUS) {
        parser_advance(p);
        ASTNode* operand = parse_primary(p);
        return ast_create_op(NODE_OP_SUB, ast_create_num(0.0), operand);
    }
    if (tok->type == TOK_PLUS) {
        parser_advance(p);
        return parse_primary(p);
    }

    if (tok->type == TOK_NUMBER) {
        parser_advance(p);
        return ast_create_num(tok->value.num_val);
    }

    if (tok->type == TOK_SYMBOL) {
        parser_advance(p);
        return ast_create_sym(tok->value.sym_name);
    }

    // Handle functions
    if (tok->type == TOK_SIN || tok->type == TOK_COS || tok->type == TOK_TAN ||
        tok->type == TOK_EXP || tok->type == TOK_LN || tok->type == TOK_LOG ||
        tok->type == TOK_SQRT || tok->type == TOK_ABS) {

        NodeType func_type = tok->type - TOK_SIN + NODE_FUNC_SIN;
        parser_advance(p);

        // Expect '('
        Token* paren = parser_current(p);
        if (paren && paren->type == TOK_LPAREN) {
            parser_advance(p);
            ASTNode* arg = parse_expression(p);

            // Expect ')'
            Token* close_paren = parser_current(p);
            if (close_paren && close_paren->type == TOK_RPAREN) {
                parser_advance(p);
            }

            return ast_create_func(func_type, arg);
        }
    }

    if (tok->type == TOK_LPAREN) {
        parser_advance(p);
        ASTNode* expr = parse_expression(p);

        Token* close = parser_current(p);
        if (!close || close->type != TOK_RPAREN) {
            /* Unbalanced parens — surface as a parse error so the input
             * renderer falls back to raw text. */
            if (expr) ast_free_tree(expr);
            return NULL;
        }
        parser_advance(p);

        if (!expr) return NULL;          /* empty "()" */
        return ast_create_func(NODE_PAREN, expr);
    }

    return NULL;
}

// Simple recursive descent for expression parsing with precedence
static ASTNode* parse_expression_bp(Parser* p, int min_prec) {
    ASTNode* left = parse_primary(p);

    while (1) {
        Token* tok = parser_current(p);
        if (!tok || tok->type == TOK_EOF || tok->type == TOK_RPAREN || tok->type == TOK_COMMA) {
            break;
        }

        int prec = get_precedence(tok->type);
        if (prec < min_prec) {
            break;
        }

        TokenType op_type = tok->type;
        parser_advance(p);

        int next_prec = is_right_associative(op_type) ? prec : prec + 1;
        ASTNode* right = parse_expression_bp(p, next_prec);

        NodeType node_type;
        switch (op_type) {
            case TOK_PLUS: node_type = NODE_OP_ADD; break;
            case TOK_MINUS: node_type = NODE_OP_SUB; break;
            case TOK_MULT: node_type = NODE_OP_MUL; break;
            case TOK_DIV: node_type = NODE_OP_DIV; break;
            case TOK_POW: node_type = NODE_OP_POW; break;
            default: node_type = NODE_OP_ADD;
        }

        left = ast_create_op(node_type, left, right);
    }

    return left;
}

static ASTNode* parse_expression(Parser* p) {
    return parse_expression_bp(p, 0);
}

// ======================
// PUBLIC API
// ======================

ASTNode* parser_init_from_string(const char* expr) {
    Token tokens[256];
    int token_count = tokenize(expr, tokens, 256);

    Parser p;
    p.tokens = tokens;
    p.pos = 0;
    p.token_count = token_count;
    ASTNode* tree = parse_expression(&p);

    /* Reject trailing garbage ("2x" would otherwise silently parse as "2"):
     * everything must be consumed up to EOF. */
    if (tree) {
        Token* rest = parser_current(&p);
        if (rest && rest->type != TOK_EOF) {
            ast_free_tree(tree);
            return NULL;
        }
    }

    return tree;
}

int node_has_children(NodeType t) {
    return t != NODE_NUM && t != NODE_SYM;
}

void ast_free_tree(ASTNode* node) {
    if (!node) return;

    if (node_has_children(node->type)) {
        ChildNode* child = node->data.children;
        while (child) {
            ChildNode* next = child->next;
            ast_free_tree(child->node);
            free(child);
            child = next;
        }
    }

    free(node);
}

// ======================
// AST HELPERS
// ======================

int node_is_func(NodeType t) {
    return t >= NODE_FUNC_SIN && t <= NODE_FUNC_ABS;
}

static int node_is_binop(NodeType t) {
    return t >= NODE_OP_ADD && t <= NODE_OP_POW;
}

ASTNode* ast_get_arg(const ASTNode* n) {
    if (!n || !node_has_children(n->type) || !n->data.children) return NULL;
    return n->data.children->node;
}

ASTNode* ast_get_right(const ASTNode* n) {
    return ast_get_arg(n);  // first list item is the right operand
}

ASTNode* ast_get_left(const ASTNode* n) {
    if (!n || !node_has_children(n->type)) return NULL;
    ChildNode* c = n->data.children;
    if (!c || !c->next) return NULL;
    return c->next->node;
}

static int ast_is_num(const ASTNode* n) {
    return n && n->type == NODE_NUM;
}

int ast_num_eq(const ASTNode* n, double v) {
    return ast_is_num(n) && n->data.num_value == v;
}

ASTNode* ast_clone(const ASTNode* node) {
    if (!node) return NULL;
    ASTNode* copy = (ASTNode*)malloc(sizeof(ASTNode));
    copy->type = node->type;

    if (node->type == NODE_NUM) {
        copy->data.num_value = node->data.num_value;
        return copy;
    }
    if (node->type == NODE_SYM) {
        memcpy(copy->data.sym_name, node->data.sym_name, sizeof(node->data.sym_name));
        return copy;
    }

    copy->data.children = NULL;
    ChildNode** tail = &copy->data.children;
    for (ChildNode* c = node->data.children; c; c = c->next) {
        ChildNode* nc = (ChildNode*)malloc(sizeof(ChildNode));
        nc->node = ast_clone(c->node);
        nc->next = NULL;
        *tail = nc;
        tail = &nc->next;
    }
    return copy;
}

int ast_contains_var(const ASTNode* node, char var) {
    if (!node) return 0;
    if (node->type == NODE_NUM) return 0;
    if (node->type == NODE_SYM) {
        return node->data.sym_name[0] == var
            && node->data.sym_name[1] == '\0';
    }
    for (ChildNode* c = node->data.children; c; c = c->next) {
        if (ast_contains_var(c->node, var)) return 1;
    }
    return 0;
}

// Replace `child`'s slot in `parent` with NULL so freeing `parent` (or an
// ancestor) leaves `child` alive. Returns `child`.
static ASTNode* unlink_child(ASTNode* parent, ASTNode* child) {
    if (parent && node_has_children(parent->type)) {
        for (ChildNode* c = parent->data.children; c; c = c->next) {
            if (c->node == child) { c->node = NULL; break; }
        }
    }
    return child;
}

// Detach `keep` from `parent`'s child list (replacing it with NULL), then
// free the parent and remaining children. Returns `keep`.
static ASTNode* detach_and_free_parent(ASTNode* parent, ASTNode* keep) {
    unlink_child(parent, keep);
    ast_free_tree(parent);
    return keep;
}

// ======================
// SIMPLIFIER
// ======================

ASTNode* ast_simplify(ASTNode* node) {
    if (!node) return NULL;
    if (!node_has_children(node->type)) return node;

    // Bottom-up: simplify children first
    for (ChildNode* c = node->data.children; c; c = c->next) {
        c->node = ast_simplify(c->node);
    }

    // Strip explicit-paren wrappers; they exist only for display.
    if (node->type == NODE_PAREN) {
        ASTNode* inner = ast_get_arg(node);
        return detach_and_free_parent(node, inner);
    }

    // Unary function: only constant-fold special cases we care about
    if (node_is_func(node->type)) {
        ASTNode* arg = ast_get_arg(node);
        // ln(1) = 0
        if (node->type == NODE_FUNC_LN && ast_num_eq(arg, 1.0)) {
            ast_free_tree(node);
            return ast_create_num(0.0);
        }
        // exp(0) = 1
        if (node->type == NODE_FUNC_EXP && ast_num_eq(arg, 0.0)) {
            ast_free_tree(node);
            return ast_create_num(1.0);
        }
        // sin(0)=0, cos(0)=1, tan(0)=0
        if (node->type == NODE_FUNC_SIN && ast_num_eq(arg, 0.0)) {
            ast_free_tree(node); return ast_create_num(0.0);
        }
        if (node->type == NODE_FUNC_COS && ast_num_eq(arg, 0.0)) {
            ast_free_tree(node); return ast_create_num(1.0);
        }
        if (node->type == NODE_FUNC_TAN && ast_num_eq(arg, 0.0)) {
            ast_free_tree(node); return ast_create_num(0.0);
        }
        return node;
    }

    if (!node_is_binop(node->type)) return node;

    ASTNode* L = ast_get_left(node);
    ASTNode* R = ast_get_right(node);

    // Constant fold
    if (ast_is_num(L) && ast_is_num(R)) {
        double a = L->data.num_value;
        double b = R->data.num_value;
        double v = 0.0;
        int ok = 1;
        switch (node->type) {
            case NODE_OP_ADD: v = a + b; break;
            case NODE_OP_SUB: v = a - b; break;
            case NODE_OP_MUL: v = a * b; break;
            case NODE_OP_DIV: if (b == 0.0) ok = 0; else v = a / b; break;
            case NODE_OP_POW: {
                // Restrict to small integer exponents to avoid pulling in pow()
                if (b == (int)b && b >= 0 && b <= 16) {
                    v = 1.0;
                    int n = (int)b;
                    for (int i = 0; i < n; i++) v *= a;
                } else ok = 0;
                break;
            }
            default: ok = 0;
        }
        if (ok) {
            ast_free_tree(node);
            return ast_create_num(v);
        }
    }

    switch (node->type) {
        case NODE_OP_ADD:
            if (ast_num_eq(L, 0.0)) return detach_and_free_parent(node, R);
            if (ast_num_eq(R, 0.0)) return detach_and_free_parent(node, L);
            break;
        case NODE_OP_SUB:
            if (ast_num_eq(R, 0.0)) return detach_and_free_parent(node, L);
            // 0 - x is left as-is (unary negation form) ...
            // ... but 0 - (0 - u) is just u, and a - (0 - u) is a + u.
            if (R->type == NODE_OP_SUB && ast_num_eq(ast_get_left(R), 0.0)) {
                ASTNode* U = unlink_child(R, ast_get_right(R));
                if (ast_num_eq(L, 0.0)) {
                    ast_free_tree(node);
                    return U;
                }
                unlink_child(node, L);
                ast_free_tree(node);
                return ast_create_op(NODE_OP_ADD, L, U);
            }
            break;
        case NODE_OP_MUL:
            if (ast_num_eq(L, 0.0) || ast_num_eq(R, 0.0)) {
                ast_free_tree(node);
                return ast_create_num(0.0);
            }
            if (ast_num_eq(L, 1.0)) return detach_and_free_parent(node, R);
            if (ast_num_eq(R, 1.0)) return detach_and_free_parent(node, L);
            /* A numeric factor next to a fraction or a negation: move it
             * inside, so the constant-factor integration rule yields x^2
             * rather than 2*x^2/2, and -(2*cos(x)) rather than 2*(-cos(x)).
             *   c * (N/d) -> (c/d)*N when c/d is an integer, else (c*N)/d
             *   c * (0-u) -> 0 - c*u                                      */
            {
                ASTNode* cn = ast_is_num(L) ? L : (ast_is_num(R) ? R : NULL);
                ASTNode* other = (cn == L) ? R : L;
                if (cn && other->type == NODE_OP_DIV &&
                    ast_is_num(ast_get_right(other)) &&
                    ast_get_right(other)->data.num_value != 0.0) {
                    double c = cn->data.num_value;
                    double d = ast_get_right(other)->data.num_value;
                    double q = c / d;
                    ASTNode* N = unlink_child(other, ast_get_left(other));
                    ast_free_tree(node);
                    if (q == (double)(long)q) {
                        if (q == 1.0) return N;
                        return ast_simplify(ast_create_op(NODE_OP_MUL, ast_create_num(q), N));
                    }
                    return ast_create_op(NODE_OP_DIV,
                        ast_simplify(ast_create_op(NODE_OP_MUL, ast_create_num(c), N)),
                        ast_create_num(d));
                }
                if (cn && other->type == NODE_OP_SUB &&
                    ast_num_eq(ast_get_left(other), 0.0)) {
                    double c = cn->data.num_value;
                    ASTNode* U = unlink_child(other, ast_get_right(other));
                    ast_free_tree(node);
                    return ast_create_op(NODE_OP_SUB, ast_create_num(0.0),
                        ast_simplify(ast_create_op(NODE_OP_MUL, ast_create_num(c), U)));
                }
            }
            break;
        case NODE_OP_DIV:
            if (ast_num_eq(R, 1.0)) return detach_and_free_parent(node, L);
            if (ast_num_eq(L, 0.0)) {
                ast_free_tree(node);
                return ast_create_num(0.0);
            }
            /* Fold nested constant denominators: (N/d1)/d2 -> N/(d1*d2). */
            if (L->type == NODE_OP_DIV && ast_is_num(R)) {
                ASTNode* LD = ast_get_right(L);
                if (ast_is_num(LD)) {
                    double newd = LD->data.num_value * R->data.num_value;
                    ASTNode* LN = ast_get_left(L);
                    /* Detach the numerator so freeing the old tree spares it. */
                    for (ChildNode* c = L->data.children; c; c = c->next) {
                        if (c->node == LN) { c->node = NULL; break; }
                    }
                    ast_free_tree(node);
                    if (newd == 1.0) return LN;
                    return ast_create_op(NODE_OP_DIV, LN, ast_create_num(newd));
                }
            }
            break;
        case NODE_OP_POW:
            if (ast_num_eq(R, 0.0)) {
                ast_free_tree(node);
                return ast_create_num(1.0);
            }
            if (ast_num_eq(R, 1.0)) return detach_and_free_parent(node, L);
            if (ast_num_eq(L, 1.0)) {
                ast_free_tree(node);
                return ast_create_num(1.0);
            }
            if (ast_num_eq(L, 0.0)) {
                ast_free_tree(node);
                return ast_create_num(0.0);
            }
            break;
        default: break;
    }

    return node;
}

// ======================
// TEXT PRINTER
// ======================

/* Plain-text form of an AST, e.g. for a raw-text fallback when the 2-D
 * layout is too wide. Precedence-driven parenthesisation; SUB(0, x) prints
 * as unary "-x". Numbers go through fmt_g (no printf in this build). */

static int node_prec(const ASTNode* n) {
    if (!n) return 5;
    switch (n->type) {
        case NODE_OP_ADD:
        case NODE_OP_SUB: return 1;
        case NODE_OP_MUL:
        case NODE_OP_DIV: return 2;
        case NODE_OP_POW: return 3;
        default: return 4;
    }
}

static int print_node(const ASTNode* node, char* buf, int pos, int cap, int parent_prec) {
    if (!node || pos >= cap - 1) return pos;

    int my_prec = node_prec(node);
    int parens = my_prec < parent_prec;
    if (parens && pos < cap - 1) buf[pos++] = '(';

    if (node->type == NODE_NUM) {
        char tmp[16];
        int len = fmt_g(tmp, (float)node->data.num_value);
        for (int i = 0; i < len && pos < cap - 1; i++) buf[pos++] = tmp[i];
    } else if (node->type == NODE_SYM) {
        const char* s = node->data.sym_name;
        while (*s && pos < cap - 1) buf[pos++] = *s++;
    } else if (node->type == NODE_PAREN) {
        if (pos < cap - 1) buf[pos++] = '(';
        pos = print_node(ast_get_arg(node), buf, pos, cap, 0);
        if (pos < cap - 1) buf[pos++] = ')';
    } else if (node_is_binop(node->type)) {
        const ASTNode* L = ast_get_left(node);
        const ASTNode* R = ast_get_right(node);
        if (node->type == NODE_OP_SUB && ast_num_eq(L, 0.0)) {
            if (pos < cap - 1) buf[pos++] = '-';
            pos = print_node(R, buf, pos, cap, my_prec + 1);
        } else {
            char op = '?';
            switch (node->type) {
                case NODE_OP_ADD: op = '+'; break;
                case NODE_OP_SUB: op = '-'; break;
                case NODE_OP_MUL: op = '*'; break;
                case NODE_OP_DIV: op = '/'; break;
                case NODE_OP_POW: op = '^'; break;
                default: break;
            }
            pos = print_node(L, buf, pos, cap, my_prec);
            if (pos < cap - 1) buf[pos++] = op;
            int right_prec = my_prec;
            if (node->type == NODE_OP_SUB || node->type == NODE_OP_DIV) right_prec = my_prec + 1;
            pos = print_node(R, buf, pos, cap, right_prec);
        }
    } else {
        const char* fname;
        switch (node->type) {
            case NODE_FUNC_SIN:  fname = "sin";  break;
            case NODE_FUNC_COS:  fname = "cos";  break;
            case NODE_FUNC_TAN:  fname = "tan";  break;
            case NODE_FUNC_EXP:  fname = "exp";  break;
            case NODE_FUNC_LN:   fname = "ln";   break;
            case NODE_FUNC_LOG:  fname = "log";  break;
            case NODE_FUNC_SQRT: fname = "sqrt"; break;
            case NODE_FUNC_ABS:  fname = "abs";  break;
            default:             fname = "?";    break;
        }
        while (*fname && pos < cap - 1) buf[pos++] = *fname++;
        if (pos < cap - 1) buf[pos++] = '(';
        pos = print_node(ast_get_arg(node), buf, pos, cap, 0);
        if (pos < cap - 1) buf[pos++] = ')';
    }

    if (parens && pos < cap - 1) buf[pos++] = ')';
    return pos;
}

void ast_to_string(const ASTNode* node, char* buf, int buf_size) {
    if (!buf || buf_size <= 0) return;
    int pos = print_node(node, buf, 0, buf_size, 0);
    if (pos >= buf_size) pos = buf_size - 1;
    buf[pos] = '\0';
}
