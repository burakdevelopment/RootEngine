CXX      ?= g++
EXE      ?= Root
SRC      := src/main.cpp src/position.cpp src/attacks.cpp src/evaluation.cpp \
            src/search.cpp src/uci.cpp src/tt.cpp src/book.cpp src/nnue.cpp \
            src/datagen.cpp

WFLAGS   := -Wall -Wextra
CXXFLAGS := -O3 -std=c++20 -march=x86-64-v2 -funroll-loops -flto -DNDEBUG $(WFLAGS)
LDFLAGS  :=

ifeq ($(OS),Windows_NT)
    LDFLAGS += -static -static-libgcc -static-libstdc++
else
    LDFLAGS += -lpthread
endif

all:
	$(CXX) $(CXXFLAGS) -o $(EXE) $(SRC) $(LDFLAGS)

native:
	$(CXX) -O3 -std=c++20 -march=native -funroll-loops -flto -DNDEBUG $(WFLAGS) -o $(EXE) $(SRC) $(LDFLAGS)

debug:
	$(CXX) -O0 -g -std=c++20 $(WFLAGS) -o $(EXE)_dbg $(SRC) $(LDFLAGS)

clean:
	rm -f $(EXE) $(EXE).exe $(EXE)_dbg $(EXE)_dbg.exe

.PHONY: all native debug clean
