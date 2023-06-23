#22/05/2023
EXECUTABLE=webserver
CC=g++
OBJ_PATH=.
ifndef OS
OS=$(shell uname)
endif

ifeq ($(OS),Darwin)
LOCAL_PATH=..
GAME_PATH=.
SEP=/
CN=rm -rf 
CFLAGS=-c -Wall -F/Library/Frameworks -std=c++11
LDFLAGS=-F/Library/Frameworks -lm -lpthread 
else ifeq ($(OS),Linux)
LOCAL_PATH=..
GAME_PATH=.
SEP=/
CN=rm -rf
CFLAGS=-c -Wall -O3
LDFLAGS=-lm -lpthread 
else ifeq ($(OS),Windows_NT)
LOCAL_PATH=..
GAME_PATH=.
SEP=\\
CN=del /q
CFLAGS=-c -Wall -O3
LDFLAGS=-lm -lpthread -lws2_32 -liphlpapi
endif

INCFLAGS=-I$(LOCAL_PATH) -I$(GAME_PATH) 

SOURCES=$(GAME_PATH)$(SEP)mainwebserver.cpp 
SOURCES+=$(LOCAL_PATH)$(SEP)axishttpsock.cpp 

OBJECTS=$(SOURCES:.cpp=.o)
OBJECTS+=$(CSOURCES:.c=.o)

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(addprefix $(OBJ_PATH)$(SEP),$(notdir $(OBJECTS))) -o $@ $(LDFLAGS)

.cpp.o:
	$(CC) $(INCFLAGS) $(CFLAGS) $< -o $(addprefix $(OBJ_PATH)$(SEP),$(notdir $@))
.c.o:
	$(CC) $(INCFLAGS) $(CFLAGS) $< -o $(addprefix $(OBJ_PATH)$(SEP),$(notdir $@))
	
clean:
	$(CN) $(OBJ_PATH)$(SEP)*.o
