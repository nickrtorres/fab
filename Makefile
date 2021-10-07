.POSIX:
.SUFFIXES: .cpp.o

CXX = /opt/gcc/GCC-11.2.0/bin/c++
CXXFLAGS = -Wall -Werror -Wpedantic               \
           -I/opt/gcc/GCC-11.2.0/include          \
	   -I/opt/include -L/opt/lib -std=c++20 -g

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

main: fab.o main.o
	$(CXX) $(CXXFLAGS) -o $@ fab.o main.o

check: testrunner
	./testrunner

testrunner: testrunner.o fab.o
	$(CXX) $(CXXFLAGS) -o $@ -lgtest testrunner.o fab.o

clean:
	rm -rf main.o fab.o testrunner.o main testrunner

main.o: main.cpp
fab.o: fab.cpp fab.h
testrunner.o: testrunner.cpp
