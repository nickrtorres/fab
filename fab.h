#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

template <typename T> using Option = std::optional<T>;

enum class TokenType {
  Arrow,
  Iden,
  LBrace,
  RBrace,
  SemiColon,
  Eof,
};

struct Token {
  const TokenType token_type;
  const Option<std::string_view> lexeme;

  friend bool operator==(const Token &, const Token &) = default;
};

struct Rule {
  std::string_view target;
  const std::string action;
  std::string_view dependency;

  friend bool operator==(const Rule &, const Rule &) = default;
};

inline bool operator<(const Rule &lhs, std::string_view rhs) {
  return lhs.target < rhs;
}

inline bool operator<(std::string_view lhs, const Rule &rhs) {
  return lhs < rhs.target;
}

inline bool operator<(const Rule &lhs, const Rule &rhs) {
  return lhs.target < rhs.target;
}

struct Environment {
  std::set<Rule, std::less<>> rules;

  const Rule &get(std::string_view) const;
  bool is_terminal(std::string_view) const;
  void insert(Rule &&rule);
  friend bool operator==(const Environment &, const Environment &) = default;
};

std::vector<Token> lex(std::string_view source);

// <rule>      ::= <target> arrow <dep> lbrace <action> rbrace
// <rule>      ::= <target> lbrace <action> rbrace
// <target>    ::= iden
// <dep>       ::= iden
// <action>    ::= iden_list
// <iden_list> ::= iden semicolon
// <iden_list> ::= iden space <iden_list>
Environment parse(std::vector<Token> &&tokens);

namespace detail {
struct LexError final : public std::runtime_error {
  const char *msg;

  LexError(const char *m);
  const char *what() noexcept;
};

class LexState {
  std::string_view m_buf;
  std::size_t m_offset = 0;
  bool m_eof = false;

public:
  LexState(std::string_view);
  Option<char> next();
  bool eof() const;
  void eat(char expected);
  std::tuple<std::size_t, std::size_t>
  eat_until(std::function<bool(char)> pred);
  std::string_view extract_lexeme(std::size_t begin, std::size_t end);
  Option<char> peek() const;
};

class ParseState {
  std::vector<Token> m_tokens;
  std::vector<Token>::const_iterator m_offset = m_tokens.cbegin();
  Environment m_env;

  std::string_view target();
  std::string_view dependency();
  std::string action();
  void iden_list();

  const Token &expect(TokenType);

public:
  ParseState(std::vector<Token> &&tokens);
  Environment env() &&;
  void rule();
  bool eof();
};
} // namespace detail
