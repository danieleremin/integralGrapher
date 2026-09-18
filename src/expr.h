#ifndef EXPR_H
#define EXPR_H

#include <stdint.h>

/* Expression parser + AST, trimmed from the cePort symbolic integral
 * calculator (reverse_engineering/cePort/src/integrate.c). The symbolic
 * engine (derivative/antiderivative rules) is not included here — this
 * project evaluates numerically (see eval.h). */

// Node types for the Abstract Syntax Tree
typedef enum {
    // Literals
    NODE_NUM,       // Numeric constant
    NODE_SYM,       // Symbol/variable

    // Binary operators
    NODE_OP_ADD,    // +
    NODE_OP_SUB,    // -
    NODE_OP_MUL,    // *
    NODE_OP_DIV,    // /
    NODE_OP_POW,    // ^

    // Unary functions
    NODE_FUNC_SIN,
    NODE_FUNC_COS,
    NODE_FUNC_TAN,
    NODE_FUNC_EXP,
    NODE_FUNC_LN,
    NODE_FUNC_LOG,
    NODE_FUNC_SQRT,
    NODE_FUNC_ABS,

    // Special
    NODE_FUNC_CUSTOM, // For future user-defined functions

    // Explicit user parentheses, preserved for display.
    // Stripped by ast_simplify before computation.
    NODE_PAREN
} NodeType;

// Child node in the N-ary tree (linked list)
typedef struct ChildNode {
    struct ASTNode* node;
    struct ChildNode* next;
} ChildNode;

// Main AST node structure using union for memory efficiency
typedef struct ASTNode {
    NodeType type;

    union {
        // For NODE_NUM: numeric value
        double num_value;

        // For NODE_SYM: variable name (max 32 chars, null-terminated)
        char sym_name[32];

        // For operators and functions: child nodes (N-ary tree)
        ChildNode* children;
    } data;

} ASTNode;

// Parse an expression string. Returns NULL on parse error.
ASTNode* parser_init_from_string(const char* expr);

void ast_free_tree(ASTNode* node);

/* Node constructors (malloc'd, never NULL-checked � same as the parser).
 * ast_create_op stores the RIGHT operand first in the child list. */
ASTNode* ast_create_num(double value);
ASTNode* ast_create_sym(const char* name);
ASTNode* ast_create_op(NodeType op_type, ASTNode* left, ASTNode* right);
ASTNode* ast_create_func(NodeType func_type, ASTNode* arg);   // also NODE_PAREN

// AST utilities (consume nothing unless documented otherwise)
ASTNode* ast_clone(const ASTNode* node);
int      ast_contains_var(const ASTNode* node, char var);
ASTNode* ast_simplify(ASTNode* node);                 // consumes input, returns simplified
void     ast_to_string(const ASTNode* node, char* buf, int buf_size);   // plain text, NUL-terminated

/* Child accessors. The child list from ast_create_op is right-then-left,
 * so the FIRST list element is the RIGHT operand — use these, never walk
 * data.children directly for operand order. */
int      node_has_children(NodeType t);
int      node_is_func(NodeType t);        // NODE_FUNC_SIN..NODE_FUNC_ABS
int      ast_num_eq(const ASTNode* n, double v);   // n is NODE_NUM equal to v
ASTNode* ast_get_left(const ASTNode* n);
ASTNode* ast_get_right(const ASTNode* n);
ASTNode* ast_get_arg(const ASTNode* n);   // unary functions / NODE_PAREN

#endif
