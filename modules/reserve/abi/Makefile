# kapi_check - boot-time ABI preflight helper for gh_hugepage_reserve.
#
#   make            # host build, to compile-test / smoke-run on your own BTF
#   make gen        # regenerate kapi_abi.gen.h from kapi_abi.tsv
#   make android    # example aarch64 static build for the device (edit paths)
#
# The device build must be static aarch64 (no libbpf on device) and run from a
# root domain (Magisk/KernelSU) that can read /sys/kernel/btf/vmlinux.

CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -I.
LDLIBS ?= -lbpf -lelf -lz

kapi_check: kapi_check.c kapi_abi.gen.h
	$(CC) $(CFLAGS) kapi_check.c $(LDLIBS) -o $@

gen: kapi_abi.gen.h
kapi_abi.gen.h: kapi_abi.tsv gen_kapi.awk
	awk -f gen_kapi.awk $< > $@

# Example static aarch64 build. Adjust NDK + prebuilt static libbpf/elf/z paths.
NDK        ?= $(HOME)/android-ndk
TARGET     ?= aarch64-linux-android31
LIBBPF_DIR ?= $(HOME)/libbpf-android/arm64
android: kapi_abi.gen.h
	$(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/$(TARGET)-clang \
		-O2 -Wall -I. -I$(LIBBPF_DIR)/include kapi_check.c \
		$(LIBBPF_DIR)/lib/libbpf.a $(LIBBPF_DIR)/lib/libelf.a $(LIBBPF_DIR)/lib/libz.a \
		-static -o kapi_check.arm64

clean:
	rm -f kapi_check kapi_check.arm64

.PHONY: gen android clean
