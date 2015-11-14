CPPSRCS		= ssalloc.cpp test_hopd.cpp cpp_framework.cpp
TARGET		= test_hopscotch
VER_FLAGS = -D_GNU_SOURCE

ROOT 		?= .
LIBSSMEM := $(ROOT)/external
ifeq ($(VERSION),MIC) 
VER_FLAGS +=-DMIC
#VER_FLAGS +=-std=c++0x#-std=c++11,g++_mic不能识别
CPP = g++_mic
else
UNAME := $(shell uname -n)
UNAME = chan-Z9NA-D6C
VER_FLAGS += -DXEON
CPP = g++
PLATFORM_NUMA = 1
ifeq ($(UNAME), chan-Z9NA-D6C)
VER_FLAGS += -DCHAN_INTEL
endif
ifeq ($(UNAME), localhost.localdomain)
VER_FLAGS += -DR730
endif
endif
#CPP			= g++

CPPFLAGS	=  -O3 -m64 -DNDEBUG -DINTEL64 -D_REENTRANT  -lrt -pthread -I$(ROOT)/include -I$(LIBSSMEM)/include
LFLAGS		=  -O3 -m64 -DNDEBUG -DINTEL64 -D_REENTRANT  -lrt -pthread -I$(ROOT)/include #-L$(LIBSSMEM)/lib -lsspfd_x86_64 -lssmem_x86_64

ifeq ($(PLATFORM_NUMA),1) #give PLATFORM_NUMA=1 for NUMA
LFLAGS += -lnuma
VER_FLAGS += -DPLATFORM_NUMA
VER_FLAGS += -std=c++11
endif 
ifdef DEBUG
VER_FLAGS += -g
endif

ifeq ($(KMP_AFFINITY),COMPACT) 
VER_FLAGS += -DCOMPACT
endif
ifeq ($(KMP_AFFINITY),SCATTER) 
VER_FLAGS += -DSCATTER
endif
ifeq ($(KMP_AFFINITY),BALANCED) 
VER_FLAGS += -DBALANCED
endif
ifeq ($(KMP_AFFINITY),NO_SET)
VER_FLAGS += -DNO_SET_CPU
endif

OBJS		= $(CPPSRCS:.cpp=.o)

all: $(TARGET)

test_hopd.o:
	$(CPP) $(VER_FLAGS) $(CPPFLAGS) -c ./test/test_hopd.cpp
ssalloc.o:
	$(CPP) $(VER_FLAGS) $(CPPFLAGS) -c ./test/ssalloc.cpp 
cpp_framework.o:
	$(CPP) $(VER_FLAGS) $(CPPFLAGS) -c ./framework/cpp_framework.cpp 

$(TARGET): $(OBJS)
	$(CPP) $(VER_FLAGS) $(LFLAGS) $(OBJS) -o $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

depend:
	mkdep $(SRCS)