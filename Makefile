all: write-read-test

write-read-test: main.cpp
	g++ -std=c++23 -o write-read-test main.cpp
