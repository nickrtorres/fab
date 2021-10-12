#include "fab.h"
#include <gtest/gtest.h>
#include <iostream>

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

TEST(Lexer, ItExpectsValidTokens) {
  ASSERT_THROW(lex("<="), std::runtime_error);
}

TEST(Parser, ItParsesARule) {
  auto tokens = lex("main <- main.cpp { c++ -o main main.cpp; }");
  auto actual = parse(std::move(tokens));

  auto expected = Environment{{{.target = "main",
                                .dependency = "main.cpp",
                                .action = "c++ -o main main.cpp"}}};

  ASSERT_EQ(expected, actual);
}

TEST(Parser, ItExpectsSemicolons) {
  auto tokens = lex("main <- main.cpp { c++ -o main main.cpp }");
  ASSERT_THROW(parse(std::move(tokens)), std::runtime_error);
}

TEST(Parser, ItOnlyKnowsDefinedVariables) {
  auto tokens = lex("main <- main.cpp { $(cmd); }");
  ASSERT_THROW(parse(std::move(tokens)), std::runtime_error);
}

int
main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
