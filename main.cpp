#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>

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

    const auto &deps = top.get().dependencies;
    if (std::ranges::all_of(
            deps, [&visited](auto d) { return visited.contains(d); }) ||
        std::ranges::all_of(deps,
                            [&env](auto d) { return env.is_terminal(d); })) {
      detail::eval(top);
      visited.insert(top.get().target);
      stack.pop();
    } else {
      for (auto d : deps | std::views::filter([&env](auto d) {
                      return !env.is_terminal(d);
                    }) | std::views::reverse) {
        stack.push(env.get(d));
      }
    }
  }
}
} // namespace

int
main(int argc, char **argv) {
  auto errout = [program = std::as_const(argv[0])](auto msg) -> int {
    std::cerr << program << ": error: " << msg << std::endl;
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
      return errout("usuage: fab [-f <Fabfile>] target");
    }
  }

  if (!std::filesystem::exists(fabfile)) {
    return errout("Fabfile not found.");
  }

  if (optind >= argc) {
    return errout("specify target to build");
  }

  const std::string target = argv[optind];
  auto handle = std::ifstream{fabfile};

  if (!handle.is_open()) {
    return errout("could not open Fabfile.");
  }

  auto buf = std::stringstream{};
  buf << handle.rdbuf();
  auto program = std::string{std::move(buf.str())};

  try {
    auto env = parse(lex(program));
    eval_rule(env, env.get(target));
  } catch (const std::runtime_error &exn) {
    return errout(exn.what());
  }
}
