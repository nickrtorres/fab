#include <algorithm>
#include <cassert>
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
#include <type_traits>
#include <utility>

#include <unistd.h>

#include "fab.h"

namespace {
constexpr int CMD_OK = 0;

std::filesystem::file_time_type
last_write(std::string_view path) {
  std::error_code ec = {};
  auto time =
      std::filesystem::last_write_time({path.cbegin(), path.cend()}, ec);

  if (!ec) {
    return time;
  } else {
    if (std::filesystem::exists(path)) {
      auto target = std::string{path.cbegin(), path.cend()};
      throw std::runtime_error(
          target + "exists, but could not determine the last write time.");
    }

    // If -- for some other reason -- we couldn't open the file, then assume it
    // doesn't exist and report that the last write time was really long ago.
    // This allows for gmake `.PHONY' style targets.
    return std::filesystem::file_time_type::min();
  }
}

namespace detail {
void
run_system_cmd(const std::string &cmd) {
  std::cerr << cmd << std::endl;
  if (CMD_OK != system(cmd.c_str())) {
    throw std::runtime_error("could not run command: " + cmd);
  }
}

void
eval(const Rule &rule) {
  // `target' doesn't exist -- it must be out of date!
  if (!std::filesystem::exists(rule.target)) {
    run_system_cmd(rule.action);
  }

  // `target' exists without any dependencies -- it must be up to date!
  if (rule.dependencies.empty()) {
    return;
  }

  auto times = rule.dependencies | std::views::transform(last_write);
  auto max = *std::ranges::max_element(times.begin(), times.end());

  if (last_write(rule.target) < max) {
    run_system_cmd(rule.action);
  }
}
} // namespace detail

template <typename T>
using Ref = std::reference_wrapper<T>;

//   cases
//   ------------------------------------------
//   (1) current node is a leaf
//         - eval node; mark visited; pop
//   (2) current node has deps
//         - if all deps are visited
//             eval node; mark visited; pop
//         - else
//             filter unvisited nodes; push

template <typename T>
struct IsConstLValueReference {
  static const bool value =
      std::is_lvalue_reference<T>::value &&
      std::is_const<typename std::remove_reference<T>::type>::value;
};

void
eval_rule(const Environment &env, const Rule &rule) {
  auto stack = std::stack<Ref<const Rule>>{};
  auto visited = std::set<std::string_view>{};
  const auto utd = [&v = std::as_const(visited),
                    &e = std::as_const(env)](auto d) {
    static_assert(IsConstLValueReference<decltype(v)>::value);
    static_assert(IsConstLValueReference<decltype(e)>::value);
    return v.contains(d) || e.is_leaf(d);
  };
  const auto not_utd = std::not_fn(utd);

  stack.push(rule);

  while (!stack.empty()) {
    const auto &top = stack.top().get();
    const auto &deps = top.dependencies;

    if (visited.contains(top.target)) {
      stack.pop();
      continue;
    }

    if (deps.empty()) {
      assert(!visited.contains(top.target));
      detail::eval(top);
      visited.insert(top.target);
      stack.pop();
    } else {
      if (std::ranges::all_of(deps, utd)) {
        assert(!visited.contains(top.target));
        detail::eval(top);
        visited.insert(top.target);
        stack.pop();
      } else {
        for (auto d :
             deps | std::views::filter(not_utd) | std::views::reverse) {
          stack.push(env.get(d));
        }
      }
    }
  }
}
} // namespace

int
main(int argc, char **argv) {
  const auto errout = [&program = std::as_const(argv[0])](auto msg) -> int {
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
  const auto program = std::string{std::move(buf.str())};

  try {
    auto env = parse(lex(program));
    eval_rule(env, env.get(target));
  } catch (const std::runtime_error &exn) {
    return errout(exn.what());
  }
}
