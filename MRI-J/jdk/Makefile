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
# JDK Makefile
#

TARGET_JDK_VERSION = 1.6
KNOWN_PLATFORM = false

# Common makefile definitions
include ../Makefile.common

define check_variable
$(if $($1),,$(error $1 must be specified))
endef

VARS = SANDBOX TARGET_PLATFORM

$(foreach var,$(VARS),$(call check_variable,$(var)))

ifeq ($(TARGET_PLATFORM), azlinux-x86_64)
KNOWN_PLATFORM = true
export TARGET_OSN = azlinux
endif

ifeq ($(TARGET_PLATFORM), azproxied-x86_64)
KNOWN_PLATFORM = true
export TARGET_OSN = azproxied
endif

ifneq ($(KNOWN_PLATFORM), true)
$(error "TARGET_PLATFORM $(TARGET_PLATFORM) not supported")
endif

unexport JAVA_HOME
unexport JAVA
unexport JAVAC
unexport JAVAH
unexport JAR
unexport LD_LIBRARY_PATH

export ARCH_DATA_MODEL = 64
export LANG = C
export ALT_JDK_IMPORT_PATH = $(ALT_BOOTDIR)
export ALT_HOTSPOT_IMPORT_PATH = $(ALT_BOOTDIR) # FIXME - change this to azlinux hotspot once we start building under openjdk
export ALT_MOTIF_DIR = /usr

export MILESTONE
export BUILD_NUMBER = b$(JDK_BUILD_NUMBER)

CORES = $(shell cat /proc/cpuinfo | grep processor | wc -l)
export PARALLEL_COMPILE_JOBS = $(shell expr $(CORES) + 1)


ALT_OUTPUTDIR_BASE = $(SANDBOX)/obj/j2se-$(TARGET_JDK_VERSION)/$(TARGET_PLATFORM)
INSTALL_DIR        = $(SANDBOX)/$(TARGET_OSN)/jdk1.6

IMAGES ?= images

VARIANT_debug = DBG
VARIANT_openjdk = OPT

default all: debug openjdk

# openjdk builds the product version
debug openjdk:
	$(MAKE) -C make ALT_OUTPUTDIR=$(ALT_OUTPUTDIR_BASE)/$@ VARIANT=$(VARIANT_$@) $@ $(IMAGES)

install::
	mkdir -p $(INSTALL_DIR)/debug
	cp -rfp $(ALT_OUTPUTDIR_BASE)/debug/j2sdk-image/* $(INSTALL_DIR)/debug/
	mkdir -p $(INSTALL_DIR)/fastdebug
	cp -rfp $(ALT_OUTPUTDIR_BASE)/debug/j2sdk-image/* $(INSTALL_DIR)/fastdebug/
	mkdir -p $(INSTALL_DIR)/product
	cp -rfp $(ALT_OUTPUTDIR_BASE)/openjdk/j2sdk-image/* $(INSTALL_DIR)/product/
	mkdir -p $(INSTALL_DIR)/optimized
	cp -rfp $(ALT_OUTPUTDIR_BASE)/openjdk/j2sdk-image/* $(INSTALL_DIR)/optimized/

installhdrs::
	@echo "Nothing to be done for installhdrs for $(TARGET_PLATFORM)"

clean clobber::
	rm -Rf $(ALT_OUTPUTDIR_BASE)

.PHONY: default all debug openjdk install installhdrs clean clobber
