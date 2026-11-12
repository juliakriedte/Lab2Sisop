TARGET       = disk-test
INSTALL_PATH = usr/bin

ROOT_DIR  = ../..
TOOLCHAIN = $(ROOT_DIR)/output/host/bin
OVERLAY   = $(ROOT_DIR)/overlay

TRIPLET = $(TOOLCHAIN)/i686-buildroot-linux-gnu
CC      = $(TRIPLET)-gcc

default: $(OVERLAY)/$(INSTALL_PATH)/$(TARGET)

$(OVERLAY)/$(INSTALL_PATH)/$(TARGET): $(OVERLAY)/$(INSTALL_PATH) $(TARGET).c
	@$(CC) $(TARGET).c -o $@ -O2

$(OVERLAY)/$(INSTALL_PATH):
	@mkdir -p $@

clean:
	@rm -f $(OVERLAY)/$(INSTALL_PATH)/$(TARGET)
