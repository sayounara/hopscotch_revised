CPPSRCS		= ssalloc.cpp test_hopd.cpp cpp_framework.cpp
TARGET		= test_hopscotch
    
ROOT 		?= .
LIBSSMEM := $(ROOT)/external

CPP			= g++

CPPFLAGS	=-std=c++11 -O3 -m64 -DNDEBUG -DINTEL64 -D_REENTRANT -lrt -pthread -I$(ROOT)/include -I$(LIBSSMEM)/include
LFLAGS		=-std=c++11 -O3 -m64 -DNDEBUG -DINTEL64 -D_REENTRANT -lrt -pthread -I$(ROOT)/include -L$(LIBSSMEM)/lib -lsspfd_x86_64 -lssmem_x86_64

OBJS		= $(CPPSRCS:.cpp=.o)

all: $(TARGET)

test_hopd.o:
	$(CPP) $(CPPFLAGS) -c ./test/test_hopd.cpp -g
ssalloc.o:
	$(CPP) $(CPPFLAGS) -c ./test/ssalloc.cpp -g
cpp_framework.o:
	$(CPP) $(CPPFLAGS) -c ./framework/cpp_framework.cpp -g

$(TARGET): $(OBJS)
	$(CPP) $(LFLAGS) $(OBJS) -o $(TARGET) -g

clean:
	rm -f $(OBJS) $(TARGET)

depend:
	mkdep $(SRCS)