#include <iostream>

#include <gtest/gtest.h>

#include "fab.h"

TEST(Lexer, ItRecognizesArrows) {
  const auto actual = lex("<-");

  const auto expected = std::vector<Token>{Token::make<Token::Ty::Arrow>(),
                                           Token::make<Token::Ty::Eof>()};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesIdentifiers) {
  const auto actual = lex("foo;");

  const auto expected = std::vector<Token>{
      Token::make<Token::Ty::Iden>(std::string_view{"foo"}),
      Token::make<Token::Ty::SemiColon>(), Token::make<Token::Ty::Eof>()};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesBraces) {
  const auto actual = lex("{}");

  const auto expected = std::vector<Token>{Token::make<Token::Ty::LBrace>(),
                                           Token::make<Token::Ty::RBrace>(),
                                           Token::make<Token::Ty::Eof>()};

  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesAFullRule) {
  const auto actual = lex("foo <- bar { baz; }");

  const auto expected = std::vector<Token>{
      Token::make<Token::Ty::Iden>("foo"), Token::make<Token::Ty::Arrow>(),
      Token::make<Token::Ty::Iden>("bar"), Token::make<Token::Ty::LBrace>(),
      Token::make<Token::Ty::Iden>("baz"), Token::make<Token::Ty::SemiColon>(),
      Token::make<Token::Ty::RBrace>(),    Token::make<Token::Ty::Eof>()};

  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItRecognizesMacros) {
  const auto actual = lex("$(CC)");

  const auto expected = std::vector<Token>{Token::make<Token::Ty::Macro>("CC"),
                                           Token::make<Token::Ty::Eof>()};
  ASSERT_EQ(expected, actual);
}

TEST(Lexer, ItExpectsValidTokens) {
  ASSERT_THROW(lex("<="), std::runtime_error);
}

TEST(Lexer, ItRecognizesGenericRules) {
  const auto actual = lex("[*.o] <- [*.c] { cc -o $@ $<; }");

  const auto expected = std::vector<Token>{
      Token::make<Token::Ty::GenericRule>("o"),
      Token::make<Token::Ty::Arrow>(),
      Token::make<Token::Ty::GenericRule>("c"),
      Token::make<Token::Ty::LBrace>(),
      Token::make<Token::Ty::Iden>("cc"),
      Token::make<Token::Ty::Iden>("-o"),
      Token::make<Token::Ty::TargetAlias>(),
      Token::make<Token::Ty::PrereqAlias>(),
      Token::make<Token::Ty::SemiColon>(),
      Token::make<Token::Ty::RBrace>(),
      Token::make<Token::Ty::Eof>(),
  };

  ASSERT_EQ(expected, actual);
}

TEST(Parser, ItParsesARule) {
  auto tokens = lex("main <- main.cpp lib.cpp { c++ -o main main.cpp; }");
  const auto actual = parse(std::move(tokens)).rules;

  const auto expected =
      std::set<Rule, std::less<>>{{.target = "main",
                                   .prereqs = {"main.cpp", "lib.cpp"},
                                   .actions = {"c++ -o main main.cpp"}}};

  ASSERT_EQ(expected, actual);
}

TEST(Parser, ItLooksUpMacros) {
  auto tokens = lex("CC := cc; main <- main.c { $(CC) -o main main.c; }");
  const auto actual = parse(std::move(tokens)).rules;

  const auto expected =
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

TEST(Parser, ItCanFillGenericRules) {
  auto tokens = lex("[*.o] <- [*.c] { cc -c $<; } [main.o] <- [main.c]; main "
                    "<- main.o { cc -o $@ $<; }");
  const auto actual = parse(std::move(tokens)).rules;

  const auto expected = std::set<Rule, std::less<>>{
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
