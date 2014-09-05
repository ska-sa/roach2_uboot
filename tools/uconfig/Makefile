# compiler flags, -Wall is just the right thing to do 
CFLAGS += -Wall
CFLAGS += -O2

KATCP ?= ../../../katcp_devel/katcp

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

# amount of debugging logic to enable 
#CFLAGS += -DDEBUG=4
#CFLAGS += -DDEBUG

# what compiler to use 
CC = $(CROSS_COMPILE)gcc

EXE = uconfig
SRC = $(EXE).c

$(EXE): $(SRC)
	$(CC) $(CFLAGS) $(INC) -s -o $@ $^ $(LIB)

clean: 
	$(RM) -f $(EXE) *.o

