CC=gcc
CFLAGS=-Wall -g
LIBS=-lcurl -ljansson

TARGET=main

SOURCES=main.c
OBJECTS=$(SOURCES:.c=.o)

.PHONE: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)
