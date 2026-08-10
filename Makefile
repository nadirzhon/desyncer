# DESYNCER — сборка без внешних зависимостей (только stdlib + POSIX).
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS  ?=

BIN     := desyncer
SRCS    := $(wildcard src/*.cpp)
OBJS    := $(SRCS:.cpp=.o)

.PHONY: all clean run-dry test

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Быстрая проверка: показать пробы, ничего не отправляя.
run-dry: $(BIN)
	./$(BIN) example.com --dry-run

clean:
	rm -f $(OBJS) $(BIN)
