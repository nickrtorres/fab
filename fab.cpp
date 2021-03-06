#include <algorithm>
#include <cassert>
#include <cctype>
#include <concepts>
#include <cstdlib>
#include <functional>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "fab.h"

namespace {
template <typename T>
[[nodiscard]] constexpr bool
same_as_v() {
  return false;
}

template <typename T, typename Head, typename... Tail>
[[nodiscard]] constexpr bool
same_as_v() {
  return std::is_same<T, Head>::value || same_as_v<T, Tail...>();
}

template <typename T, typename Head, typename... Tail>
concept SameAs = same_as_v<T, Head, Tail...>();

template <typename T>
[[nodiscard]] bool
matches(T) {
  return false;
}

template <typename Hd, typename... Tl>
[[nodiscard]] bool
matches(Hd candidate, Hd head, Tl... tail) {
  return candidate == head || matches(candidate, tail...);
}

[[nodiscard]] std::string
sv_to_string(std::string_view sv) {
  return std::string{sv.cbegin(), sv.cend()};
}

template <typename Container, typename T, typename MkExn>
[[nodiscard]] const auto &
find_or_throw(const Container &haystack, const T &needle,
              MkExn &&mk) requires std::invocable<MkExn> {
  const auto it = haystack.find(needle);

  if (std::end(haystack) == it) {
    throw mk();
  }

  return *it;
}

template <typename S>
concept Concat = requires(S s) {
  {std::string{}.append(s)};
};

template <typename R, typename D, typename Transform = std::identity>
requires std::ranges::range<R> &&
    std::invocable<Transform, std::ranges::range_value_t<R>> &&
    Concat<std::invoke_result_t<Transform, std::ranges::range_value_t<R>>>
[[nodiscard]] std::string
foldl(R &&range, const D &delim, Transform transform = {}) {
  auto s = std::string{};
  auto first = bool{true};

  for (const auto &e : range) {
    if (!first) {
      s += delim;
    }

    s += std::invoke(transform, e);
    first = false;
  }

  return s;
}
} // namespace

namespace detail {
struct [[nodiscard]] FabError final : std::runtime_error {
  template <typename T, typename U>
  struct [[nodiscard]] Unexpected {
    const T expected;
    const U actual;
  };

  using UnexpectedCharacter = Unexpected<char, char>;
  using UnexpectedTokenType = Unexpected<Token::Ty, Token::Ty>;
  using TokenNotInExpectedSet =
      Unexpected<std::vector<Option<Token::Ty>>, Option<Token::Ty>>;
  using UnexpectedFill = Unexpected<std::string_view, std::string_view>;

  struct [[nodiscard]] UndefinedVariable {
    const std::string_view var;
  };

  struct [[nodiscard]] UnknownTarget {
    const std::string_view target;
  };

  struct [[nodiscard]] ExpectedLValue {
    const std::string_view macro;
  };

  struct [[nodiscard]] UnexpectedEof {};

  struct [[nodiscard]] BuiltInMacrosRequireActionScope {};

  struct [[nodiscard]] NoRulesToRun {};

  struct [[nodiscard]] UndefinedGenericRule {
    const std::string_view target;
    const std::string_view prereq;
  };

  struct [[nodiscard]] GetErrMsg {
    template <typename T>
    [[nodiscard]] std::string operator()(const Unexpected<T, T> &u) const {
      auto ss = std::stringstream{};
      ss << "expected: " << u.expected << "; got: " << u.actual;
      return ss.str();
    }

    template <typename T>
    [[nodiscard]] std::string
    operator()(const Unexpected<std::vector<T>, T> &u) const {
      auto ss = std::stringstream{};
      ss << "expected one of: {";

      ss << foldl(u.expected, " ", [](const T &t) {
        auto ss = std::stringstream{};
        ss << t;
        return ss.str();
      });

      ss << "}; got: " << u.actual;
      return ss.str();
    }

    [[nodiscard]] std::string operator()(const UndefinedVariable &uv) const {
      return "undefined variable: " + sv_to_string(uv.var);
    }

    [[nodiscard]] std::string operator()(const UnknownTarget &ut) const {
      return "no rule to make target `" + sv_to_string(ut.target) + "'";
    }

    std::string operator()(const ExpectedLValue &e) const {
      return "expected lvalue but got macro at: " + sv_to_string(e.macro);
    }

    [[nodiscard]] std::string operator()(const UnexpectedEof &) const {
      return "unexpected <EOF>";
    }

    [[nodiscard]] std::string
    operator()(const BuiltInMacrosRequireActionScope &) const {
      return "built in macros are only valid in action blocks.";
    }

    std::string operator()(const NoRulesToRun &) const {
      return "no rules to run.";
    }

    [[nodiscard]] std::string operator()(const UndefinedGenericRule &g) {
      return "undefined generic rule: {target = " + sv_to_string(g.target) +
             ", prereq = " + sv_to_string(g.prereq) + "}.";
    }
  };

  using ErrTy =
      std::variant<BuiltInMacrosRequireActionScope, ExpectedLValue,
                   NoRulesToRun, TokenNotInExpectedSet, UndefinedGenericRule,
                   UndefinedVariable, UnexpectedCharacter, UnexpectedEof,
                   UnexpectedFill, UnexpectedTokenType, UnknownTarget>;

  explicit FabError(const ErrTy &ty)
      : std::runtime_error(std::visit(GetErrMsg{}, ty)) {
  }
};

template <typename R>
[[nodiscard]] auto
move_collect(R &&range) requires std::ranges::range<R> {
  std::vector<std::ranges::range_value_t<R>> out = {};
  std::ranges::move(range, std::back_inserter(out));
  return out;
}

struct [[nodiscard]] LValue {
  const std::string_view iden;
};

struct [[nodiscard]] RValue {
  const std::string_view iden;
};

struct [[nodiscard]] TargetAlias {};

struct [[nodiscard]] PrereqAlias {};

using ValueType = std::variant<LValue, RValue, TargetAlias, PrereqAlias>;

// An association is generated by the first pass of parsing. It consists of an
// lvalue or an rvalue -- packed into the ValueType variant -- that will be
// resolved some point later on in parsing.
using Association = std::tuple<std::string_view, std::vector<ValueType>>;

using Binding = std::pair<std::string_view, std::string>;

// Intermediate representation for Rules -- after parsing they'll need to be
// resolved by looking each `ValueType` variant up in the environment.
struct [[nodiscard]] RuleIr {
  const ValueType target;
  const std::vector<ValueType> prereqs;
  const std::vector<std::vector<ValueType>> actions;
};

struct [[nodiscard]] Fill {
  [[nodiscard]] static std::string_view get_extension(std::string_view s) {
    const auto offset = s.rfind(".");

    if (std::string_view::npos == offset || s.size() - 1 == offset) {
      throw FabError(
          FabError::UnexpectedFill{.expected = "<base>.<ext>", .actual = s});
    }

    return s.substr(offset + 1);
  }

  const std::string_view target;
  const std::string_view target_ext;
  const std::string_view prereq;
  const std::string_view prereq_ext;

  Fill(std::string_view t, std::string_view p)
      : target(t)
      , target_ext(get_extension(t))
      , prereq(p)
      , prereq_ext(get_extension(p)) {
  }
};

struct [[nodiscard]] GenericRule {
  const std::string_view target_ext;
  const std::string_view prereq_ext;
  const std::vector<std::vector<ValueType>> actions;
};

bool
operator==(const GenericRule &lhs, const Fill &rhs) {
  return std::tie(lhs.target_ext, lhs.prereq_ext) ==
         std::tie(rhs.target_ext, rhs.prereq_ext);
}

struct [[nodiscard]] Ir {
  const std::vector<RuleIr> rules;
  const std::vector<Association> associations;
};

class [[nodiscard]] LexState {
  const std::string_view buf;
  std::string_view::const_iterator m_offset = buf.cbegin();

public:
  LexState(std::string_view source)
      : buf(source) {
  }

  [[nodiscard]] char next() {
    if (eof()) {
      throw FabError(FabError::UnexpectedEof{});
    }

    char c = *m_offset;
    m_offset = std::next(m_offset);
    return c;
  }

  [[nodiscard]] bool eof() const {
    return buf.cend() == m_offset;
  }

  void eat(char expected) {
    if (expected != *m_offset) {
      throw FabError(FabError::UnexpectedCharacter{.expected = expected,
                                                   .actual = *m_offset});
    }

    assert(next() == expected);
  }

  [[nodiscard]] Option<char> peek() const {
    if (eof()) {
      return {};
    }

    return *m_offset;
  }

  template <typename I>
  [[nodiscard]] std::string_view
  extract_lexeme(I begin, I end) requires std::input_iterator<I> {
    return std::string_view{begin, end};
  }

  template <typename P>
  [[nodiscard]] auto eat_until(P pred) requires std::predicate<P, char> {
    const auto begin = m_offset;

    while (!pred(next()))
      ;

    assert(m_offset != buf.begin());
    m_offset = std::prev(m_offset);

    return std::tuple{begin, m_offset};
  }
};

class [[nodiscard]] ParseState {
  const std::vector<Token> tokens;
  std::vector<Token>::const_iterator m_offset = tokens.cbegin();
  std::vector<Association> m_associations = {};
  std::vector<Fill> m_fills = {};
  std::vector<RuleIr> m_rules = {};
  std::vector<GenericRule> m_generic_rules = {};

private:
  const Token &eat(Token::Ty expected) {
    assert(m_offset != tokens.end());
    const auto &actual = *m_offset;

    if (expected != actual.ty()) {
      throw FabError(FabError::UnexpectedTokenType{.expected = expected,
                                                   .actual = actual.ty()});
    }

    m_offset = std::next(m_offset);
    return actual;
  }

  template <Token::Ty ty>
  [[nodiscard]] std::string_view eat_for_lexeme() {
    return eat(ty).lexeme<ty>();
  }

  [[nodiscard]] std::tuple<std::vector<ValueType>,
                           std::vector<std::vector<ValueType>>>
  rule() {
    if (peek() != Token::Ty::LBrace) {
      eat(Token::Ty::Arrow);
    }

    std::vector<ValueType> prereqs = this->prereqs();

    if (Token::Ty::SemiColon == peek()) {
      eat(Token::Ty::SemiColon);
      return std::make_tuple(std::move(prereqs),
                             std::vector<std::vector<ValueType>>{});
    }

    std::vector<std::vector<ValueType>> actions = this->action();
    return std::tuple{std::move(prereqs), std::move(actions)};
  }

  [[nodiscard]] Token::Ty peek() const {
    if (eof()) {
      throw FabError(FabError::UnexpectedEof{});
      return {};
    } else {
      return m_offset->ty();
    }
  }

  [[nodiscard]] ValueType iden_status() {
    const auto peeked = peek();

    if (Token::Ty::Iden == peeked) {
      return RValue{eat_for_lexeme<Token::Ty::Iden>()};
    } else if (Token::Ty::Macro == peeked) {
      return LValue{eat_for_lexeme<Token::Ty::Macro>()};
    } else if (Token::Ty::TargetAlias == peeked) {
      eat(Token::Ty::TargetAlias);
      return TargetAlias{};
    } else if (Token::Ty::PrereqAlias == peeked) {
      eat(Token::Ty::PrereqAlias);
      return PrereqAlias{};
    } else {
      throw FabError(FabError::TokenNotInExpectedSet{
          .expected = {{Token::Ty::Iden}, {Token::Ty::Macro}},
          .actual = peeked});
    }
  }

  [[nodiscard]] ValueType target() {
    return iden_status();
  }

  [[nodiscard]] std::vector<ValueType> prereqs() {
    return iden_list();
  }

  [[nodiscard]] std::vector<std::vector<ValueType>> action() {
    eat(Token::Ty::LBrace);

    auto done = bool{false};
    auto actions = std::vector<std::vector<ValueType>>{};

    while (!done) {
      actions.push_back(iden_list());
      eat(Token::Ty::SemiColon);

      if (Token::Ty::RBrace == peek()) {
        done = true;
      }
    }

    eat(Token::Ty::RBrace);
    return actions;
  }

  [[nodiscard]] std::vector<ValueType> iden_list() {
    std::vector<ValueType> idens;

    while (matches(peek(), Token::Ty::Iden, Token::Ty::Macro,
                   Token::Ty::TargetAlias, Token::Ty::PrereqAlias)) {
      idens.push_back(iden_status());
    }

    return idens;
  }

  [[nodiscard]] std::vector<ValueType> assignment() {
    eat(Token::Ty::Eq);
    auto idens = iden_list();
    eat(Token::Ty::SemiColon);
    return idens;
  }

  void generic_rule() {
    const auto target_ext = eat_for_lexeme<Token::Ty::GenericRule>();
    auto prereq_ext = std::string_view{""};

    if (peek() != Token::Ty::LBrace) {
      eat(Token::Ty::Arrow);
      prereq_ext = eat_for_lexeme<Token::Ty::GenericRule>();
    }

    m_generic_rules.push_back(GenericRule{.target_ext = target_ext,
                                          .prereq_ext = prereq_ext,
                                          .actions = std::move(action())});
  }

  void fill() {
    const auto target = eat_for_lexeme<Token::Ty::Fill>();
    auto prereq = std::string_view{""};

    if (peek() != Token::Ty::SemiColon) {
      eat(Token::Ty::Arrow);
      prereq = eat_for_lexeme<Token::Ty::Fill>();
    }

    eat(Token::Ty::SemiColon);
    m_fills.push_back(Fill{target, prereq});
  }

public:
  ParseState(std::vector<Token> &&tokens)
      : tokens(tokens) {
  }

  void stmt_list() {
    if (Token::Ty::GenericRule == peek()) {
      generic_rule();
      return;
    }

    if (Token::Ty::Fill == peek()) {
      fill();
      return;
    }

    const auto iden = iden_status();
    if (Token::Ty::Eq == peek()) {
      if (std::holds_alternative<RValue>(iden)) {
        const auto lhs = std::get<RValue>(iden).iden;
        const auto rhs = assignment();
        m_associations.emplace_back(lhs, rhs);
      } else {
        throw FabError(
            FabError::ExpectedLValue{.macro = std::get<LValue>(iden).iden});
      }
    } else if (matches(peek(), Token::Ty::Arrow, Token::Ty::LBrace)) {
      const auto [prereqs, actions] = rule();
      m_rules.push_back(
          {.target = iden, .prereqs = prereqs, .actions = actions});
    } else {
      throw FabError(
          FabError::TokenNotInExpectedSet{.expected = {{Token::Ty::Eq},
                                                       {Token::Ty::Arrow},
                                                       {Token::Ty::LBrace}},
                                          .actual = peek()});
    }
  }

  [[nodiscard]] bool eof() const {
    assert(m_offset != tokens.cend());
    return Token::Ty::Eof == m_offset->ty();
  }

  [[nodiscard]] Ir into_ir() && {
    for (const auto &fill : m_fills) {
      const auto matching =
          std::find(m_generic_rules.cbegin(), m_generic_rules.cend(), fill);

      if (m_generic_rules.cend() == matching) {
        throw FabError(FabError::UndefinedGenericRule{.target = fill.target,
                                                      .prereq = fill.prereq});
      } else {
        m_rules.push_back(detail::RuleIr{
            .target = detail::RValue{.iden = fill.target},
            .prereqs = std::vector<detail::ValueType>{detail::RValue{
                .iden = fill.prereq}},
            .actions = matching->actions});
      }
    }

    return Ir{
        .rules = std::move(m_rules),
        .associations = std::move(m_associations),
    };
  }
};

namespace resolve {
// Fab's grammar supplies two main scopes: action and _everything else_.  The
// only really special thing about action scope is visibility of _special_
// macros -- namely make(1) style '$@' and '$<' to refer to the current target
// and prerequisites, respectively. By design, this is not caught by the
// parser (since it be just be a few more nasty if checks). Instead, this is
// encoded directly into ValueType with the variants TargetAlias and
// PrereqAlias. Resolver operator() overloads constrain their parameters with
// these two concepts to disallow invalid Fab programs.
template <typename T>
concept ActionScope = SameAs<T, TargetAlias, PrereqAlias>;

template <typename T>
concept FileScope = SameAs<T, RValue, LValue>;

struct [[nodiscard]] Resolver {
  const std::map<std::string_view, std::string> &macros;

  [[nodiscard]] std::string_view operator()(const RValue &term) const {
    return term.iden;
  }

  [[nodiscard]] std::string_view operator()(const LValue &res) const {
    const auto &pair = find_or_throw(macros, res.iden, [&res] {
      return FabError(FabError::UndefinedVariable{.var = res.iden});
    });

    return pair.second;
  }

  template <typename T>
  [[nodiscard]] std::string_view
  operator()(const T &) const requires ActionScope<T> {
    throw FabError(FabError::BuiltInMacrosRequireActionScope{});
  }
};

struct [[nodiscard]] ActionResolver {
  const std::string_view target;
  const std::vector<std::string_view> prereqs;
  const std::map<std::string_view, std::string> &macros;

  [[nodiscard]] std::string operator()(const TargetAlias &) const {
    return sv_to_string(target);
  }

  [[nodiscard]] std::string operator()(const PrereqAlias &) const {
    return foldl(prereqs, " ");
  }

  template <typename T>
  [[nodiscard]] std::string
  operator()(const T &variant) const requires FileScope<T> {
    return sv_to_string(Resolver{macros}(variant));
  }
};

namespace detail {
[[nodiscard]] Binding
resolve_rvalue(const Association &association) {
  const auto into_rvalue = [](const ValueType &v) {
    return std::get<RValue>(v).iden;
  };

  const auto [iden, values] = association;
  return std::make_pair(iden, foldl(values, " ", into_rvalue));
}

struct [[nodiscard]] NonRValueResolver {
  const std::map<std::string_view, std::string> &macros;

  [[nodiscard]] Binding operator()(const Association &association) const {
    const auto resolver = Resolver{.macros = macros};
    const auto [iden, values] = association;
    return std::make_pair(iden, foldl(values, " ", [&](const ValueType &value) {
                            return std::visit(resolver, value);
                          }));
  }
};

[[nodiscard]] std::map<std::string_view, std::string>
resolve_associations(const std::vector<Association> &associations) {
  const auto is_rvalue = [](const Association &association) {
    const auto [iden, values] = association;
    return std::ranges::all_of(values, [](const auto &v) {
      return std::holds_alternative<RValue>(v);
    });
  };

  auto rvalues = std::views::filter(associations, is_rvalue);
  auto non_rvalues = std::views::filter(associations, std::not_fn(is_rvalue));

  auto macros = std::map<std::string_view, std::string>{};
  std::ranges::move(std::views::transform(rvalues, resolve_rvalue),
                    std::inserter(macros, macros.begin()));
  std::ranges::move(
      std::views::transform(non_rvalues, NonRValueResolver{.macros = macros}),
      std::inserter(macros, macros.begin()));

  return macros;
}

[[nodiscard]] Rule
resolve_rule(const std::map<std::string_view, std::string> &macros,
             const RuleIr &rule) {
  const auto resolver = Resolver{.macros = macros};
  const auto target = std::visit(resolver, rule.target);
  auto prereqs =
      move_collect(std::views::transform(rule.prereqs, [&](const ValueType &v) {
        return std::visit(resolver, v);
      }));

  const auto resolve_action = [&](const ValueType &v) {
    const auto resolver =
        ActionResolver{.target = target, .prereqs = prereqs, .macros = macros};
    return std::visit(resolver, v);
  };

  auto actions =
      move_collect(std::views::transform(rule.actions, [&](const auto &action) {
        return foldl(action, " ", resolve_action);
      }));

  return Rule{.target = target,
              .prereqs = std::move(prereqs),
              .actions = std::move(actions)};
}

[[nodiscard]] std::vector<Rule>
resolve_rules(const std::map<std::string_view, std::string> &macros,
              const std::vector<RuleIr> &rule_irs) {
  return move_collect(std::views::transform(rule_irs, [&](const RuleIr &rule) {
    return resolve_rule(macros, rule);
  }));
}

template <typename T>
[[nodiscard]] std::set<T, std::less<>>
into_set(std::vector<T> vs) requires std::move_constructible<T> {
  return std::set<T, std::less<>>{
      std::make_move_iterator(vs.begin()),
      std::make_move_iterator(vs.end()),
  };
}

} // namespace detail

[[nodiscard]] Environment
parse_state(Ir ir) {
  auto macros = detail::resolve_associations(ir.associations);
  auto rules = detail::resolve_rules(macros, ir.rules);

  if (rules.empty()) {
    throw FabError(FabError::NoRulesToRun{});
  }

  const std::string_view head = rules.front().target;
  return Environment{.macros = std::move(macros),
                     .rules = detail::into_set(std::move(rules)),
                     .head = head};
}
} // namespace resolve
} // namespace detail

Token::Token(Token::Ty ty, Option<std::string_view> lexeme)
    : m_ty(ty)
    , m_lexeme(lexeme) {
}

[[nodiscard]] std::vector<Token>
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
    case '#': {
      [[maybe_unused]] auto dc =
          state.eat_until([](char c) { return '\n' == c; });
      state.eat('\n');
      break;
    }
    case ':':
      state.eat('=');
      tokens.push_back(Token::make<Token::Ty::Eq>());
      break;
    case ';':
      tokens.push_back(Token::make<Token::Ty::SemiColon>());
      break;
    case '{':
      tokens.push_back(Token::make<Token::Ty::LBrace>());
      break;
    case '}':
      tokens.push_back(Token::make<Token::Ty::RBrace>());
      break;
    case '<':
      state.eat('-');
      tokens.push_back(Token::make<Token::Ty::Arrow>());
      break;
    case '[':
      if ('*' == state.peek()) {
        state.eat('*');
        state.eat('.');
        const auto [begin, end] =
            state.eat_until([](char c) { return ']' == c; });
        state.eat(']');
        tokens.push_back(Token::make<Token::Ty::GenericRule>(
            state.extract_lexeme(begin, end)));
        break;
      } else {
        const auto [begin, end] =
            state.eat_until([](char c) { return ']' == c; });
        state.eat(']');
        tokens.push_back(
            Token::make<Token::Ty::Fill>(state.extract_lexeme(begin, end)));
        break;
      }
    case '$': {
      if ('@' == state.peek()) {
        state.eat('@');
        tokens.push_back(Token::make<Token::Ty::TargetAlias>());
        break;
      }

      if ('<' == state.peek()) {
        state.eat('<');
        tokens.push_back(Token::make<Token::Ty::PrereqAlias>());
        break;
      }

      state.eat('(');
      const auto [begin, end] =
          state.eat_until([](char c) { return ')' == c; });
      tokens.push_back(
          Token::make<Token::Ty::Macro>(state.extract_lexeme(begin, end)));
      state.eat(')');
      break;
    }
    default: {
      const auto [begin, end] =
          state.eat_until([](char c) { return matches(c, ' ', '\n', ';'); });

      tokens.push_back(Token::make<Token::Ty::Iden>(
          state.extract_lexeme(std::prev(begin), end)));
      break;
    }
    }
  }

  tokens.push_back(Token::make<Token::Ty::Eof>());
  return tokens;
}

[[nodiscard]] Environment
parse(std::vector<Token> &&tokens) {
  auto state = detail::ParseState{std::move(tokens)};
  while (!state.eof()) {
    state.stmt_list();
  }

  return detail::resolve::parse_state(std::move(state).into_ir());
}

[[nodiscard]] bool
operator<(const Rule &lhs, std::string_view rhs) {
  return lhs.target < rhs;
}

[[nodiscard]] bool
operator<(std::string_view lhs, const Rule &rhs) {
  return lhs < rhs.target;
}

[[nodiscard]] bool
operator<(const Rule &lhs, const Rule &rhs) {
  return lhs.target < rhs.target;
}

[[nodiscard]] bool
Environment::is_leaf(std::string_view rule) const {
  return !rules.contains(rule);
}

[[nodiscard]] const Rule &
Environment::get(std::string_view target) const {
  return find_or_throw(rules, target, [&target] {
    return detail::FabError(detail::FabError::UnknownTarget{.target = target});
  });
}

std::ostream &
operator<<(std::ostream &os, const Token &token) {
  os << token.ty();

  switch (token.ty()) {
  case Token::Ty::Fill:
    os << "['" << token.lexeme<Token::Ty::Fill>() << "']";
  case Token::Ty::Iden:
    os << "['" << token.lexeme<Token::Ty::Iden>() << "']";
  case Token::Ty::Macro:
    os << "['" << token.lexeme<Token::Ty::Macro>() << "']";
  case Token::Ty::GenericRule:
    os << "['" << token.lexeme<Token::Ty::GenericRule>() << "']";
  default:
    break;
  }

  return os;
}

std::ostream &
operator<<(std::ostream &os, const Token::Ty &ty) {
  switch (ty) {
  case Token::Ty::Arrow:
    return os << "ARROW";
  case Token::Ty::Eof:
    return os << "EOF";
  case Token::Ty::Eq:
    return os << "EQ";
  case Token::Ty::Fill:
    return os << "FILL";
  case Token::Ty::Iden:
    return os << "IDEN";
  case Token::Ty::LBrace:
    return os << "LBRACE";
  case Token::Ty::Macro:
    return os << "MACRO";
  case Token::Ty::PrereqAlias:
    return os << "PREREQALIAS";
  case Token::Ty::RBrace:
    return os << "RBRACE";
  case Token::Ty::SemiColon:
    return os << "SEMICOLON";
  case Token::Ty::GenericRule:
    return os << "generic_rule";
  case Token::Ty::TargetAlias:
    return os << "TARGETALIAS";
  default:
    __builtin_unreachable();
  }
}

std::ostream &
operator<<(std::ostream &os, const Environment &env) {
  for (const auto &r : env.rules) {
    os << r;
  }

  return os;
}

std::ostream &
operator<<(std::ostream &os, const Rule &r) {
  os << "{.target = " << r.target;
  os << ", .prereqs = [";

  {
    auto first = bool{true};
    for (const auto &d : r.prereqs) {
      if (!first) {
        os << ", ";
      }

      os << d;
      first = false;
    }
  }

  os << "]"
     << ", .actions = [";

  {
    auto first = bool{true};
    for (const auto &a : r.actions) {
      if (!first) {
        os << ", ";
      }

      os << a;
      first = false;
    }
  }

  os << "]"
     << "}";

  return os;
}
