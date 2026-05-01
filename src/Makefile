CC=gcc
CFLAGS=-fPIC -shared -ldl
TARGET=monohook.so

all: $(TARGET)

$(TARGET): monohook.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
