#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <ranges>

#include "fab.h"

[[noreturn]] void fatal(std::string_view msg, std::source_location loc) {
  std::cerr << loc.file_name() << ':' << loc.function_name() << ':'
            << loc.line() << ' ' << msg << std::endl;
  std::abort();
}

LexError::LexError(const char *m) : std::runtime_error(m), msg(m) {}

const char *LexError::what() noexcept { return msg; }

Lexer::Lexer(std::string_view source) : m_buf(source) {}

Option<char> Lexer::next() {
  if (m_eof) {
    return {};
  }

  if (m_offset == m_buf.size()) {
    m_eof = true;
    return {};
  }

  const auto old = m_offset;
  m_offset += 1;
  return m_buf[old];
}

void Lexer::eat(char expected) {
  if (expected != m_buf[m_offset]) {
    throw LexError("bad token");
  }

  assert(next().has_value());
}

Option<char> Lexer::peek() const {
  if (m_eof) {
    return {};
  } else {
    return m_buf[m_offset];
  }
}

std::tuple<std::size_t, std::size_t>
Lexer::eat_until(std::function<bool(char)> pred) {
  assert(m_offset != 0);
  std::size_t begin = m_offset - 1;
  std::size_t end = m_buf.size();

  bool done = false;
  while (!done) {
    auto n = peek();
    if (!n) {
      done = true;
    } else if (pred(n.value())) {
      end = m_offset;
      done = true;
    } else {
      [[maybe_unused]] auto dontcare = next();
    }
  }

  return {begin, end};
}

std::vector<Token> Lexer::lex() && {
  auto tokens = std::vector<Token>{};

  while (!m_eof) {
    auto c = next();
    if (!c) {
      break;
    }

    switch (c.value()) {
    case ';':
      tokens.push_back({TokenType::SemiColon, {}});
      break;
    case '{':
      tokens.push_back({TokenType::LBrace, {}});
      break;
    case '}':
      tokens.push_back({TokenType::RBrace, {}});
      break;
    case '<':
      this->eat('-');
      tokens.push_back({TokenType::Arrow, {}});
      break;
    case '\t':
    case '\n':
    case ' ':
      break;
    default:
      auto [begin, end] =
          eat_until([](char c) { return c == ' ' || c == '\n' || c == ';'; });
      tokens.push_back(
          {TokenType::Iden,
           std::string_view{m_buf.begin() + begin, m_buf.begin() + end}});
      break;
    }
  }

  tokens.push_back({TokenType::Eof, {}});
  return tokens;
}

std::size_t RuleHash::operator()(const Rule &r) const noexcept {
  return std::hash<std::string_view>{}(r.target);
}

void Environment::insert(Rule &&rule) { m_rules.emplace(std::move(rule)); }

const Rule &Environment::default_rule() { return *(m_rules.cbegin()); }

// main <- main.o {
//   c++ main.o -o main
// }
//
// <rule>      ::= <target> arrow <dep> lbrace <action> rbrace
// <rule>      ::= <target> lbrace <action> rbrace
// <target>    ::= iden
// <dep>       ::= iden
// <action>    ::= iden_list
// <iden_list> ::= iden semicolon
// <iden_list> ::= iden space <iden_list>
//
Parser::Parser(std::vector<Token> tokens) : m_tokens(tokens) {}

Environment Parser::parse() && {
  while (!eof()) {
    rule();
  }

  return m_env;
}

bool Parser::eof() {
  assert(m_offset != m_tokens.cend());
  return m_offset->token_type == TokenType::Eof;
}

const Token &Parser::expect(TokenType tt) {
  const auto &token = *m_offset;

  if (tt != token.token_type) {
    throw std::runtime_error("FIXME");
  }

  m_offset = std::next(m_offset);
  return token;
}

void Parser::rule() {
  std::string_view target = this->target();
  expect(TokenType::Arrow);
  std::string_view dependency = this->dependency();
  expect(TokenType::LBrace);
  std::string action = this->action();
  expect(TokenType::RBrace);

  m_env.insert({.target = target, .action = action, .dependency = dependency});
}

std::string_view Parser::target() {
  return expect(TokenType::Iden).lexeme.value();
};

std::string_view Parser::dependency() {
  return expect(TokenType::Iden).lexeme.value();
}

std::string Parser::action() {
  // Pretty much foldl, but that's not until C++23:
  // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2214r0.html#stdaccumulate-rangesfold
  std::string action;
  for (auto w :
       m_tokens | std::views::drop(m_offset - m_tokens.begin()) |
           std::views::take_while(
               [](const Token &t) { return t.token_type == TokenType::Iden; }) |
           std::views::transform([](const Token &t) -> std::string_view {
             return t.lexeme.value();
           })) {
    expect(TokenType::Iden);
    action += w;
    action.push_back(' ');
  }

  action.pop_back();

  expect(TokenType::SemiColon);
  return action;
}

void Parser::iden_list() {}
