UNAME_S = $(shell uname -s)
UNAME_M = $(shell uname -m)

OBJ = main.o loadelf.o parseMips.o helper.o profileMips.o githash.o branch_predictor.o saveState.o simCache.o
HOST =
ifeq ($(UNAME_M), x86_64)
	HOST = -march=native -flto
endif

ifeq ($(UNAME_S),Linux)
	CXX = g++ $(HOST)
	EXTRA_LD = -lunwind -lboost_system -lboost_program_options
endif

ifeq ($(UNAME_S),FreeBSD)
	CXX = CC -march=native
	EXTRA_LD = -L/usr/local/lib -lunwind -lboost_system -lboost_program_options
endif

ifeq ($(UNAME_S),Darwin)
	CXX = clang++ -march=native
	EXTRA_LD = -L/opt/local/lib -lboost_program_options-mt
endif

CXXFLAGS = -std=c++11 -g $(OPT)
LIBS =  $(EXTRA_LD) -lpthread

DEP = $(OBJ:.o=.d)
OPT = -O3 -g -fomit-frame-pointer -std=c++11
EXE = bpred_mips

.PHONY : all clean

all: $(EXE)

$(EXE) : $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) $(LIBS) -o $(EXE)

githash.cc : .git/HEAD .git/index
	echo "const char *githash = \"$(shell git rev-parse HEAD)\";" > $@

%.o: %.cc
	$(CXX) -MMD $(CXXFLAGS) -c $< 

%.o : %.s
	as $< -o $@

-include $(DEP)

clean:
	rm -rf $(EXE) $(OBJ) $(DEP)
