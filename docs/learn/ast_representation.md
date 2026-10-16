# Holding the AST in C++: three options

The first design question of the parser plan. All three store the same tree; they differ in how
a node refers to its children, which decides how the rest of the interpreter reads the tree.

The running example is the node for `a + b`: a binary operator with two child expressions.

## 1. A class hierarchy

Every node type derives from a base class, and children are owned through pointers to the base.

```cpp
struct Expr {
    virtual ~Expr() = default;
    int line = 0;
};

struct Name final : Expr {
    std::string text;
};

struct Binary final : Expr {
    BinaryOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};
```

Code that walks the tree (the printer, the checks, the interpreter) needs to know which kind of
node it holds. Either it asks (`dynamic_cast<Binary*>(expr)`), or every node gets a virtual
`accept(Visitor&)` and every pass becomes a `Visitor` class with one method per node type (the
*visitor pattern*).

**Pros**
- The most familiar OOP shape; most books and tutorials (including Crafting Interpreters) use it.
- Each node only takes the memory it needs.

**Cons**
- One heap allocation per node.
- Walking the tree means either `dynamic_cast` chains (slow, and nothing tells you when you
  forgot a node type) or visitor boilerplate (two virtual calls per node, and every pass split
  across many methods).
- Behavior is scattered: `Binary` evaluation lives in one class, `Binary` printing in another.

## 2. `std::variant`

The set of node types is a closed list, and an expression *is* one of them.

```cpp
struct Binary;
struct Call;

using Expr = std::variant<Nil, True, False, Integer, Float, String, Name, Binary, Call /* ... */>;

struct Binary {
    BinaryOp op;
    std::unique_ptr<Expr> left; // a variant cannot contain itself, so children are boxed
    std::unique_ptr<Expr> right;
    int line = 0;
};
```

Walking the tree is `std::visit` with one lambda per node type:

```cpp
std::visit(overloaded{
    [&](const Integer& node) { /* ... */ },
    [&](const Binary& node) { /* ... node.left, node.right ... */ },
    // ...one per alternative
}, expr);
```

**Pros**
- **The compiler checks every pass is complete.** With no catch-all lambda, adding a node type
  makes every `std::visit` that misses it fail to compile. Lua's grammar is fixed, so a closed
  set fits it exactly.
- Plain structs with value semantics: no base class, no virtual functions, no casts.
- All the code for one pass sits in one place, which reads like the grammar.

**Cons**
- Children still need `std::unique_ptr` (one allocation per child).
- Every `Expr` is as large as its largest alternative.
- Compiler errors from a wrong `std::visit` are long and hard to read at first.

## 3. An arena with indices

All nodes live in big `std::vector`s owned by one `Ast` object, and a node refers to its children
by **index**, not by pointer.

```cpp
struct ExprId { std::uint32_t index; }; // a small, typed handle

struct Binary {
    BinaryOp op;
    ExprId left;
    ExprId right;
};

using ExprNode = std::variant<Nil, Integer, Name, Binary /* ... */>;

struct Ast {
    std::vector<ExprNode> exprs;
    const ExprNode& operator[](ExprId id) const { return exprs[id.index]; }
};
```

**Pros**
- **Fast:** a few large allocations instead of one per node. Nodes sit next to each other in
  memory, which the CPU cache likes, and an index is 4 bytes instead of an 8-byte pointer.
- Freeing the tree is freeing a few vectors. With `unique_ptr` trees, destroying a very deep tree
  (say `a + a + a + ...` with a million terms) recurses once per level and can overflow the
  stack. An arena has no such recursion.
- Easy to copy, save to a file, or compare.

**Cons**
- Every access goes through the arena (`ast[node.left]`), so code is noisier, and functions need
  the `Ast` passed around.
- An index can silently point into the wrong arena; that bug is harder to spot than a dangling
  pointer.
- It is an optimization first. Its benefits matter most for big programs and fast compilers.

## Side by side

|                                | Class hierarchy        | `std::variant`              | Arena                       |
| ------------------------------ | ---------------------- | --------------------------- | --------------------------- |
| Allocations                    | one per node           | one per boxed child         | a few per tree              |
| Forgetting a node type in a pass | silent (`dynamic_cast`) or caught (pure-virtual visitor) | compile error | compile error (if nodes are a variant) |
| Reading the code               | familiar, scattered    | close to the grammar        | noisier (`ast[id]`)         |
| Very deep trees                | recursive destruction  | recursive destruction       | no recursion to free        |
| Best for                       | open-ended node sets   | closed sets like a grammar  | speed and big inputs        |

## A note on very deep trees

The deep-tree problem is not only about freeing memory. A tree-walking interpreter evaluates
`a + a + ... + a` by recursion too, whatever the representation. So all three need the same
answer: a depth limit with a clear error (the plan's part 9, G10), like Lua's own limits.

## Recommendation

**`std::variant` with boxed children** (option 2). It matches a fixed grammar, the compiler
checks that every pass handles every node, and the code stays plain structs you can read. That
fits this project's priority: understanding and visibility first.

An arena is the natural next step if speed ever matters here. But the roadmap's stage 13
replaces the tree walker with a bytecode VM, and that changes how fast tree access needs to be
anyway.
