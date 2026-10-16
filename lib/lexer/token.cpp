#include "lexer/token.hpp"

#include <algorithm>
#include <format>
#include <string>

namespace lua2 {

std::string_view token_kind_name(TokenKind kind) {
    // No default case: if a TokenKind is added and not named here, the
    // compiler warns that the switch does not cover it (-Wswitch).
    switch (kind) {
    case TokenKind::EndOfStream: return "EndOfStream";
    case TokenKind::Name: return "Name";
    case TokenKind::Other: return "Other";
    case TokenKind::Integer: return "Integer";
    case TokenKind::Float: return "Float";
    case TokenKind::String: return "String";
    case TokenKind::Plus: return "Plus";
    case TokenKind::Minus: return "Minus";
    case TokenKind::Star: return "Star";
    case TokenKind::Slash: return "Slash";
    case TokenKind::Percent: return "Percent";
    case TokenKind::Caret: return "Caret";
    case TokenKind::Hash: return "Hash";
    case TokenKind::Ampersand: return "Ampersand";
    case TokenKind::Tilde: return "Tilde";
    case TokenKind::Pipe: return "Pipe";
    case TokenKind::Less: return "Less";
    case TokenKind::Greater: return "Greater";
    case TokenKind::Assign: return "Assign";
    case TokenKind::LParen: return "LParen";
    case TokenKind::RParen: return "RParen";
    case TokenKind::LBrace: return "LBrace";
    case TokenKind::RBrace: return "RBrace";
    case TokenKind::LBracket: return "LBracket";
    case TokenKind::RBracket: return "RBracket";
    case TokenKind::Semicolon: return "Semicolon";
    case TokenKind::Colon: return "Colon";
    case TokenKind::Comma: return "Comma";
    case TokenKind::Dot: return "Dot";
    case TokenKind::Equal: return "Equal";
    case TokenKind::NotEqual: return "NotEqual";
    case TokenKind::LessEqual: return "LessEqual";
    case TokenKind::GreaterEqual: return "GreaterEqual";
    case TokenKind::ShiftLeft: return "ShiftLeft";
    case TokenKind::ShiftRight: return "ShiftRight";
    case TokenKind::DoubleSlash: return "DoubleSlash";
    case TokenKind::DoubleColon: return "DoubleColon";
    case TokenKind::Concat: return "Concat";
    case TokenKind::Ellipsis: return "Ellipsis";
    case TokenKind::And: return "And";
    case TokenKind::Break: return "Break";
    case TokenKind::Do: return "Do";
    case TokenKind::Else: return "Else";
    case TokenKind::ElseIf: return "ElseIf";
    case TokenKind::End: return "End";
    case TokenKind::False: return "False";
    case TokenKind::For: return "For";
    case TokenKind::Function: return "Function";
    case TokenKind::Goto: return "Goto";
    case TokenKind::If: return "If";
    case TokenKind::In: return "In";
    case TokenKind::Local: return "Local";
    case TokenKind::Nil: return "Nil";
    case TokenKind::Not: return "Not";
    case TokenKind::Or: return "Or";
    case TokenKind::Repeat: return "Repeat";
    case TokenKind::Return: return "Return";
    case TokenKind::Then: return "Then";
    case TokenKind::True: return "True";
    case TokenKind::Until: return "Until";
    case TokenKind::While: return "While";
    }
    return "?"; // only reachable with an invalid enum value
}

namespace {

// The spelling of an operator or punctuation token, or "" for other kinds.
std::string_view symbol_spelling(TokenKind kind) {
    switch (kind) {
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::Caret: return "^";
    case TokenKind::Hash: return "#";
    case TokenKind::Ampersand: return "&";
    case TokenKind::Tilde: return "~";
    case TokenKind::Pipe: return "|";
    case TokenKind::Less: return "<";
    case TokenKind::Greater: return ">";
    case TokenKind::Assign: return "=";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::LBracket: return "[";
    case TokenKind::RBracket: return "]";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Colon: return ":";
    case TokenKind::Comma: return ",";
    case TokenKind::Dot: return ".";
    case TokenKind::Equal: return "==";
    case TokenKind::NotEqual: return "~=";
    case TokenKind::LessEqual: return "<=";
    case TokenKind::GreaterEqual: return ">=";
    case TokenKind::ShiftLeft: return "<<";
    case TokenKind::ShiftRight: return ">>";
    case TokenKind::DoubleSlash: return "//";
    case TokenKind::DoubleColon: return "::";
    case TokenKind::Concat: return "..";
    case TokenKind::Ellipsis: return "...";
    default: return {};
    }
}

} // namespace

std::string expected_text(TokenKind kind) {
    // Like Lua's luaX_token2str: tokens without a fixed spelling are named in
    // angle brackets and not quoted.
    switch (kind) {
    case TokenKind::EndOfStream: return "<eof>";
    case TokenKind::Name: return "<name>";
    case TokenKind::String: return "<string>";
    case TokenKind::Integer: return "<integer>";
    case TokenKind::Float: return "<number>";
    case TokenKind::Other: return "<other>"; // never expected by the parser
    default: break;
    }
    if (const auto* const keyword = std::ranges::find(keywords, kind, &Keyword::kind);
        keyword != keywords.end()) {
        return std::format("'{}'", keyword->text);
    }
    return std::format("'{}'", symbol_spelling(kind));
}

} // namespace lua2
