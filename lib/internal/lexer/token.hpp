#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cpplua {

// Every kind of token the lexer can produce. Later parts add names, numbers,
// strings, keywords and operators (see docs/spec/lexical.md, L11).
enum class TokenKind : std::uint8_t {
    EndOfStream, // end of the input, repeated on every later call (L1.2)

    // Tokens with a value
    Name,    // a name such as x or _ENV; value holds its text (L4.1)
    Other,   // any other byte, such as @ or $; value holds that byte (L9.1)
    Integer, // an integer numeral such as 42; value holds a std::int64_t (L5.6, L5.7)
    Float,   // a float numeral such as 3.5 or 1e10; value holds a double (L5.6)
    String,  // a string literal; value holds its bytes after escapes (L7)

    // Single-character tokens (L8.1)
    Plus,      // +  addition
    Minus,     // -  subtraction, or unary negation
    Star,      // *  multiplication
    Slash,     // /  float division
    Percent,   // %  modulo
    Caret,     // ^  exponentiation
    Hash,      // #  length operator
    Ampersand, // &  bitwise and
    Tilde,     // ~  bitwise xor, or unary bitwise not
    Pipe,      // |  bitwise or
    Less,      // <  less than
    Greater,   // >  greater than
    Assign,    // =  assignment
    LParen,    // (  open parenthesis: grouping, calls
    RParen,    // )  close parenthesis
    LBrace,    // {  open brace: table constructor
    RBrace,    // }  close brace
    LBracket,  // [  open bracket: indexing, table keys
    RBracket,  // ]  close bracket
    Semicolon, // ;  optional statement separator
    Colon,     // :  method call or definition, as in obj:method()
    Comma,     // ,  separates list items
    Dot,       // .  field access, as in t.x

    // Multi-character tokens (L8.1)
    Equal,        // ==   equal
    NotEqual,     // ~=   not equal
    LessEqual,    // <=   less than or equal
    GreaterEqual, // >=   greater than or equal
    ShiftLeft,    // <<   bitwise left shift
    ShiftRight,   // >>   bitwise right shift
    DoubleSlash,  // //   floor division
    DoubleColon,  // ::   goto label, as in ::top::
    Concat,       // ..   string concatenation
    Ellipsis,     // ...  varargs

    // Keywords: the 22 reserved words (L4.3)
    And,      // and       logical and
    Break,    // break     leave the innermost loop
    Do,       // do        start a block: do ... end, and loop bodies
    Else,     // else      last branch of an if
    ElseIf,   // elseif    another branch of an if
    End,      // end       close a block
    False,    // false     the boolean false
    For,      // for       numeric and generic for loops
    Function, // function  function definitions and expressions
    Goto,     // goto      jump to a ::label::
    If,       // if        conditional
    In,       // in        generic for: for k, v in pairs(t)
    Local,    // local     declare local variables and functions
    Nil,      // nil       the nil value
    Not,      // not       logical not
    Or,       // or        logical or
    Repeat,   // repeat    repeat ... until loop
    Return,   // return    return from a function
    Then,     // then      after an if or elseif condition
    True,     // true      the boolean true
    Until,    // until     end of a repeat loop
    While,    // while     while loop
};

// The enumerator's name, such as "LParen" or "While", for debug output.
std::string_view token_kind_name(TokenKind kind);

// The spelling of each keyword. A name is a keyword exactly when its text
// appears here (L4.3).
//
// It is a constexpr std::array rather than a std::map or std::vector: the 22
// entries never change, so the table is built at compile time, lives in
// read-only memory and costs nothing at startup.
struct Keyword {
    std::string_view text;
    TokenKind kind;
};

inline constexpr auto keywords = std::to_array<Keyword>({
    {.text = "and", .kind = TokenKind::And},
    {.text = "break", .kind = TokenKind::Break},
    {.text = "do", .kind = TokenKind::Do},
    {.text = "else", .kind = TokenKind::Else},
    {.text = "elseif", .kind = TokenKind::ElseIf},
    {.text = "end", .kind = TokenKind::End},
    {.text = "false", .kind = TokenKind::False},
    {.text = "for", .kind = TokenKind::For},
    {.text = "function", .kind = TokenKind::Function},
    {.text = "goto", .kind = TokenKind::Goto},
    {.text = "if", .kind = TokenKind::If},
    {.text = "in", .kind = TokenKind::In},
    {.text = "local", .kind = TokenKind::Local},
    {.text = "nil", .kind = TokenKind::Nil},
    {.text = "not", .kind = TokenKind::Not},
    {.text = "or", .kind = TokenKind::Or},
    {.text = "repeat", .kind = TokenKind::Repeat},
    {.text = "return", .kind = TokenKind::Return},
    {.text = "then", .kind = TokenKind::Then},
    {.text = "true", .kind = TokenKind::True},
    {.text = "until", .kind = TokenKind::Until},
    {.text = "while", .kind = TokenKind::While},
});
static_assert(keywords.size() == 22);

// What a token carries besides its kind:
//   std::monostate  nothing (operators, keywords, EndOfStream)
//   std::string     a name's text or a string's bytes
//   std::int64_t    an integer numeral
//   double          a float numeral
using TokenValue = std::variant<std::monostate, std::string, std::int64_t, double>;

struct Token {
    TokenKind kind = TokenKind::EndOfStream;
    int line = 1; // the line the token starts on, counted from 1 (L2.2)
    TokenValue value;

    // Where the token is in the source: source.substr(offset, length) is its
    // text exactly as written, such as "0x10" for an Integer with value 16.
    // Parser errors quote it (G9.2). 32 bits each keep the token small; the
    // lexer refuses sources of 4 GiB or more.
    std::uint32_t offset = 0;
    std::uint32_t length = 0;

    // The line the token ends on: after a long string, its closing line.
    // Lua counts a token as read on that line (lastline), which decides the
    // lines of the instructions compiled after it.
    int end_line = 1;

    // Two tokens are equal when they mean the same thing: kind, line and
    // value. Where they sit in the source does not count, so tests can write
    // the expected token without computing offsets.
    bool operator==(const Token& other) const {
        return kind == other.kind && line == other.line && value == other.value;
    }
};

// How Lua names a token kind in "X expected" messages (G9.3): keywords and
// symbols quoted ('end', '=='), and the rest in angle brackets (<eof>, <name>).
std::string expected_text(TokenKind kind);

} // namespace cpplua
