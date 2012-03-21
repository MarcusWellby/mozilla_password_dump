#NSPR_INCLUDE_DIR=`pkg-config --cflags-only-I nspr | sed 's@-I@@')`
#NSPR_LIB_DIR=$(pkg-config --libs-only-L nspr | sed 's@-L@@')  \

# -lplds4 -lplc4 -lnspr4 -ldl -lrt -lsocket -ldl -lm
LDFLAGS=-Wl,-rpath,/usr/lib/nss -Wl,-rpath,/usr/lib/nspr -Wl,--rpath -Wl,/usr/local/lib -Lini

all:
	cd ini && make
	gcc -Wall -Iini -I/usr/include/nss -I/usr/include/nspr -c mozilla_password_dump.c
	gcc  $(LDFLAGS) -o mozilla_password_dump mozilla_password_dump.o -lnspr4 -lnss3 -lsqlite3 -liniparser
