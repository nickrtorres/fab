#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <tuple>
#include <unistd.h>
#include <utility>

#include "fab.h"

namespace {
namespace detail {
void
eval(const Rule &rule) {
  std::cerr << rule.action << std::endl;
  system(rule.action.c_str());
}
} // namespace detail

template <typename T>
using Ref = std::reference_wrapper<T>;

// traverse through dependency tree (depth first) until all a rules descendents
// are satisfied.
void
eval_rule(const Environment &env, const Rule &rule) {
  auto stack = std::stack<Ref<const Rule>>{};
  auto visited = std::set<std::string_view>{};

  stack.push(rule);

  while (!stack.empty()) {
    auto top = stack.top();

    if (visited.contains(top.get().dependency) ||
        env.is_terminal(top.get().dependency)) {
      detail::eval(top);
      visited.insert(top.get().target);
      stack.pop();
    } else {
      stack.push(env.get(top.get().dependency));
    }
  }
}
} // namespace

int
main(int argc, char **argv) {
  auto fail = [program = std::as_const(argv[0])](std::string_view msg) -> int {
    std::cerr << program << ": " << msg << std::endl;
    return 1;
  };

  std::string fabfile = "Fabfile";
  int ch = {};
  while ((ch = getopt(argc, argv, "f:")) != -1) {
    switch (ch) {
    case 'f':
      fabfile = optarg;
      break;
    case '?':
    default:
      return fail("usuage: fab [-f <Fabfile>] target");
    }
  }

  if (!std::filesystem::exists(fabfile)) {
    return fail("Fabfile not found.");
  }

  if (optind >= argc) {
    return fail("specify target to build");
  }

  const std::string target = argv[optind];
  std::ifstream handle(fabfile);

  if (!handle.is_open()) {
    return fail("could not open Fabfile.");
  }

  std::stringstream buf;
  buf << handle.rdbuf();
  std::string program = std::move(buf.str());

  auto env = parse(lex(program));
  eval_rule(env, env.get(target));
}
