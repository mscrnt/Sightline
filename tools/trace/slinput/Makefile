# Sightline deterministic input plugin
CC      ?= gcc
CFLAGS  ?= -O2 -fPIC -Wall -Wextra -Wno-unused-parameter
INCLUDE := -I/usr/include/mupen64plus
TARGET  := slinput.so

$(TARGET): slinput.c
	$(CC) $(CFLAGS) $(INCLUDE) -shared -o $@ $< -ldl

clean:
	$(RM) $(TARGET)

.PHONY: clean
