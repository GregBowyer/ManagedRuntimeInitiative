# Note: set these appropriately for your installation
INCDIR=/usr/include
LIBDIR=/lib64

azpmem: azpmem.c
	gcc -o azpmem azpmem.c -I$(INCDIR) -DAZNIX_API_VERSION=200 -L$(LIBDIR) -laznix -lpthread

clean: FORCE
	rm -f azpmem azpmem.o

FORCE:

