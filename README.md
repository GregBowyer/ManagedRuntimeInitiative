ManagedRuntimeInitiative
========================

 The Managed Runtime Initiative is an open development and integration initiative launched by Azul Systems with the aim of improving the execution of managed runtimes (.e.g. Java, Ruby, .Net) by enhancing interfaces and functionality across vertical components of the systems stack (e.g. managed runtime, OS kernel, hypervisor and hardware layer)

Ok so it went away when Azul shut it down, its still not a bad idea mind.

Dont expect this code to work all that well, when I have a mind I do occasionally port it to newer kernel versions, but I dont tend for it.

Also, dont expect this to ever make it into the kernel, if you read the code you will find that it exports some deep things; maybe one day we will figure out as a community how to export the supports needed for GPGC to work on linux without Azul having to do it, but dont hold your breath, the linux community views garbage collection as stupid (well lets be fair _kernel developers_ actually view it as useful (go splunking in the kernel ...), but many people for who C is clearly the fabric on which god wraught the universe tend to jump up and down, to those people I suggest you go look at http://gchandbook.org/ http://www.canonware.com/download/jemalloc/jemalloc-latest/doc/jemalloc.html and many other things to get over yourselves)
