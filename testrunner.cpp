#include "fab.h"
#include <gtest/gtest.h>
#include <iostream>

std::ostream &operator<<(std::ostream &os, const Token &t) {
  if (t.lexeme.has_value()) {
    return os << t.lexeme.value();
  }

  switch (t.token_type) {
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
  default:
    fatal("unknown token");
  }
}

std::ostream &operator<<(std::ostream &os, const Rule &r) {
  return os << "{ .target = " << r.target << ", .action = " << r.action
            << ", .dependency = " << r.dependency << "}" << std::endl;
}

std::ostream &operator<<(std::ostream &os, const Environment &env) {
  for (auto r : env.m_rules) {
    os << r;
  }

  return os;
}

TEST(Lexer, ItRecognizesArrows) {
  auto actual = Lexer("<-").lex();

  auto expected =
      std::vector<Token>{{TokenType::Arrow, {}}, {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesIdentifiers) {
  const std::string foo = "foo;";
  auto actual = Lexer(foo).lex();

  auto expected = std::vector<Token>{{TokenType::Iden, std::string_view{"foo"}},
                                     {TokenType::SemiColon, {}},
                                     {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesBraces) {
  const std::string foo = "{}";
  auto actual = Lexer(foo).lex();

  auto expected = std::vector<Token>{
      {TokenType::LBrace, {}}, {TokenType::RBrace, {}}, {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesAFullRule) {
  const std::string rule = "foo <- bar {baz;}";
  auto actual = Lexer(rule).lex();

  auto expected =
      std::vector<Token>{{TokenType::Iden, "foo"}, {TokenType::Arrow, {}},
                         {TokenType::Iden, "bar"}, {TokenType::LBrace, {}},
                         {TokenType::Iden, "baz"}, {TokenType::SemiColon, {}},
                         {TokenType::RBrace, {}},  {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Parser, ItParsesARule) {
  const std::string program = "main <- main.cpp { c++ -o main main.cpp; }";
  auto tokens = Lexer(program).lex();
  auto actual = Parser(tokens).parse();

  auto expected = Environment{{{.target = "main",
                                .action = "c++ -o main main.cpp",
                                .dependency = "main.cpp"}}};

  ASSERT_EQ(expected, actual);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
