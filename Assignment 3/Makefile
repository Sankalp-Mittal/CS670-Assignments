# Makefile for Assignment 3

CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra
LIBS = -lboost_system -lpthread

all: p0 p1 p2

p0: pB.cpp common.hpp
	$(CXX) -DROLE_p0 $(CXXFLAGS) -I. pB.cpp -o p0 $(LIBS)

p1: pB.cpp common.hpp
	$(CXX) -DROLE_p1 $(CXXFLAGS) -I. pB.cpp -o p1 $(LIBS)

p2: p2.cpp common.hpp
	$(CXX) $(CXXFLAGS) -I. p2.cpp -o p2 $(LIBS)

test_data:
	python3 gen_test_data.py 10 20 5 8

check:
	python3 checker.py

clean:
	rm -f p0 p1 p2
	rm -rf data/p0_shares/*.txt data/p1_shares/*.txt
	rm -f data/*.txt

docker_build:
	docker-compose build

docker_run:
	docker-compose up

docker_clean:
	docker-compose down -v
	docker system prune -f

.PHONY: all test_data check clean docker_build docker_run docker_clean
