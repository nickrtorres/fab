#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <ranges>

#include "fab.h"

namespace detail {
LexError::LexError(const char *m) : std::runtime_error(m), msg(m) {}

const char *LexError::what() noexcept { return msg; }

LexState::LexState(std::string_view source) : m_buf(source) {}

Option<char> LexState::next() {
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

bool LexState::eof() const { return this->m_eof; }

void LexState::eat(char expected) {
  if (expected != m_buf[m_offset]) {
    throw LexError("bad token");
  }

  assert(next().has_value());
}

Option<char> LexState::peek() const {
  if (m_eof) {
    return {};
  } else {
    return m_buf[m_offset];
  }
}

std::string_view LexState::extract_lexeme(std::size_t begin, std::size_t end) {
  return std::string_view{m_buf.begin() + begin, m_buf.begin() + end};
}

std::tuple<std::size_t, std::size_t>
LexState::eat_until(std::function<bool(char)> pred) {
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

ParseState::ParseState(std::vector<Token> &&tokens) : m_tokens(tokens) {}

Environment ParseState::env() && { return std::exchange(m_env, {}); }

bool ParseState::eof() {
  assert(m_offset != m_tokens.cend());
  return m_offset->token_type == TokenType::Eof;
}

const Token &ParseState::expect(TokenType tt) {
  const auto &token = *m_offset;

  if (tt != token.token_type) {
    throw std::runtime_error("FIXME");
  }

  m_offset = std::next(m_offset);
  return token;
}

void ParseState::rule() {
  std::string_view target = this->target();
  expect(TokenType::Arrow);
  std::string_view dependency = this->dependency();
  expect(TokenType::LBrace);
  std::string action = this->action();
  expect(TokenType::RBrace);

  m_env.insert({.target = target, .action = action, .dependency = dependency});
}

std::string_view ParseState::target() {
  return expect(TokenType::Iden).lexeme.value();
};

std::string_view ParseState::dependency() {
  return expect(TokenType::Iden).lexeme.value();
}

std::string ParseState::action() {
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

void ParseState::iden_list() {}
} // namespace detail

std::vector<Token> lex(std::string_view source) {
  detail::LexState state{source};
  auto tokens = std::vector<Token>{};

  while (!state.eof()) {
    auto c = state.next();
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
      state.eat('-');
      tokens.push_back({TokenType::Arrow, {}});
      break;
    case '\t':
    case '\n':
    case ' ':
      break;
    default:
      auto [begin, end] = state.eat_until(
          [](char c) { return c == ' ' || c == '\n' || c == ';'; });
      tokens.emplace_back(TokenType::Iden, state.extract_lexeme(begin, end));
      break;
    }
  }

  tokens.push_back({TokenType::Eof, {}});
  return tokens;
}

Environment parse(std::vector<Token> &&tokens) {
  auto state = detail::ParseState{std::move(tokens)};
  while (!state.eof()) {
    state.rule();
  }

  return std::move(state).env();
}

void Environment::insert(Rule &&rule) { rules.emplace(std::move(rule)); }

bool Environment::is_terminal(std::string_view rule) const {
  return !rules.contains(rule);
}

const Rule &Environment::get(std::string_view name) const {
  auto it = rules.find(name);

  if (it == rules.cend()) {
    auto n = std::string(name.begin(), name.end());
    const std::string m = "No such rule => " + n + "!";
    throw std::runtime_error(m);
  } else {
    return *it;
  }
}
