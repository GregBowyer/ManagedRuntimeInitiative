# Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License version 2 only, as published by
# the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
# details (a copy is included in the LICENSE file that accompanied this code).
#
# You should have received a copy of the GNU General Public License version 2
# along with this work; if not, write to the Free Software Foundation,Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View,
# CA 94043 USA, or visit www.azulsystems.com if you need additional information
# or have any questions.
#
# azsys Makefile
#

LIBRARY  = azsys
SRCROOT ?= $(CURDIR)

# Common Azul definitions
include ../Makefile.common

define check_variable
$(if $($1),,$(error $1 must be specified))
endef

VARS = SANDBOX TARGET_PLATFORM MKDIRS INSTALL RM_DASH_RF AR \
       TARGET_ARCHIVE_PREFIX TARGET_ARCHIVE_SUFFIX \
       TARGET_SHARED_LIBRARY_PREFIX TARGET_SHARED_LIBRARY_SUFFIX \
       TARGET_LDFLAGS_WHOLE_ARCHIVE TARGET_LDFLAGS_NO_WHOLE_ARCHIVE

$(foreach var,$(VARS),$(call check_variable,$(var)))

KNOWN_PLATFORM = false

ifeq ($(TARGET_PLATFORM), azlinux-x86_64)
KNOWN_PLATFORM  = true
TARGET_OSN     ?= azlinux
AZ_DEFINE       = AZ_LINUX
endif

ifeq ($(TARGET_PLATFORM), azproxied-x86_64)
KNOWN_PLATFORM  = true
TARGET_OSN     ?= azproxied
AZ_DEFINE       = AZ_PROXIED
endif

TARGET_ISA     ?= x86_64

ifneq ($(KNOWN_PLATFORM), true)
$(error "TARGET_PLATFORM $(TARGET_PLATFORM) not supported")
endif

SOURCES.C = \
	errors.c \
	exceptions.c \
	list.c \
	memory_hooks.c \
	mutex.c \
	os.c \
	process.c \
	syslog.c \
	system.c \
	thread.c \
	time.c

SOURCES.S = \
	whack_stack.s

OBJDIR                           = $(SANDBOX)/obj/azsys/$(TARGET_PLATFORM)
OPT.OBJDIR                       = $(OBJDIR)/opt
DBG.OBJDIR                       = $(OBJDIR)/dbg

INSTALL_DIR                      = $(SANDBOX)/$(TARGET_OSN)
INSTALL_LIB_DIR                  = $(INSTALL_DIR)/lib/$(TARGET_ISA)
# FIXME - azsys should be used after we nuke libos 
#INSTALL_INCLUDE_DIR              = $(INSTALL_DIR)/include/$(LIBRARY)
INSTALL_INCLUDE_DIR              = $(INSTALL_DIR)/include/os
INSTALL_AZNIX_INCLUDE_DIR        = $(INSTALL_DIR)/include/aznix
INCLUDE_DIR                      = $(INSTALL_DIR)/include

OBJECTS.OPT                      = $(addprefix $(OPT.OBJDIR)/, $(SOURCES.C:.c=.o))
OBJECTS.DBG                      = $(addprefix $(DBG.OBJDIR)/, $(SOURCES.C:.c=.o))
OBJECTS.OPT                     += $(addprefix $(OPT.OBJDIR)/, $(SOURCES.S:.s=.o))
OBJECTS.DBG                     += $(addprefix $(DBG.OBJDIR)/, $(SOURCES.S:.s=.o))

OBJECTS                          = $(OBJECTS.OPT) $(OBJECTS.DBG)

SRC_DIRS                         = $(SRCROOT)/src
SRC_DIRS                        += $(SRCROOT)/src/$(TARGET_OSN)

VPATH                           += $(SRC_DIRS:%=%:)


LIB.OPT                          = $(OPT.OBJDIR)/$(TARGET_ARCHIVE_PREFIX)$(LIBRARY)$(TARGET_ARCHIVE_SUFFIX)
LIB.DBG                          = $(DBG.OBJDIR)/$(TARGET_ARCHIVE_PREFIX)$(LIBRARY)_g$(TARGET_ARCHIVE_SUFFIX)
SHARED_LIB.OPT                   = $(OPT.OBJDIR)/$(TARGET_SHARED_LIBRARY_PREFIX)$(LIBRARY)$(TARGET_SHARED_LIBRARY_SUFFIX)
SHARED_LIB.DBG                   = $(DBG.OBJDIR)/$(TARGET_SHARED_LIBRARY_PREFIX)$(LIBRARY)_g$(TARGET_SHARED_LIBRARY_SUFFIX)

BUILD_TARGETS                    = $(LIB.DBG) $(LIB.OPT) $(SHARED_LIB.DBG) $(SHARED_LIB.OPT)

default all: $(BUILD_TARGETS)

CFLAGS_WARNINGS                  = -Wall -W -Werror -std=gnu99 -Wno-unused-parameter -Wno-unused-function
CFLAGS                           = $(CFLAGS_WARNINGS) -I$(AVX_INCLUDE_DIR) -I$(INCLUDE_DIR) -m64 -D_REENTRANT -D_XOPEN_SOURCE=600 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -DX86_64 -fno-strict-aliasing -DAZUL -DAZ_X86 -D$(AZ_DEFINE) -fPIC -DAZNIX_API_VERSION=$(AZNIX_API_VERSION)
CFLAGS_OPTIMIZED                 = $(CFLAGS) -O2
CFLAGS_DEBUG                     = $(CFLAGS) -DDEBUG -g
LDFLAGS                          = -fPIC -shared

.SUFFIXES: .o .c .s

$(OPT.OBJDIR)/%.o :   %.s
	$(CC) $(CFLAGS) -x assembler-with-cpp $< -c -o $@

$(DBG.OBJDIR)/%.o :   %.s
	$(CC) $(CFLAGS) -x assembler-with-cpp $< -c -o $@

$(OPT.OBJDIR)/%.o :   %.c
	$(CC) $(CFLAGS_OPTIMIZED) $< -c -o $@

$(DBG.OBJDIR)/%.o :   %.c
	$(CC) $(CFLAGS_DEBUG) $< -c -o $@

$(LIB.OPT): $(OPT.OBJDIR) $(OBJECTS.OPT)
	$(AR) -cr $(LIB.OPT) $(OBJECTS.OPT)

$(LIB.DBG): $(DBG.OBJDIR) $(OBJECTS.DBG)
	$(AR) -cr $(LIB.DBG) $(OBJECTS.DBG)

$(SHARED_LIB.OPT): $(LIB.OPT)
	$(CC) $(LDFLAGS) $(TARGET_LDFLAGS_WHOLE_ARCHIVE) $< $(TARGET_LDFLAGS_NO_WHOLE_ARCHIVE) -o $@

$(SHARED_LIB.DBG): $(LIB.DBG)
	$(CC) $(LDFLAGS) $(TARGET_LDFLAGS_WHOLE_ARCHIVE) $< $(TARGET_LDFLAGS_NO_WHOLE_ARCHIVE) -o $@

install::
	$(MKDIRS) $(INSTALL_LIB_DIR)
	$(INSTALL) $(BUILD_TARGETS) $(INSTALL_LIB_DIR)

installhdrs::
	$(MKDIRS) $(INSTALL_INCLUDE_DIR)
	$(INSTALL) $(SRCROOT)/include/azsys/*.h $(INSTALL_INCLUDE_DIR)
ifeq ($(TARGET_PLATFORM),azproxied-x86_64)
	$(INSTALL) $(SRCROOT)/include/$(TARGET_OSN)/azsys/*.h $(INSTALL_INCLUDE_DIR)
endif

$(OPT.OBJDIR) $(DBG.OBJDIR):
	$(MKDIRS) $@

clean::
	$(RM_DASH_RF) $(OBJDIR)

.PHONY : default all install installhdrs clean
