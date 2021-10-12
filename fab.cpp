#include <array>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <ranges>
#include <sstream>
#include <variant>
#include <vector>

#include "fab.h"

std::ostream &
operator<<(std::ostream &os, const TokenType &t) {
  switch (t) {
  case TokenType::Arrow:
    return os << "ARROW";
  case TokenType::SemiColon:
    return os << "SEMICOLON";
  case TokenType::LBrace:
    return os << "LBRACE";
  case TokenType::RBrace:
    return os << "RBRACE";
  case TokenType::Iden:
    return os << "IDEN";
  case TokenType::Eof:
    return os << "EOF";
  case TokenType::Eq:
    return os << "EQ";
  default:
    std::cerr << "FATAL: unhandled TokenType" << std::endl;
    std::abort();
  }
}

std::ostream &
operator<<(std::ostream &os, const Option<TokenType> &t) {
  if (t) {
    return os << t.value();
  } else {
    return os << "<NONE>";
  }
}

template <typename Container, typename T, typename GenExn>
const auto &
find_or_throw(const Container &haystack, const T &needle, GenExn &&gen) {
  auto it = haystack.find(needle);

  if (it == std::end(haystack)) {
    throw gen();
  } else {
    return *it;
  }
}

namespace detail {
struct NeedsResolution {
  std::string_view iden;
};

struct Terminal {
  std::string_view iden;
};

bool
operator<(const NeedsResolution &lhs, const NeedsResolution &rhs) {
  return lhs.iden < rhs.iden;
}

bool
operator<(const NeedsResolution &lhs, const Terminal &rhs) {
  return lhs.iden < rhs.iden;
}

bool
operator<(const Terminal &lhs, const NeedsResolution &rhs) {
  return lhs.iden < rhs.iden;
}

bool
operator<(const Terminal &lhs, const Terminal &rhs) {
  return lhs.iden < rhs.iden;
}

using ResolutionStatus = std::variant<NeedsResolution, Terminal>;

// Intermediate representation for Rules -- after parsing they'll need to be
// resolved by looking each `NeedsResolution` variant up in the environment.
struct RuleIr {
  ResolutionStatus target;
  ResolutionStatus dependency;
  std::vector<ResolutionStatus> action;
};

bool
operator<(const RuleIr &lhs, const RuleIr &rhs) {
  return lhs.target < rhs.target;
}

struct FabError final : std::runtime_error {
  template <typename T>
  struct Unexpected {
    T expected;
    T actual;
  };

  using UnexpectedCharacter = Unexpected<char>;
  using UnexpectedTokenType = Unexpected<TokenType>;

  struct TokenNotInExpectedSet {
    std::vector<Option<TokenType>> expected;
    Option<TokenType> actual;
  };

  struct UndefinedVariable {
    std::string_view var;
  };

  struct UnknownTarget {
    std::string_view target;
  };

  struct GetErrMsg {
    template <typename T>
    std::string operator()(const Unexpected<T> &u) {
      std::stringstream ss;
      ss << "expected: " << u.expected << "; got: " << u.actual;
      return ss.str();
    }

    std::string operator()(const TokenNotInExpectedSet &u) {
      std::stringstream ss;
      ss << "expected one of: {";

      // SO to the rescue: https://stackoverflow.com/a/27585064
      bool first = true;
      for (auto tt : u.expected) {
        if (!first) {
          ss << ", ";
        }

        ss << tt;
        first = false;
      }

      ss << "}; got: " << u.actual;
      return ss.str();
    }

    std::string operator()(const UndefinedVariable &uv) {
      return "undefined variable: " + std::string(uv.var.begin(), uv.var.end());
    }

    std::string operator()(const UnknownTarget &ut) {
      return "No rule to make target `" +
             std::string(ut.target.begin(), ut.target.end()) + "'";
    }
  };

  using ErrTy =
      std::variant<UnexpectedCharacter, UnexpectedTokenType, UndefinedVariable,
                   TokenNotInExpectedSet, UnknownTarget>;

  FabError(const ErrTy &ty)
      : std::runtime_error(std::visit(GetErrMsg{}, ty)) {
  }
};

class LexState {
  std::string_view m_buf;
  std::size_t m_offset = 0;

public:
  LexState(std::string_view source)
      : m_buf(source) {
  }

  Option<char> next() {
    if (eof()) {
      return {};
    }

    const auto old = m_offset;
    m_offset += 1;
    return m_buf[old];
  }

  bool eof() const {
    return m_offset == m_buf.size();
  }

  void eat(char expected) {
    if (expected != m_buf[m_offset]) {
      throw FabError(FabError::UnexpectedCharacter{.expected = expected,
                                                   .actual = m_buf[m_offset]});
    }

    assert(next().has_value());
  }

  Option<char> peek() const {
    if (eof()) {
      return {};
    } else {
      return m_buf[m_offset];
    }
  }

  std::string_view extract_lexeme(std::size_t begin, std::size_t end) {
    return std::string_view{m_buf.begin() + begin, m_buf.begin() + end};
  }

  std::tuple<std::size_t, std::size_t>
  eat_until(std::function<bool(char)> pred) {
    std::size_t begin = m_offset;
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
};

class ParseState {
  std::vector<Token> m_tokens;
  std::vector<Token>::const_iterator m_offset = m_tokens.cbegin();
  std::set<RuleIr> rules;
  std::map<std::string_view, std::string_view> macros;

  const Token &eat(TokenType expected) {
    const auto &actual = *m_offset;

    if (expected != actual.token_type) {
      throw FabError(FabError::UnexpectedTokenType{
          .expected = expected, .actual = actual.token_type});
    }

    m_offset = std::next(m_offset);
    return actual;
  }

  std::tuple<ResolutionStatus, std::vector<ResolutionStatus>> rule() {
    eat(TokenType::Arrow);
    ResolutionStatus dependency = this->dependency();
    eat(TokenType::LBrace);
    std::vector<ResolutionStatus> action = this->action();
    eat(TokenType::RBrace);
    return std::tie(dependency, action);
  }

  std::optional<TokenType> peek() const {
    if (eof()) {
      return {};
    } else {
      return m_offset->token_type;
    }
  }

  ResolutionStatus iden_status() {
    auto peeked = peek();

    if (TokenType::Iden == peeked) {
      return Terminal{eat(TokenType::Iden).lexeme.value()};
    } else if (TokenType::Macro == peeked) {
      return NeedsResolution{eat(TokenType::Macro).lexeme.value()};
    } else {
      throw FabError(FabError::TokenNotInExpectedSet{
          .expected = {{TokenType::Iden}, {TokenType::Macro}},
          .actual = peeked});
    }
  }

  ResolutionStatus target() {
    return iden_status();
  }

  ResolutionStatus dependency() {
    return iden_status();
  }

  bool matches(TokenType) {
    return false;
  }

  template <typename... Tl>
  bool matches(TokenType expected, TokenType head, Tl... tail) {
    return expected == head || matches(expected, tail...);
  }

  std::vector<ResolutionStatus> action() {
    std::vector<ResolutionStatus> action;

    auto peeked = peek();
    while (peeked.has_value() &&
           matches(peeked.value(), TokenType::Iden, TokenType::Macro)) {
      action.push_back(iden_status());
      peeked = peek();
    }

    eat(TokenType::SemiColon);
    return action;
  }

  std::string iden_list() {
    return {};
  }

  std::string_view assignment() {
    eat(TokenType::Eq);
    auto lexeme = eat(TokenType::Iden).lexeme.value();
    eat(TokenType::SemiColon);
    return lexeme;
  }

public:
  ParseState(std::vector<Token> &&tokens)
      : m_tokens(tokens) {
  }

  void stmt_list() {
    auto iden = iden_status();
    auto peeked = peek();

    if (peeked == TokenType::Eq) {
      auto lhs = std::get<Terminal>(iden).iden;
      auto rhs = assignment();
      macros.emplace(lhs, rhs);
    } else if (peeked == TokenType::Arrow) {
      auto [dependency, action] = rule();
      rules.insert(
          {.target = iden, .dependency = dependency, .action = action});
    } else {
      throw FabError(FabError::TokenNotInExpectedSet{
          .expected = {{TokenType::Eq}, {TokenType::Arrow}}, .actual = peeked});
    }
  }

  bool eof() const {
    assert(m_offset != m_tokens.cend());
    return m_offset->token_type == TokenType::Eof;
  }

  std::tuple<std::set<RuleIr>, std::map<std::string_view, std::string_view>>
  destructure() {
    auto rules = std::exchange(this->rules, {});
    auto macros = std::exchange(this->macros, {});

    return std::tie(rules, macros);
  }
};

struct Resolver {
  const std::map<std::string_view, std::string_view> macros;

  std::string_view operator()(const Terminal &term) const {
    return term.iden;
  }

  std::string_view operator()(const NeedsResolution &res) const {
    auto pair = find_or_throw(macros, res.iden, [&res] {
      return FabError(FabError::UndefinedVariable{.var = res.iden});
    });

    return pair.second;
  }
};

Environment
resolve(ParseState state) {
  auto env = Environment{};
  auto [rules, macros] = state.destructure();
  const auto visitor = Resolver{std::move(macros)};
  auto visitc = [&visitor](auto arg) { return std::visit(visitor, arg); };

  for (const auto &rule : rules) {
    auto target = visitc(rule.target);
    auto dependency = visitc(rule.dependency);

    std::string action;
    for (auto iden : rule.action | std::views::transform(visitc)) {
      action += iden;
      action.push_back(' ');
    }

    action.pop_back();

    env.insert({.target = target, .dependency = dependency, .action = action});
  }

  return env;
}
} // namespace detail

std::vector<Token>
lex(std::string_view source) {
  detail::LexState state{source};
  auto tokens = std::vector<Token>{};

  while (!state.eof()) {
    auto current = state.peek();
    if (!current) {
      break;
    }

    switch (current.value()) {
    case '\t':
      [[fallthrough]];
    case '\n':
      [[fallthrough]];
    case ' ':
      state.next();
      break;
    case ':':
      state.next();
      state.eat('=');
      tokens.push_back({TokenType::Eq, {}});
      break;
    case ';':
      state.next();
      tokens.push_back({TokenType::SemiColon, {}});
      break;
    case '{':
      state.next();
      tokens.push_back({TokenType::LBrace, {}});
      break;
    case '}':
      state.next();
      tokens.push_back({TokenType::RBrace, {}});
      break;
    case '<':
      state.next();
      state.eat('-');
      tokens.push_back({TokenType::Arrow, {}});
      break;
    case '$': {
      state.next();
      state.eat('(');
      auto [begin, end] = state.eat_until([](char c) { return c == ')'; });
      tokens.emplace_back(TokenType::Macro, state.extract_lexeme(begin, end));
      state.eat(')');
      break;
    }
    default: {
      auto [begin, end] = state.eat_until(
          [](char c) { return c == ' ' || c == '\n' || c == ';'; });
      tokens.emplace_back(TokenType::Iden, state.extract_lexeme(begin, end));
      break;
    }
    }
  }

  tokens.push_back({TokenType::Eof, {}});
  return tokens;
}

Environment
parse(std::vector<Token> &&tokens) {
  auto state = detail::ParseState{std::move(tokens)};
  while (!state.eof()) {
    state.stmt_list();
  }

  return detail::resolve(std::move(state));
}

void
Environment::insert(Rule &&rule) {
  rules.emplace(std::move(rule));
}

bool
Environment::is_terminal(std::string_view rule) const {
  return !rules.contains(rule);
}

const Rule &
Environment::get(std::string_view target) const {
  return find_or_throw(rules, target, [&target] {
    return detail::FabError(detail::FabError::UnknownTarget{.target = target});
  });
}
