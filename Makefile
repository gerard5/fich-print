CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LIBS ?=

TARGET := fichero
SRC_DIR := src

SRC := main.c printer.c aiyin_io_fd.c aiyin_io_usb.c aiyin_ops.c niimbot_ops.c font_pcf.c

# libusb-1.0
ifeq ($(shell pkg-config --exists libusb-1.0 && echo yes),yes)
    CFLAGS += $(shell pkg-config --cflags libusb-1.0) -DHAVE_LIBUSB
    LIBS += $(shell pkg-config --libs libusb-1.0)
endif

# DataMatrix (libdmtx)
ifeq ($(shell pkg-config --exists libdmtx && echo yes),yes)
    CFLAGS += $(shell pkg-config --cflags libdmtx) -DHAVE_LIBDMTX
    LIBS += $(shell pkg-config --libs libdmtx)
endif

SRC_FILES := $(addprefix $(SRC_DIR)/, $(SRC))
HDR_FILES := $(wildcard $(SRC_DIR)/*.h)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC_FILES) $(HDR_FILES)
	$(CC) $(CFLAGS) -o $@ $(SRC_FILES) $(LIBS)

clean:
	rm -f $(TARGET)
