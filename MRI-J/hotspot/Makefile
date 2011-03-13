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
# hotspot6 Makefile
#

UNAME_ARCH := $(shell uname -m)
ifneq ($(UNAME_ARCH), x86_64)
$(error "Build host platform should be x86_64 - $(UNAME_ARCH) is not supported")
endif

LIBRARY = jvm
TARGET_JDK_VERSION  = 1.6

# Common Azul definitions
include ../Makefile.common

# FIXME - Remove these lines when we move to Makefile.common
GCC_DIR ?= /home/buildmaster/sw/gcc/4.3.0/linux/x86_64/bin
BINUTILS_DIR ?= /home/buildmaster/sw/binutils/2.18/linux/x86_64/bin

define check_variable
$(if $($1),,$(error $1 must be specified))
endef

VARS = SANDBOX TARGET_PLATFORM JAVA_HOME MKDIRS INSTALL RM_DASH_RF \
       TARGET_ARCHIVE_PREFIX TARGET_ARCHIVE_SUFFIX \
       TARGET_SHARED_LIBRARY_PREFIX TARGET_SHARED_LIBRARY_SUFFIX \
       BUILD_NUMBER JDK_VERSION \
       VERSION_MAJOR VERSION_MINOR VERSION_SP VERSION_PATCH VERSION_SUFFIX \
       AVX_INCLUDE_DIR AZNIX_API_VERSION \
			 GCC_DIR BINUTILS_DIR

$(foreach var,$(VARS),$(call check_variable,$(var)))

PLATFORM           ?= $(TARGET_PLATFORM)
SRCROOT            ?= $(shell pwd)

BUILD_FLAGS   += -bn $(BUILD_NUMBER) -jv $(JDK_VERSION) -rv $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_SP).$(VERSION_PATCH)$(VERSION_SUFFIX) -ai $(AVX_INCLUDE_DIR) -aav $(AZNIX_API_VERSION) -gccd $(GCC_DIR) -bud $(BINUTILS_DIR)

ifeq ($(TARGET_PLATFORM), azproxied-x86_64)
TARGET_OSN     = azproxied
TARGET_ISA     = x86_64
endif

ifeq ($(TARGET_PLATFORM), azlinux-x86_64)
TARGET_OSN     = azlinux
TARGET_ISA     = x86_64
endif

TARGET_JDK_DIR = $(SANDBOX)/$(TARGET_OSN)/jdk$(TARGET_JDK_VERSION)

JIT          ?= jit
ifeq ($(JIT), jit)
BUILD_FLAGS  += -jit
else
ifeq ($(JIT), core)
BUILD_FLAGS  += -core
else
$(error "invalid value for JIT: $(JIT)")
endif
endif

#  build.java dumps more information
ifeq ($(VERBOSE), true)
BUILD_FLAGS  += -v
endif

#  build.java displays each command and it's output
#  NOTE:  -Wall is specified in build.java, so if you turn on this option you'll see all compiler warnings
ifeq ($(WARN), true)
BUILD_FLAGS  += -w 
endif

ifeq ($(TARGET_PLATFORM), azlinux-x86_64)
INSTALL_DIR   ?= $(TARGET_JDK_DIR)
else
INSTALL_DIR   ?= $(SANDBOX)/$(TARGET_OSN)/lib/$(TARGET_ISA)/$(TARGET_JDK_VERSION)-1.6
endif

OBJ_DIR        = $(SANDBOX)/obj/hotspot6/$(TARGET_OSN)_$(TARGET_ISA)_$(JIT)$(TARGET_JDK_VERSION)
TARGET_OBJ_DIR = sandbox/obj/hotspot6/$(TARGET_OSN)_$(TARGET_ISA)_$(JIT)$(TARGET_JDK_VERSION)
BUILD_DIR      = $(OBJ_DIR)/build

default all: debug fastdebug optimized product

# install_libso - $1 - variant, $2 - dbg_suffix
define install_libso
	-$(INSTALL) $(OBJ_DIR)/$(1)/$(TARGET_SHARED_LIBRARY_PREFIX)$(LIBRARY)$(2)$(TARGET_SHARED_LIBRARY_SUFFIX) $(INSTALL_DIR)/$(1)/jre/lib/amd64/server/$(TARGET_SHARED_LIBRARY_PREFIX)$(LIBRARY)$(TARGET_SHARED_LIBRARY_SUFFIX)
endef

# install_liba - $1 - variant, $2 - dbg_suffix
define install_liba
	-$(INSTALL) $(OBJ_DIR)/$(1)/$(TARGET_ARCHIVE_PREFIX)$(LIBRARY)$(2)$(TARGET_ARCHIVE_SUFFIX) $(INSTALL_DIR)/$(TARGET_ARCHIVE_PREFIX)$(LIBRARY)_$(JIT)_$(1)$(TARGET_ARCHIVE_SUFFIX)
endef

install: installhdrs
ifeq ($(TARGET_PLATFORM), azlinux-x86_64)
	$(call install_libso,debug,_g)
	$(call install_libso,fastdebug,_g)
	$(call install_libso,product,)
	$(call install_libso,optimized,)
else
	-$(MKDIRS) $(INSTALL_DIR)
	$(call install_liba,debug,_g)
	$(call install_liba,fastdebug,_g)
	$(call install_liba,product,)
	$(call install_liba,optimized,)
endif

installhdrs:
  
clean: $(BUILD_DIR)/build.class
	-$(RM_DASH_RF) $(OBJ_DIR)

debug fastdebug optimized product: $(BUILD_DIR)/build.class
	$(JAVA_HOME)/bin/java -cp $(BUILD_DIR) build $(BUILD_FLAGS) -s $(SANDBOX) -t $(TARGET_PLATFORM) $@

$(BUILD_DIR)/build.class: $(SRCROOT)/build/build.java
	$(MKDIRS) $(BUILD_DIR)
	$(JAVA_HOME)/bin/javac -d $(BUILD_DIR) -classpath $(BUILD_DIR) $(SRCROOT)/build/build.java

.PHONY: debug fastdebug optimized product default all install installhdrs clean

