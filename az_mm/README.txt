This directory contains the Azul memory management kernel module.

USE:

When loaded, the device /dev/azul will be created.  One opens it and
performs ioctls (actually, unlocked_ioctls) on it to achieve the
equivalent of system calls.

BUILDING:

To build the memory management module, you should use "make -f
Makefile.standalone", specifying KERNELDIR appropriately to point to the
location of your kernel source.  The default is set to build
from within a kernel source tree hierarchy where this code lives
under .../src/azul/mm.  Hence KERNELDIR is set to ../../../src.
If you have the module somewhere else, you can point to the appropriate
kernel sources using "make KERNELDIR=/path/to/my/kernel/source -f
Makefile.standalone".  For the case where you are building against
the installed, running kernel, a useful command is probably "make
KERNELDIR=/lib/modules/`uname -r`/build -f Makefile.standalone".

- bog
  05/26/2010

