all: write-read-test

write-read-test: main.cpp
	g++ -std=c++23 -O3 -o write-read-test main.cpp
