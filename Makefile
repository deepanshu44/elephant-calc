CC ?= gcc
FT_CFLAGS = $(shell pkg-config --cflags freetype2 2>/dev/null)
FT_LIBS = $(shell pkg-config --libs freetype2 2>/dev/null || echo "-lfreetype")
CFLAGS ?= -Wall -Wextra -O3 -std=c11 $(FT_CFLAGS)
LIBS = -lX11 -lcairo -lm -pthread $(FT_LIBS)

SRCS = main.c parser.c format.c storage.c convert.c brackets.c undo.c timezone.c autocomplete.c currency.c
OBJS = $(SRCS:.c=.o)
TARGET = elephant-calc

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c elephant.h brackets.h undo.h timezone.h autocomplete.h currency.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

benchmark: $(TARGET)
	./$(TARGET) --benchmark

.PHONY: all clean benchmark
