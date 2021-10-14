.POSIX:
.SUFFIXES: .cpp.o

CXX = /opt/gcc/GCC-11.2.0/bin/c++
CXXFLAGS = -O3 -Wall -Werror -Wpedantic           \
           -I/opt/gcc/GCC-11.2.0/include          \
	   -I/opt/include -std=c++20 -g

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

fab: fab.o main.o
	$(CXX) $(CXXFLAGS) -o $@ fab.o main.o

check: unit integration

tidy:
	clang-tidy fab.cpp main.cpp -- -I/opt/gcc/GCC-11.2.0/include

integration: testrunner fab
	cd integration && python3 integration.py

unit: testrunner
	./testrunner

testrunner: testrunner.o fab.o
	$(CXX) $(CXXFLAGS) -o $@ testrunner.o fab.o -L/opt/lib -lgtest -lpthread

clean:
	rm -rf main.o fab.o testrunner.o fab testrunner

main.o: main.cpp
fab.o: fab.cpp fab.h
testrunner.o: testrunner.cpp
