`fab` let's you specify dependencies and run commands similar to
[`make(1)`][make].  It is implemented in C++20 and (where possible):
  - exposes a minimal interface to users (really just `lex`, `parse`, and a few
   types)
  - uses immutable values and references
  - avoids allocation (ie. `string_view` instead of `std::string`)
  - uses [ranges]
  - constrains generics with [concepts]
  - uses stdlib algorithms instead of manual iteration

Much like `make(1)`, `fab` supplies three main constructs: targets,
prerequisites, and actions -- a _rule_ is a combination these constructs.

A `Fabfile` that builds a C program from a `main.c` and `lib.c` file might look
something like this.

```
main <- main.o lib.o {
  cc -o main main.o lib.o;
}

main.o <- main.c {
  cc -c main.c;
}

lib.o {
  cc -c lib.c;
}
```

Running `fab` on this file is as simple as

```
% fab

```

Like `make(1)`, `fab` lets you assign values to identifiers. These assignments
are called macros.

```
CC := /opt/bin/gcc;

main <- main.o lib.o {
  $(CC) -o main main.o lib.o;
}

main.o <- main.c {
  $(CC) -c main.c;
}

lib.o <- lib.c {
  $(CC) -c lib.c;
}
```

In addition to user defined macros, `fab` supplies a few familiar built-in
macros as well.
```
CC := /opt/bin/gcc;

main <- main.o lib.o {
  $(CC) -o $@ $<;
}

main.o <- main.c {
  $(CC) -c $<;
}

lib.o <- lib.c {
  $(CC) -c $<;
}
```

Fab also allows generic rules -- similar to make's inference rules. A generic
rule is created by providing a basic outline of the rule -- called a stencil --
and then filling in that outline -- called a fill. The general syntax is
demonstrated below.
```
CC := /opt/bin/gcc;

main <- main.o lib.o {
  $(CC) -o $@ $<;
}

[main.o] <- [main.c];

[lib.o] <- [lib.c];

[*.o] <- [*.c] {
  cc -c $<;
}
```

### TODO
- [x] allow multiple prerequisites in each rule
- [x] allow rules without dependencies
- [x] allow multiple identifiers in each macro definition
- [x] support default rule
- [x] allow multiple actions in a single action block
- [x] allow inference rules


[concepts]: https://en.cppreference.com/w/cpp/language/constraints
[make]: https://pubs.opengroup.org/onlinepubs/009695299/utilities/make.html
[ranges]: https://en.cppreference.com/w/cpp/header/ranges
