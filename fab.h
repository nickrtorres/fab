#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

// Beautify options a bit
template <typename T> using Option = std::optional<T>;

[[noreturn]] void
fatal(std::string_view msg,
      std::source_location loc = std::source_location::current());

struct LexError final : public std::runtime_error {
  const char *msg;

  LexError(const char *m);
  const char *what() noexcept;
};

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

class Lexer {
  std::string_view m_buf;
  std::size_t m_offset = 0;
  bool m_eof = false;

public:
  Lexer(std::string_view source);

  Option<char> next();
  void eat(char expected);
  std::tuple<std::size_t, std::size_t>
  eat_until(std::function<bool(char)> pred);
  Option<char> peek() const;

  std::vector<Token> lex() &&;
};

struct Rule {
  std::string_view target;
  const std::string action;
  std::string_view dependency;

  friend bool operator==(const Rule &, const Rule &) = default;
};

struct RuleHash {
  std::size_t operator()(const Rule &r) const noexcept;
};

struct Environment {
  std::unordered_set<Rule, RuleHash> m_rules;

  void insert(Rule &&rule);
  const Rule &default_rule();
  friend bool operator==(const Environment &, const Environment &) = default;
};

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
class Parser {
  std::vector<Token> m_tokens;
  std::vector<Token>::const_iterator m_offset = m_tokens.cbegin();
  Environment m_env = {};

  void rule();
  std::string_view target();
  std::string_view dependency();
  std::string action();
  void iden_list();

  const Token &expect(TokenType);
  bool eof();

public:
  Parser(std::vector<Token> tokens);
  Environment parse() &&;
};
