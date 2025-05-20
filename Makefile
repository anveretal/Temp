CC = gcc
CFLAGS = -Wall -Wpedantic -Wpointer-arith -Wendif-labels -Wmissing-format-attribute \
         -Wimplicit-fallthrough=3 -Wcast-function-type -Wshadow=compatible-local -Wformat-security \
		 -I./src/include

.PHONY: all clean

all: install libconfig liblogger greeting proxy

install:
	mkdir -p install install/plugins

libconfig: src/include/config.h src/config.c | install
	$(CC) $(CFLAGS) -c src/config.c -o install/config.o
	ar rcs install/libconfig.a install/config.o

# new
liblogger: src/include/logger.h src/logger.c src/include/my_time.h src/my_time.c | install
	$(CC) $(CFLAGS) -fPIC -shared src/logger.c src/my_time.c -o install/liblogger.so

# new
libtime: src/include/my_time.h src/my_time.c | install
	$(CC) $(CFLAGS) -fPIC -shared src/my_time.c -o install/libtime.so.1.0
	ln -sf libtime.so.1.0 install/libtime.so



greeting: plugins/greeting/greeting.c src/include/master.h | install/plugins
	$(CC) $(CFLAGS) -fPIC -shared plugins/greeting/greeting.c -o install/plugins/greeting.so
#-undefined dynamic_lookup   -- for MacOS -- necessarily
#-Wl,--allow-shlib-undefined -- for Linux -- optionally

proxy: src/include/master.h install/libconfig.a install/liblogger.so src/master.c | install
	$(CC) $(CFLAGS) -rdynamic src/master.c -o install/proxy -L./install -lconfig -llogger -ldl

clean:
	rm -rf install