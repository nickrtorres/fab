#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <tuple>

#include "fab.h"

bool isTerminal(const std::string_view &dependent) { return true; }

class RuleRunner {
  virtual void dispatch(const Rule &) const = 0;

public:
  virtual ~RuleRunner() = default;
  void operator()(const Rule &rule) { dispatch(rule); }
};

struct TargetOutOfDate final : public RuleRunner {
  void dispatch(const Rule &rule) const override {
    std::cerr << rule.action << std::endl;
    system(rule.action.c_str());
  }
};

using TargetDoesNotExist = TargetOutOfDate;

struct NoOpRunner : public RuleRunner {
  void dispatch(const Rule &rule) const override {
    std::cerr << "already up to date" << std::endl;
  }
};

namespace factory {
std::unique_ptr<RuleRunner> get_runner(const Rule &rule) {
  if (!std::filesystem::exists(rule.target)) {
    return std::unique_ptr<RuleRunner>{new TargetDoesNotExist{}};
  }

  if (isTerminal(rule.dependency)) {
    if (std::filesystem::last_write_time(rule.target) <
        std::filesystem::last_write_time(rule.dependency)) {
      return std::unique_ptr<RuleRunner>{new TargetOutOfDate{}};
    } else {
      return std::unique_ptr<RuleRunner>{new NoOpRunner{}};
    }
  } else {
    // TODO
    throw std::runtime_error("not implemented");
  }
}

} // namespace factory

int main() {
  const std::string FABFILE = "Fabfile";

  if (!std::filesystem::exists(FABFILE)) {
    std::cerr << "Fabfile not found." << std::endl;
    return 1;
  }

  std::ifstream handle(FABFILE);

  if (!handle.is_open()) {
    return 1;
  }

  std::stringstream buf;
  buf << handle.rdbuf();
  std::string program = std::move(buf.str());

  auto tokens = Lexer(program).lex();
  auto env = Parser(tokens).parse();

  auto default_rule = env.default_rule();
  auto runner = factory::get_runner(default_rule);
  (*runner)(default_rule);
}
