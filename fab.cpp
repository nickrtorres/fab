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
std::string
sv_to_string(std::string_view sv) {
  return std::string{sv.cbegin(), sv.cend()};
}

std::ostream &
operator<<(std::ostream &os, const TokenType &t) {
  switch (t) {
  case TokenType::Arrow:
    return os << "<-";
  case TokenType::SemiColon:
    return os << ";";
  case TokenType::LBrace:
    return os << "{";
  case TokenType::RBrace:
    return os << "}";
  case TokenType::Iden:
    return os << "IDEN";
  case TokenType::Eof:
    return os << "EOF";
  case TokenType::Eq:
    return os << "=";
  case TokenType::TargetAlias:
    return os << "$@";
  case TokenType::PrereqAlias:
    return os << "$<";
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
  const std::string_view iden;
};

struct RValue {
  const std::string_view iden;
};

struct TargetAlias {};

struct PrereqAlias {};

using ValueType = std::variant<LValue, RValue, TargetAlias, PrereqAlias>;

// An association is generated by the first pass of parsing. It consists of an
// lvalue or an rvalue -- packed into the ValueType variant -- that will be
// resolved some point later on in parsing.
using Association = std::tuple<std::string_view, std::vector<ValueType>>;

// Intermediate representation for Rules -- after parsing they'll need to be
// resolved by looking each `ValueType` variant up in the environment.
struct RuleIr {
  const ValueType target;
  const std::vector<ValueType> dependencies;
  const std::vector<ValueType> action;
};

struct FabError final : std::runtime_error {
  template <typename T, typename U>
  struct Unexpected {
    const T expected;
    const U actual;
  };

  using UnexpectedCharacter = Unexpected<char, char>;
  using UnexpectedTokenType = Unexpected<TokenType, TokenType>;
  using TokenNotInExpectedSet =
      Unexpected<std::vector<Option<TokenType>>, Option<TokenType>>;

  struct UndefinedVariable {
    const std::string_view var;
  };

  struct UnknownTarget {
    const std::string_view target;
  };

  struct ExpectedLValue {
    const std::string_view macro;
  };

  struct UnexpectedEof {};

  struct BuiltInMacrosRequireActionScope {};

  struct GetErrMsg {
    template <typename T>
    std::string operator()(const Unexpected<T, T> &u) const {
      auto ss = std::stringstream{};
      ss << "expected: " << u.expected << "; got: " << u.actual;
      return ss.str();
    }

    template <typename T>
    std::string operator()(const Unexpected<std::vector<T>, T> &u) const {
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

    std::string operator()(const UndefinedVariable &uv) const {
      return "undefined variable: " + std::string(uv.var.begin(), uv.var.end());
    }

    std::string operator()(const UnknownTarget &ut) const {
      return "no rule to make target `" +
             std::string(ut.target.begin(), ut.target.end()) + "'";
    }

    std::string operator()(const ExpectedLValue &e) const {
      return "expected lvalue but got macro at: " +
             std::string(e.macro.begin(), e.macro.end());
    }

    std::string operator()(const UnexpectedEof &) const {
      return "unexpected <EOF>";
    }

    std::string operator()(const BuiltInMacrosRequireActionScope &) const {
      return "built in macros are only valid in action blocks.";
    }
  };

  using ErrTy = std::variant<BuiltInMacrosRequireActionScope, ExpectedLValue,
                             TokenNotInExpectedSet, UndefinedVariable,
                             UnexpectedCharacter, UnexpectedEof,
                             UnexpectedTokenType, UnknownTarget>;

  explicit FabError(const ErrTy &ty)
      : std::runtime_error(std::visit(GetErrMsg{}, ty)) {
  }
};

class LexState {
  const std::string_view m_buf;
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
  const std::vector<Token> m_tokens;
  std::vector<Token>::const_iterator m_offset = m_tokens.cbegin();
  std::vector<RuleIr> rules = {};
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

    if (peek() == TokenType::SemiColon) {
      eat(TokenType::SemiColon);
      return std::make_tuple(std::move(dependencies), std::vector<ValueType>{});
    }

    eat(TokenType::LBrace);
    std::vector<ValueType> action = this->action();
    eat(TokenType::RBrace);
    return std::tuple{std::move(dependencies), std::move(action)};
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
    } else if (TokenType::TargetAlias == peeked) {
      eat(TokenType::TargetAlias);
      return TargetAlias{};
    } else if (TokenType::PrereqAlias == peeked) {
      eat(TokenType::PrereqAlias);
      return PrereqAlias{};
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

  static bool matches(TokenType) {
    return false;
  }

  template <typename... Tl>
  static bool matches(TokenType expected, TokenType head, Tl... tail) {
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

    while (ParseState::matches(peek(), TokenType::Iden, TokenType::Macro,
                               TokenType::TargetAlias,
                               TokenType::PrereqAlias)) {
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
    } else if (ParseState::matches(peeked, TokenType::Arrow,
                                   TokenType::LBrace)) {
      auto [dependencies, action] = rule();
      rules.push_back(
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

  std::tuple<std::vector<RuleIr>, std::vector<Association>> destructure() {
    auto rules = std::exchange(this->rules, {});
    auto macros = std::exchange(this->macros, {});

    return std::tie(rules, macros);
  }
};
template <typename T>
concept ActionScope =
    std::is_same<TargetAlias, T>::value || std::is_same<PrereqAlias, T>::value;

template <typename T>
concept FileScope =
    std::is_same<RValue, T>::value || std::is_same<LValue, T>::value;

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

  template <typename T>
  std::string_view operator()(const T &) const requires ActionScope<T> {
    throw FabError(FabError::BuiltInMacrosRequireActionScope{});
  }
};

struct ActionResolver {
  const std::string_view target;
  const std::vector<std::string_view> dependencies;
  const std::map<std::string_view, std::string> &macros;

  std::string operator()(const TargetAlias &) const {
    return sv_to_string(target);
  }

  std::string operator()(const PrereqAlias &) const {
    return foldl(dependencies, " ");
  }

  template <typename T>
  std::string operator()(const T &variant) const requires FileScope<T> {
    return sv_to_string(Resolver{macros}(variant));
  }
};

namespace resolve {
namespace detail {
std::map<std::string_view, std::string>
resolve_associations(const std::vector<Association> &associations) {
  const auto base_resolver = [](const auto &association, auto &&transformer) {
    auto [k, vs] = association;
    return std::make_pair(k,
                          foldl(vs | std::views::transform(transformer), " "));
  };

  const auto is_rvalue = [](const Association &association) {
    auto [k, vs] = association;
    return std::ranges::all_of(
        vs, [](const auto &v) { return std::holds_alternative<RValue>(v); });
  };

  const auto resolve_as_rvalue = [base_resolver](const auto &association) {
    const auto into_rvalue = [](const auto &v) {
      return std::get<RValue>(v).iden;
    };

    return base_resolver(association, into_rvalue);
  };

  auto resolved = std::map<std::string_view, std::string>{};
  std::ranges::move(
      std::views::transform(associations | std::views::filter(is_rvalue),
                            resolve_as_rvalue),
      std::inserter(resolved, resolved.begin()));

  const auto resolve_with_visitor = [&macros = std::as_const(resolved),
                                     base_resolver](const auto &association) {
    const auto visitc = [&](const auto &variant) {
      auto visitor = Resolver{macros};
      return std::visit(visitor, variant);
    };

    return base_resolver(association, visitc);
  };

  std::ranges::move(
      std::views::transform(
          std::views::filter(associations, std::not_fn(is_rvalue)),
          resolve_with_visitor),
      std::inserter(resolved, resolved.begin()));

  return resolved;
}

std::vector<Rule>
resolve_irs(const std::map<std::string_view, std::string> &macros,
            const std::vector<RuleIr> &irs) {

  const auto resolve_ir = [macros](const auto &ir) {
    const auto visitc = [&](auto arg) {
      const auto visitor = Resolver{macros};
      return std::visit(visitor, arg);
    };

    const auto target = visitc(ir.target);
    auto dependencies = std::vector<std::string_view>{};
    std::ranges::copy(std::views::transform(ir.dependencies, visitc),
                      std::back_inserter(dependencies));

    auto action_visitc = [&, &dependencies =
                                 std::as_const(dependencies)](auto arg) {
      const auto visitor = ActionResolver{
          .target = target, .dependencies = dependencies, .macros = macros};
      return std::visit(visitor, arg);
    };

    auto action = foldl(std::views::transform(ir.action, action_visitc), " ");

    return Rule{.target = target,
                .dependencies = std::move(dependencies),
                .action = std::move(action)};
  };

  auto resolved = std::vector<Rule>{};
  std::ranges::move(std::views::transform(irs, resolve_ir),
                    std::back_inserter(resolved));

  return resolved;
}

template <typename T>
concept Moveable = std::is_move_constructible<T>::value;

template <typename T>
std::set<T, std::less<>>
into_set(std::vector<T> vs) requires Moveable<T> {
  return std::set<T, std::less<>>{
      std::make_move_iterator(vs.begin()),
      std::make_move_iterator(vs.end()),
  };
}
} // namespace detail

Environment
parse_state(ParseState state) {
  const auto [irs, associations] = state.destructure();
  /* const */ auto macros = detail::resolve_associations(associations);
  /* const */ auto rules = detail::resolve_irs(macros, irs);
  const std::string_view head = rules.front().target;

  return Environment{.macros = std::move(macros),
                     .rules = detail::into_set(std::move(rules)),
                     .head = head};
}
} // namespace resolve
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
    case '#':
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
      if (state.peek() == '@') {
        state.eat('@');
        tokens.push_back({TokenType::TargetAlias, {}});
        break;
      }

      if (state.peek() == '<') {
        state.eat('<');
        tokens.push_back({TokenType::PrereqAlias, {}});
        break;
      }

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

  return detail::resolve::parse_state(std::move(state));
}

bool
Environment::is_leaf(std::string_view rule) const {
  return !rules.contains(rule);
}

const Rule &
Environment::get(std::string_view target) const {
  return find_or_throw(rules, target, [&target] {
    return detail::FabError(detail::FabError::UnknownTarget{.target = target});
  });
}
