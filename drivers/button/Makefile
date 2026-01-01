# SPDX-License-Identifier: GPL-2.0
#
# Makefile for BBB Flagship Button Driver (out-of-tree build)
#
# Usage:
#   On BBB (native build):
#     make
#
#   Cross-compile from host:
#     make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KERNEL_SRC=/path/to/linux
#
#   Clean:
#     make clean

# Module name (without .ko extension)
obj-m := bbb_flagship_button_combined.o
bbb_flagship_button_combined-y := bbb_flagship_button.o bbb_flagship_button_chardev.o


# Kernel source directory
# On BBB, this points to the kernel headers package
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

# Build directory (current directory)
PWD := $(shell pwd)

# Default target: build the module
all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

# Clean build artifacts
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

# Install the module (copies to /lib/modules/.../extra/)
install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

# Show help
help:
	@echo "BBB Flagship Button Driver Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build the kernel module (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  install - Install module to /lib/modules/"
	@echo ""
	@echo "Variables:"
	@echo "  KERNEL_SRC - Path to kernel source/headers"
	@echo "  ARCH       - Target architecture (arm for BBB)"
	@echo "  CROSS_COMPILE - Cross compiler prefix"
	@echo ""
	@echo "Examples:"
	@echo "  Native build on BBB:"
	@echo "    make"
	@echo ""
	@echo "  Cross-compile:"
	@echo "    make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \\"
	@echo "         KERNEL_SRC=/path/to/linux-source"

.PHONY: all clean install help

