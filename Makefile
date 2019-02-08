.PHONY: all clean
CUR_PATH := $(CURDIR)
BIN_PATH := $(CUR_PATH)/bin
$(shell if [ ! -d $(BIN_PATH) ]; then mkdir $(BIN_PATH); fi)

INC_PATH := $(CUR_PATH)/inc
CLI_SRC_PATH := $(CUR_PATH)/src/client
SER_SRC_PATH := $(CUR_PATH)/src/server
OBJ_PATH := $(SRC_PATH)

CLI_CFILES := $(wildcard $(CLI_SRC_PATH)/*)
CLI_CFILES += $(CUR_PATH)/src/tftp_udp_com.c
SER_CFILES := $(wildcard $(SER_SRC_PATH)/*)
SER_CFILES += $(CUR_PATH)/src/tftp_udp_com.c
SER_OBJS := $(patsubst %.c,%.o,$(CFILES))

all:server client
	
server:
	$(CC) -Wall -g -I$(INC_PATH) $(SER_CFILES) -o $(BIN_PATH)/$@
client:
	$(CC) -Wall -g -I$(INC_PATH) $(CLI_CFILES) -o $(BIN_PATH)/$@
clean:
	rm -rf $(BIN_PATH)
