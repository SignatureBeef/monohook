CC ?= cc
UNAME_S := $(shell uname -s)

COMMON_CFLAGS := -fPIC
COMMON_LDFLAGS :=

ifeq ($(UNAME_S),Darwin)
	TARGET := monohook.dylib
	DARWIN_ARCHS ?= arm64
	ARCH_FLAGS := $(foreach ARCH,$(DARWIN_ARCHS),-arch $(ARCH))
	COMMON_CFLAGS += $(ARCH_FLAGS)
	COMMON_LDFLAGS += $(ARCH_FLAGS)
	COMMON_LDFLAGS += -dynamiclib -undefined dynamic_lookup
else
	TARGET := monohook.so
	COMMON_LDFLAGS += -shared -ldl
endif

CFLAGS ?= $(COMMON_CFLAGS)
LDFLAGS ?= $(COMMON_LDFLAGS)

all: $(TARGET)

$(TARGET): monohook.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f monohook.so monohook.dylib
