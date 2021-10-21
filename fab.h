#ifndef FAB_H
#define FAB_H

#include <optional>
#include <set>
#include <string_view>

template <typename T>
using Option = std::optional<T>;

enum class TokenType {
  // Simple TokenTypes
  Arrow,
  Eof,
  Eq,
  LBrace,
  PrereqAlias,
  RBrace,
  SemiColon,
  TargetAlias,

  // Complex tokens -- ie. those that have a non-null lexeme.
  Fill,
  Iden,
  Macro,
  Stencil,
};

class Token {
  const TokenType m_token_type;
  const Option<std::string_view> m_lexeme;

  Token(TokenType token_type, Option<std::string_view> lexeme)
      : m_token_type(token_type)
      , m_lexeme(lexeme) {
  }

public:
  bool operator==(const Token &) const = default;

  template <TokenType token_type>
  static Token make(Option<std::string_view> lexeme) {
    static_assert(Token::complex<token_type>(),
                  "Only complex tokens have lexemes. Use the no-arg "
                  "constructor for simple tokens.");
    return Token(token_type, lexeme);
  }

  template <TokenType token_type>
  static Token make() {
    return Token(token_type, {});
  }

  template <TokenType token_type>
  std::string_view lexeme() const {
    static_assert(Token::complex<token_type>(),
                  "Only complex tokens have lexemes.");

    assert(m_lexeme.has_value());
    return m_lexeme.value();
  }

  TokenType token_type() const {
    return m_token_type;
  }

  template <TokenType ty>
  constexpr static bool complex() {
    return TokenType::Fill == ty || TokenType::Iden == ty ||
           TokenType::Macro == ty || TokenType::Stencil == ty;
  }
};

struct Rule {
  const std::string_view target;
  const std::vector<std::string_view> prereqs;
  const std::vector<std::string> actions;

  bool operator==(const Rule &) const = default;

  inline bool is_phony() const {
    return actions.empty();
  }
};

inline bool
operator<(const Rule &lhs, std::string_view rhs) {
  return lhs.target < rhs;
}

inline bool
operator<(std::string_view lhs, const Rule &rhs) {
  return lhs < rhs.target;
}

inline bool
operator<(const Rule &lhs, const Rule &rhs) {
  return lhs.target < rhs.target;
}

struct Environment {
  // During parsing the resolver needs to allocate strings for macro lookup. The
  // lifetime of these strings is managed by this map. Each rule in the set
  // below *may* hold a string_view to one of the values in this map.
  const std::map<std::string_view, std::string> macros;
  const std::set<Rule, std::less<>> rules;
  std::string_view head;

  const Rule &get(std::string_view) const;
  bool is_leaf(std::string_view) const;
  bool operator==(const Environment &) const = default;
};

std::vector<Token> lex(std::string_view source);

// Fab's grammar allows two main non-terminals: assignment and rule statements.
// Parsing produces an environment that maps the schema specified by a fabfile
// into proper C++ types.
//
// clang-format off
// <Fabfile>       ::= <stmt_list>
// <stmt_list>     ::= <stmt> <stmt_list>
// <stmt_list>     ::= <stmt>
// <stmt>          ::= <assignment>
// <stmt>          ::= <rule>
// <stmt>          ::= <stencil>
// <stmt>          ::= <fill>
// <stencil>       ::= STENCIL EXTENSION <- STENCIL EXTENSION LBRACE <action_list> RBRACE
// <stencil>       ::= STENCIL EXTENSION LBRACE <action_list> RBRACE
// <fill>          ::= FILL <- FILL SEMICOLON
// <fill>          ::= FILL SEMICOLON
// <assignment>    ::= <iden> := <iden_list>
// <rule>          ::= <target> <- <iden_list> SEMICOLON
// <rule>          ::= <target> <- <iden_list> LBRACE <action> RBRACE
// <rule>          ::= <target> LBRACE <action_list> RBRACE
// <target>        ::= iden
// <dep>           ::= iden
// <alias>         ::= $@
// <alias>         ::= $<
// <action_list>   ::= <alias> <action_list>
// <action_list>   ::= IDEN <action_list>
// <action_list>   ::= MACRO <action_list>
// <action_list>   ::= <alias> SEMICOLON
// <action_list>   ::= IDEN SEMICOLON
// <action_list>   ::= MACRO SEMICOLON
// <iden_list>     ::= IDEN SEMICOLON
// <iden_list>     ::= MACRO SEMICOLON
// <iden_list>     ::= IDEN <iden_list>
// <iden_list>     ::= MACRO <iden_list>
// clang-format on
Environment parse(std::vector<Token> &&tokens);

template <typename T>
std::ostream &
operator<<(std::ostream &os, const Option<T> &t) {
  if (t) {
    return os << t.value();
  } else {
    return os << "NONE";
  }
}

std::ostream &operator<<(std::ostream &os, const Token &token);
std::ostream &operator<<(std::ostream &os, const TokenType &t);
std::ostream &operator<<(std::ostream &os, const Environment &env);
std::ostream &operator<<(std::ostream &os, const Rule &r);

#endif // FAB_H
