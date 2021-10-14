#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <vector>

#include "fab.h"

namespace {
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

template <typename Container, typename T, typename MkExn>
const auto &
find_or_throw(const Container &haystack, const T &needle, MkExn &&mk) {
  auto it = haystack.find(needle);

  if (it == std::end(haystack)) {
    throw mk();
  }

  return *it;
}

template <typename Range, typename Delim>
std::string
foldl(Range &&range, const Delim &d) {
  std::string s = {};

  bool first = true;
  for (const auto &e : range) {
    if (!first) {
      s += d;
    }

    s += e;
    first = false;
  }

  return s;
}
} // namespace

namespace detail {
struct LValue {
  std::string_view iden;
};

struct RValue {
  std::string_view iden;
};

bool
operator<(const LValue &lhs, const LValue &rhs) {
  return lhs.iden < rhs.iden;
}

bool
operator<(const LValue &lhs, const RValue &rhs) {
  return lhs.iden < rhs.iden;
}

bool
operator<(const RValue &lhs, const LValue &rhs) {
  return lhs.iden < rhs.iden;
}

bool
operator<(const RValue &lhs, const RValue &rhs) {
  return lhs.iden < rhs.iden;
}

using ValueType = std::variant<LValue, RValue>;

// An association is generated by the first pass of parsing. It consists of an
// lvalue or an rvalue -- packed into the ValueType variant -- that will be
// resolved some point later on in parsing.
using Association = std::tuple<std::string_view, std::vector<ValueType>>;

// Intermediate representation for Rules -- after parsing they'll need to be
// resolved by looking each `ValueType` variant up in the environment.
struct RuleIr {
  ValueType target = {};
  std::vector<ValueType> dependencies = {};
  std::vector<ValueType> action = {};
};

bool
operator<(const RuleIr &lhs, const RuleIr &rhs) {
  return lhs.target < rhs.target;
}

struct FabError final : std::runtime_error {
  template <typename T, typename U>
  struct Unexpected {
    T expected;
    U actual;
  };

  using UnexpectedCharacter = Unexpected<char, char>;
  using UnexpectedTokenType = Unexpected<TokenType, TokenType>;
  using TokenNotInExpectedSet =
      Unexpected<std::vector<Option<TokenType>>, Option<TokenType>>;

  struct UndefinedVariable {
    std::string_view var;
  };

  struct UnknownTarget {
    std::string_view target;
  };

  struct ExpectedLValue {
    std::string_view macro;
  };

  struct UnexpectedEof {};

  struct GetErrMsg {
    template <typename T>
    std::string operator()(const Unexpected<T, T> &u) {
      auto ss = std::stringstream{};
      ss << "expected: " << u.expected << "; got: " << u.actual;
      return ss.str();
    }

    template <typename T>
    std::string operator()(const Unexpected<std::vector<T>, T> &u) {
      auto ss = std::stringstream{};
      ss << "expected one of: {";

      ss << foldl(u.expected | std::views::transform([](auto e) {
                    std::stringstream ss = {};
                    ss << e;
                    return ss.str();
                  }),
                  ", ");

      ss << "}; got: " << u.actual;
      return ss.str();
    }

    std::string operator()(const UndefinedVariable &uv) {
      return "undefined variable: " + std::string(uv.var.begin(), uv.var.end());
    }

    std::string operator()(const UnknownTarget &ut) {
      return "no rule to make target `" +
             std::string(ut.target.begin(), ut.target.end()) + "'";
    }

    std::string operator()(const ExpectedLValue &e) {
      return "expected lvalue but got macro at: " +
             std::string(e.macro.begin(), e.macro.end());
    }

    std::string operator()(const UnexpectedEof &) {
      return "unexpected <EOF>";
    }
  };

  using ErrTy = std::variant<ExpectedLValue, TokenNotInExpectedSet,
                             UndefinedVariable, UnexpectedCharacter,
                             UnexpectedEof, UnexpectedTokenType, UnknownTarget>;

  explicit FabError(const ErrTy &ty)
      : std::runtime_error(std::visit(GetErrMsg{}, ty)) {
  }
};

class LexState {
  std::string_view m_buf;
  std::string_view::const_iterator m_offset = m_buf.cbegin();

public:
  LexState(std::string_view source)
      : m_buf(source) {
  }

  [[nodiscard]] char next() {
    if (eof()) {
      throw FabError(FabError::UnexpectedEof{});
    }

    char c = *m_offset;
    m_offset = std::next(m_offset);
    return c;
  }

  bool eof() const {
    return m_offset == m_buf.cend();
  }

  void eat(char expected) {
    if (expected != *m_offset) {
      throw FabError(FabError::UnexpectedCharacter{.expected = expected,
                                                   .actual = *m_offset});
    }

    assert(next() == expected);
  }

  Option<char> peek() const {
    if (eof()) {
      return {};
    }

    return *m_offset;
  }

  std::string_view extract_lexeme(auto begin, auto end) {
    return std::string_view{begin, end};
  }

  template <typename Pred>
  auto eat_until(Pred pred) {
    const auto begin = m_offset;

    while (!pred(next()))
      ;

    assert(m_offset != m_buf.begin());
    m_offset = std::prev(m_offset);

    return std::tuple{begin, m_offset};
  }
};

class ParseState {
  std::vector<Token> m_tokens = {};
  std::vector<Token>::const_iterator m_offset = m_tokens.cbegin();
  std::set<RuleIr> rules = {};
  std::vector<Association> macros = {};

  const Token &eat(TokenType expected) {
    const auto &actual = *m_offset;

    if (expected != actual.token_type) {
      throw FabError(FabError::UnexpectedTokenType{
          .expected = expected, .actual = actual.token_type});
    }

    m_offset = std::next(m_offset);
    return actual;
  }

  std::tuple<std::vector<ValueType>, std::vector<ValueType>> rule() {
    if (peek() != TokenType::LBrace) {
      eat(TokenType::Arrow);
    }

    std::vector<ValueType> dependencies = this->dependencies();
    eat(TokenType::LBrace);
    std::vector<ValueType> action = this->action();
    eat(TokenType::RBrace);
    return std::tie(dependencies, action);
  }

  TokenType peek() const {
    if (eof()) {
      throw FabError(FabError::UnexpectedEof{});
      return {};
    } else {
      return m_offset->token_type;
    }
  }

  ValueType iden_status() {
    auto peeked = peek();

    if (TokenType::Iden == peeked) {
      return RValue{eat(TokenType::Iden).lexeme.value()};
    } else if (TokenType::Macro == peeked) {
      return LValue{eat(TokenType::Macro).lexeme.value()};
    } else {
      throw FabError(FabError::TokenNotInExpectedSet{
          .expected = {{TokenType::Iden}, {TokenType::Macro}},
          .actual = peeked});
    }
  }

  ValueType target() {
    return iden_status();
  }

  std::vector<ValueType> dependencies() {
    return iden_list();
  }

  bool matches(TokenType) {
    return false;
  }

  template <typename... Tl>
  bool matches(TokenType expected, TokenType head, Tl... tail) {
    return expected == head || matches(expected, tail...);
  }

  std::vector<ValueType> action() {
    std::vector<ValueType> action;
    auto actions = iden_list();
    eat(TokenType::SemiColon);
    return actions;
  }

  std::vector<ValueType> iden_list() {
    std::vector<ValueType> idens;

    while (matches(peek(), TokenType::Iden, TokenType::Macro)) {
      idens.push_back(iden_status());
    }

    return idens;
  }

  std::vector<ValueType> assignment() {
    eat(TokenType::Eq);
    auto idens = iden_list();
    eat(TokenType::SemiColon);
    return idens;
  }

public:
  ParseState(std::vector<Token> &&tokens)
      : m_tokens(tokens) {
  }

  void stmt_list() {
    auto iden = iden_status();
    auto peeked = peek();

    if (peeked == TokenType::Eq) {
      if (std::holds_alternative<RValue>(iden)) {
        auto lhs = std::get<RValue>(iden).iden;
        auto rhs = assignment();
        macros.emplace_back(lhs, rhs);
      } else {
        throw FabError(
            FabError::ExpectedLValue{.macro = std::get<LValue>(iden).iden});
      }
    } else if (matches(peeked, TokenType::Arrow, TokenType::LBrace)) {
      auto [dependencies, action] = rule();
      rules.insert(
          {.target = iden, .dependencies = dependencies, .action = action});
    } else {
      throw FabError(
          FabError::TokenNotInExpectedSet{.expected = {{TokenType::Eq},
                                                       {TokenType::Arrow},
                                                       {TokenType::LBrace}},
                                          .actual = peeked});
    }
  }

  bool eof() const {
    assert(m_offset != m_tokens.cend());
    return m_offset->token_type == TokenType::Eof;
  }

  std::tuple<std::set<RuleIr>, std::vector<Association>> destructure() {
    auto rules = std::exchange(this->rules, {});
    auto macros = std::exchange(this->macros, {});

    return std::tie(rules, macros);
  }
};

struct Resolver {
  const std::map<std::string_view, std::string> &macros;

  std::string_view operator()(const RValue &term) const {
    return term.iden;
  }

  std::string_view operator()(const LValue &res) const {
    auto pair = find_or_throw(macros, res.iden, [&res] {
      return FabError(FabError::UndefinedVariable{.var = res.iden});
    });

    return pair.second;
  }
};

Environment
resolve(ParseState state) {
  const auto [rules, unresolved_macros] = state.destructure();
  auto macros = std::map<std::string_view, std::string>{};
  auto is_rvalue = [](Association association) {
    auto [k, vs] = association;
    return std::ranges::all_of(
        vs, [](auto v) { return std::holds_alternative<RValue>(v); });
  };

  auto into_rvalue = [](auto v) { return std::get<RValue>(v).iden; };

  for (auto [k, v] :
       unresolved_macros | std::views::filter(is_rvalue) |
           std::views::transform([&](auto association) {
             auto [k, vs] = association;
             return std::tuple{
                 k, foldl(vs | std::views::transform(into_rvalue), " ")};
           })) {
    macros.emplace(k, v);
  }

  auto second_pass = std::map<std::string_view, std::string>{};
  const auto first_pass_visitor = Resolver{macros};
  for (auto [k, v] :
       unresolved_macros | std::views::filter(std::not_fn(is_rvalue)) |
           std::views::transform([&](auto association) {
             auto [k, vs] = association;
             auto def = foldl(vs | std::views::transform([&](auto e) {
                                return std::visit(first_pass_visitor, e);
                              }),
                              " ");

             return std::tuple{k, std::move(def)};
           })) {

    second_pass.emplace(k, v);
  }

  macros.merge(second_pass);

  auto env = Environment{std::move(macros)};
  const auto final_pass_visitor = Resolver{env.macros()};
  auto visitc = [&](auto arg) { return std::visit(final_pass_visitor, arg); };

  for (const auto &rule : rules) {
    auto target = visitc(rule.target);
    auto dependencies_view = rule.dependencies | std::views::transform(visitc);
    auto dependencies = std::vector<std::string_view>{dependencies_view.begin(),
                                                      dependencies_view.end()};
    env.insert(
        {.target = target,
         .dependencies = dependencies,
         .action = foldl(rule.action | std::views::transform(visitc), " ")});
  }

  return env;
}
} // namespace detail

std::vector<Token>
lex(std::string_view source) {
  detail::LexState state{source};
  auto tokens = std::vector<Token>{};

  while (!state.eof()) {
    switch (state.next()) {
    case '\t':
      [[fallthrough]];
    case '\n':
      [[fallthrough]];
    case ' ':
      break;
    case '/':
      state.eat('/');
      state.eat_until([](char c) { return '\n' == c; });
      state.eat('\n');
      break;
    case ':':
      state.eat('=');
      tokens.push_back({TokenType::Eq, {}});
      break;
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
    case '$': {
      state.eat('(');
      auto [begin, end] = state.eat_until([](char c) { return c == ')'; });
      tokens.emplace_back(TokenType::Macro, state.extract_lexeme(begin, end));
      state.eat(')');
      break;
    }
    default: {
      auto [begin, end] = state.eat_until(
          [](char c) { return c == ' ' || c == '\n' || c == ';'; });

      tokens.emplace_back(TokenType::Iden,
                          state.extract_lexeme(std::prev(begin), end));
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

Environment::Environment(std::map<std::string_view, std::string> macros)
    : m_macros(std::move(macros)) {
}

void
Environment::insert(Rule &&rule) {
  m_rules.emplace(std::move(rule));
}

bool
Environment::is_terminal(std::string_view rule) const {
  return !m_rules.contains(rule);
}

const Rule &
Environment::get(std::string_view target) const {
  return find_or_throw(m_rules, target, [&target] {
    return detail::FabError(detail::FabError::UnknownTarget{.target = target});
  });
}
