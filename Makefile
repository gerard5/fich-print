SRC := main.c printer.c font_pcf.c
SRC += aiyin/aiyin_ops.c aiyin/aiyin_io_fd.c aiyin/aiyin_io_usb.c
SRC += niimbot/niimbot_ops.c

TARGET := fichero

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LIBS ?=

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRC_DIR := src
BUILD_DIR := build

SRC_FILES := $(addprefix $(SRC_DIR)/, $(SRC))
OBJS := $(addprefix $(BUILD_DIR)/, $(SRC:.c=.o))
DEPS := $(OBJS:.o=.d)
BUILD_SUBDIRS := $(filter-out $(BUILD_DIR)/, $(sort $(dir $(OBJS))))

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

ifeq ($(V),1)
  log =
  Q =
else
  log = $(info $(indent)$(cmd_$(1))$(2))
  Q = @

  cmd_CC      = CC      
  cmd_LD      = LD      
  cmd_INSTALL = INSTALL 
  cmd_REMOVE  = REMOVE  
  cmd_CLEAN   = CLEAN   
endif

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(call log,LD,$@)
	$(Q)$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(call log,CC,$<)
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

install: $(TARGET)
	$(call log,INSTALL,$(TARGET))
	$(Q)install -d $(DESTDIR)$(BINDIR)
	$(Q)install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	$(call log,REMOVE,$(TARGET))
	$(Q)rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	$(call log,CLEAN,objects)
	$(Q)rm -f $(TARGET) $(OBJS) $(DEPS)
	$(Q)rmdir $(BUILD_SUBDIRS) $(BUILD_DIR) 2>/dev/null || true

-include $(DEPS)
