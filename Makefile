#############################################################################################
# Makefile
#############################################################################################
# G++ is part of GCC (GNU compiler collection) and is a compiler best suited for C++
CC=g++

# Compiler Flags: https://linux.die.net/man/1/g++
#############################################################################################
# -g: produces debugging information (for gdb)
# -Wall: enables all the warnings
# -Wextra: further warnings
# -Werror: treat warnings as errors
# -O: Optimizer turned on
# -std: use the C++ 14 standard
# -c: says not to run the linker
# -pthread: Add support for multithreading using the POSIX threads library. This option sets 
#           flags for both the preprocessor and linker. It does not affect the thread safety 
#           of object code produced by the compiler or that of libraries supplied with it. 
#           These are HP-UX specific flags.
#############################################################################################
CFLAGS=-g -Wall -Wextra -Werror -O -std=c++14 -pthread

rebuild: clean all
all: ./twmailer-server ./twmailer-client

clean:
	clear
	rm -f twmailer-*

./obj/myclient.o: myclient.cpp
	${CC} ${CFLAGS} -o obj/myclient.o myclient.cpp -c -lldap -llber

./obj/myserver.o: myserver.cpp
	${CC} ${CFLAGS} -o obj/myserver.o myserver.cpp -c -lldap -llber

./twmailer-server: ./obj/myserver.o
	${CC} ${CFLAGS} -o twmailer-server obj/myserver.o -lldap -llber

./twmailer-client: ./obj/myclient.o
	${CC} ${CFLAGS} -o twmailer-client obj/myclient.o -lldap -llber
