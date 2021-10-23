.POSIX:
.SUFFIXES: .cpp.o

CXX = /opt/gcc/GCC-11.2.0/bin/c++
CXXFLAGS = -Wall -Werror -Wpedantic               \
            -fsanitize=address                    \
           -I/opt/gcc/GCC-11.2.0/include          \
	   -I/opt/include -std=c++20 -g

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

fab: fab.o main.o
	$(CXX) $(CXXFLAGS) -o $@ fab.o main.o

check: unit accept

tidy:
	clang-tidy fab.cpp main.cpp -- -I/opt/gcc/GCC-11.2.0/include

accept: testrunner fab
	cd integration && python3 integration.py

unit: testrunner
	./testrunner

testrunner: testrunner.o fab.o fab.h
	$(CXX) $(CXXFLAGS) -o $@ testrunner.o fab.o -L/opt/lib -lgtest -lpthread

clean:
	rm -rf main.o fab.o testrunner.o fab testrunner

main.o: main.cpp
fab.o: fab.cpp fab.h
testrunner.o: testrunner.cpp
