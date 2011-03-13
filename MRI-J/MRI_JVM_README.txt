Managed Runtime Initiative JVM README

This document contains documentation for the  JVM portion of the Managed Runtime 
Initiative (www.ManagedRuntime.org). It is an open software development and 
integration initiative launched by Azul Systems that exposes new functionality 
and interfaces to improve managed runtime execution. An open-source software 
reference implementation for Java(™) on Linux (called xPress) consists of a 
JVM derived from OpenJDK 6 and a set of enhanced loadable Linux kernel modules.

System Requirements:

xPress is x86/linux specific and is not intended for other architectures.
    - OS:  Linux Fedora FC12  v2.6.32-9

Status

This JVM release is a 'technology preview', that is, it is not release quality 
and some areas are not yet fully implemented.  It is presented now, however, 
so that the algorithms and implementation techniques can be reviewed.

Downloading and Extracting the Source Files:

The JVM source code is distributed as a tar archive. Refer to the Managed Runtime 
Initiative web site for specific download instructions. Extract the tar archive 
with the "tar -xv" command to a directory of your choice. Top level directory 
in the JDK release is named MRI-J.

Building the JVM:

Follow the following steps to install headers, build the libraries, and 
and build and install the JVM software:

1. Change to the MRI-J directory
2. Change definitions in file Makefile.common for your build environment.
3. Set Environment variables:
    set JAVA_HOME must be set to a valid openjdk binary installation. 
    unset TARGET_PLATFORM 
    unset SANDBOX
4. make -f Makefile.top
5. relax, have a cuppa coffee, your vm will be ready soon...

To Test:

% sandbox/azlinux/jdk1.6/debug/bin/java -version
