#include <iostream>

#include <gtest/gtest.h>

#include "fab.h"

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

TEST(Lexer, ItRecognizesStencils) {
  auto actual = lex("[*.o] <- [*.c] { cc -o $@ $<; }");

  auto expected = std::vector<Token>{
      {TokenType::Stencil, {"o"}},  {TokenType::Arrow, {}},
      {TokenType::Stencil, {"c"}},  {TokenType::LBrace, {}},
      {TokenType::Iden, "cc"},      {TokenType::Iden, "-o"},
      {TokenType::TargetAlias, {}}, {TokenType::PrereqAlias, {}},
      {TokenType::SemiColon, {}},   {TokenType::RBrace, {}},
      {TokenType::Eof, {}}};

  ASSERT_EQ(expected, actual);
}

TEST(Parser, ItParsesARule) {
  auto tokens = lex("main <- main.cpp lib.cpp { c++ -o main main.cpp; }");
  auto actual = parse(std::move(tokens)).rules;

  auto expected =
      std::set<Rule, std::less<>>{{.target = "main",
                                   .prereqs = {"main.cpp", "lib.cpp"},
                                   .actions = {"c++ -o main main.cpp"}}};

  ASSERT_EQ(expected, actual);
}

TEST(Parser, ItLooksUpMacros) {
  auto tokens = lex("CC := cc; main <- main.c { $(CC) -o main main.c; }");
  auto actual = parse(std::move(tokens)).rules;

  auto expected =
      std::set<Rule, std::less<>>{{.target = "main",
                                   .prereqs = {"main.c"},
                                   .actions = {"cc -o main main.c"}}};

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

TEST(Parser, ItCanFillStencils) {
  auto tokens = lex("[*.o] <- [*.c] { cc -c $<; } [main.o] <- [main.c]; main "
                    "<- main.o { cc -o $@ $<; }");
  auto actual = parse(std::move(tokens)).rules;

  auto expected = std::set<Rule, std::less<>>{
      {.target = "main.o", .prereqs = {"main.c"}, .actions = {"cc -c main.c"}},
      {.target = "main",
       .prereqs = {"main.o"},
       .actions = {"cc -o main main.o"}}};

  ASSERT_EQ(actual, expected);
}

int
main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
