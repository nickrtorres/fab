#ifndef FAB_H
#define FAB_H

#include <optional>
#include <set>
#include <string_view>

template <typename T>
using Option = std::optional<T>;

class Token {
public:
  enum class Ty {
    // Simple
    Arrow,
    Eof,
    Eq,
    LBrace,
    PrereqAlias,
    RBrace,
    SemiColon,
    TargetAlias,

    // Complex
    Fill,
    Iden,
    Macro,
    GenericRule,
  };

private:
  const Token::Ty m_ty;
  const Option<std::string_view> m_lexeme;

  template <Token::Ty ty>
  [[nodiscard]] constexpr static bool complex() {
    return Token::Ty::Fill == ty || Token::Ty::Iden == ty ||
           Token::Ty::Macro == ty || Token::Ty::GenericRule == ty;
  }

  Token(Token::Ty, Option<std::string_view>);

public:
  template <Token::Ty ty>
  static Token make(Option<std::string_view> lexeme) {
    static_assert(Token::complex<ty>(),
                  "Only complex tokens have lexemes. Use Token::make() for "
                  "simple tokens.");
    return Token(ty, lexeme);
  }

  template <Token::Ty ty>
  static Token make() {
    return Token(ty, {});
  }

  template <Token::Ty ty>
  [[nodiscard]] std::string_view lexeme() const {
    static_assert(Token::complex<ty>(), "Only complex tokens have lexemes.");

    assert(m_lexeme.has_value());
    return m_lexeme.value();
  }

  [[nodiscard]] inline Token::Ty ty() const {
    return m_ty;
  }

  bool operator==(const Token &) const = default;
};

struct Rule {
  const std::string_view target;
  const std::vector<std::string_view> prereqs;
  const std::vector<std::string> actions;

  bool operator==(const Rule &) const = default;

  [[nodiscard]] inline bool is_phony() const {
    return actions.empty();
  }
};

bool operator<(const Rule &lhs, std::string_view rhs);
bool operator<(std::string_view lhs, const Rule &rhs);
bool operator<(const Rule &lhs, const Rule &rhs);

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
std::ostream &operator<<(std::ostream &os, const Token::Ty &t);
std::ostream &operator<<(std::ostream &os, const Environment &env);
std::ostream &operator<<(std::ostream &os, const Rule &r);

#endif // FAB_H
