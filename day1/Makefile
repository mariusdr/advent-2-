CC ?= /usr/bin/cc 
CFLAGS := -Werror -Wall -Wunused -std=c2x
TARGET := cat 

SRC_PATH := src
BUILD_PATH := build
SRCS := $(foreach x, $(SRC_PATH), $(wildcard $(addprefix $(x)/*,.c)))
OBJS := $(addprefix $(BUILD_PATH)/, $(addsuffix .o, $(notdir $(basename $(SRCS)))))

default: all 

$(BUILD_PATH)/$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(CFLAGS)

$(BUILD_PATH)/%.o: $(SRC_PATH)/%.c 
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: all 
all: $(BUILD_PATH)/$(TARGET)

.PHONY: clean 
clean:
	@rm -f $(BUILD_PATH)/*