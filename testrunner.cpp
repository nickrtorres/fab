#include "fab.h"
#include <gtest/gtest.h>
#include <iostream>

std::ostream &
operator<<(std::ostream &os, const Token &t) {
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
    std::abort();
  }
}

std::ostream &
operator<<(std::ostream &os, const Rule &r) {
  return os << "{ .target = " << r.target << ", .action = " << r.action
            << ", .dependency = " << r.dependency << "}" << std::endl;
}

std::ostream &
operator<<(std::ostream &os, const Environment &env) {
  for (auto r : env.rules) {
    os << r;
  }

  return os;
}

TEST(Lexer, ItRecognizesArrows) {
  auto actual = lex("<-");

  auto expected =
      std::vector<Token>{{TokenType::Arrow, {}}, {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesIdentifiers) {
  auto actual = lex("foo;");

  auto expected = std::vector<Token>{{TokenType::Iden, std::string_view{"foo"}},
                                     {TokenType::SemiColon, {}},
                                     {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesBraces) {
  auto actual = lex("{}");

  auto expected = std::vector<Token>{
      {TokenType::LBrace, {}}, {TokenType::RBrace, {}}, {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesAFullRule) {
  auto actual = lex("foo <- bar { baz; }");

  auto expected =
      std::vector<Token>{{TokenType::Iden, "foo"}, {TokenType::Arrow, {}},
                         {TokenType::Iden, "bar"}, {TokenType::LBrace, {}},
                         {TokenType::Iden, "baz"}, {TokenType::SemiColon, {}},
                         {TokenType::RBrace, {}},  {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesMacros) {
  auto actual = lex("$(CC)");

  auto expected =
      std::vector<Token>{{TokenType::Macro, "CC"}, {TokenType::Eof, {}}};
  ASSERT_EQ(expected, actual);
}

TEST(Parser, ItParsesARule) {
  auto tokens = lex("main <- main.cpp { c++ -o main main.cpp; }");
  auto actual = parse(std::move(tokens));

  auto expected = Environment{{{.target = "main",
                                .action = "c++ -o main main.cpp",
                                .dependency = "main.cpp"}}};

  ASSERT_EQ(expected, actual);
}

int
main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
