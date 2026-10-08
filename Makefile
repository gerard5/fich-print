CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LIBS ?=

TARGET := fichero
SRC_DIR := src

SRC := main.c fichero.c font_pcf.c
LIBS += -ldmtx

SRC_FILES := $(addprefix $(SRC_DIR)/, $(SRC))
HDR_FILES := $(wildcard $(SRC_DIR)/*.h)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC_FILES) $(HDR_FILES)
	$(CC) $(CFLAGS) -o $@ $(SRC_FILES) $(LIBS)

clean:
	rm -f $(TARGET)
