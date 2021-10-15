#ifndef FAB_H
#define FAB_H

#include <optional>
#include <set>
#include <string_view>
#include <vector>

template <typename T>
using Option = std::optional<T>;

enum class TokenType {
  Arrow,
  Eof,
  Eq,
  Iden,
  LBrace,
  Macro,
  RBrace,
  SemiColon,
};

struct Token {
  const TokenType token_type;
  const Option<std::string_view> lexeme;

  bool operator==(const Token &) const = default;
};

struct Rule {
  const std::string_view target;
  const std::vector<std::string_view> dependencies;
  const std::string action;

  bool operator==(const Rule &) const = default;

  inline bool is_phony() const {
    return action.empty();
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
// <Fabfile>    ::= <stmt_list>
// <stmt_list>  ::= <stmt> <stmt_list>
// <stmt_list>  ::= <stmt>
// <stmt>       ::= <assignment>
// <stmt>       ::= <rule>
// <assignment> ::= <iden> := <iden_list>
// <rule>       ::= <target> <- <iden_list> semicolon
// <rule>       ::= <target> <- <iden_list> lbrace <action> rbrace
// <rule>       ::= <target> lbrace <action> rbrace
// <target>     ::= iden
// <dep>        ::= iden
// <action>     ::= iden_list
// <iden_list>  ::= iden semicolon
// <iden_list>  ::= macro semicolon
// <iden_list>  ::= iden space <iden_list>
// <iden_list>  ::= macro space <iden_list>
Environment parse(std::vector<Token> &&tokens);

#endif // FAB_H
