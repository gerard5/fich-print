CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LIBS ?=

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

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

TARGET = fichero

SRC_DIR := src
BUILD_DIR := build

SRC := main.c printer.c font_pcf.c
SRC += aiyin/aiyin_ops.c aiyin/aiyin_io_fd.c aiyin/aiyin_io_usb.c
SRC += niimbot/niimbot_ops.c

SRC_FILES := $(addprefix $(SRC_DIR)/, $(SRC))
OBJS := $(addprefix $(BUILD_DIR)/, $(SRC:.c=.o))
BUILD_SUBDIRS := $(filter-out $(BUILD_DIR)/, $(sort $(dir $(OBJS))))

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)
	rmdir $(BUILD_SUBDIRS) $(BUILD_DIR)
