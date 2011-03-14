// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.nio.channels.*;

/**
 * A simple project build routine.

 * I hate makefiles, and ANT uses the horrid XML so I'm doing this hack
 * instead.  Actually, I've used this technique before to great success.  I
 * ought to make a Real Project out of it someday.
 *
 * What you see is the 'guts' of make/ANT PLUS the project file dependencies
 * at the end - all in pure Java.  Thus I can add new features to the make
 * (parallel distributed make, shared caching, etc) or add new dependencies.
 * Especially for the dependencies, there is no obtuse format to describe
 * things - it's all pure Java.
 *
 * TO DO LIST:
 * 
 * Priority Queue of jobs to compile
 * Parallize Yea Olde Above Priority Queue across the Build Farm (or even on same machine!)
 * FindBugz nitpicky
 *
 * Priorties -

 * 1) Things that failed for syntax errors last time; under the assumption
 *    you're working in a tight edit/compile cycle anything that failed in the
 *    last go-round should be compiled first this go-round - because you
 *    probably don't have all the syntax errors out yet.  This is very useful
 *    when hacking top-level includes like thread.hpp, because each touch of
 *    thread.hpp will force the World to recompile but likely there's exactly
 *    .cpp file you care about (thread.cpp?) and it's usually very late in the
 *    build.  One easy way to do this is to notice that failed build steps
 *    always nuke their target, so a missing target is most likely came from a
 *    syntax error on the last edit/compile cycle.
 *
 * 2) Jobs that might make more work - such as the ADLC or JVMTI or reading
 *    the foo.P files; each of these steps might possibly make more targets to
 *    build.  Thus the priority queue has to be dynamic; some targets appear
 *    only after some build-steps have happened.
 *
 * 3) Slow jobs; waiting to the end of a 200-file compile to START compiling
 *    'type.cpp' just lengthens the build cycle.  type.cpp probably takes more
 *    than 60sec to compile whereas most jobs compile less than 5sec.  We want
 *    to overlap the compile of type.cpp with many other compiles.
 *
 * @since 1.6
 * @author Cliff Click
 */
class build {
  static String  _buildnumber = "00";
  static String  _jdkversion = "1.6.0_13";
  static String  _releaseversion = "0.0.0.0";
  static String  _avx_includes = null;
  static int     _aznix_api_version;
  static String  _gcc_dir = null;
  static String  _binutils_dir = null;
  static int     _verbose;      // Be noisy; 0 (quiet), 1 (noisy) or 2 (very noisy)
  static boolean _warn;         // dump stderr and stdout from commands, default=false
  static boolean _justprint;    // Print, do not do any building
  static boolean _clean;        // Remove target instead of building it
  static boolean _keepontrucking; // Do not stop at errors
  static int     _parallelism;   
  // Top-level project directory
  static File TOP;              // File for the project directory
  static String TOP_PATH;       // Path to top-level
  static String TOP_PATH_SLASH;
  // Where the results go
  static String SANDBOX;
  // Pick up JAVA_HOME from the java property "java.home"
  static String JAVA_HOME;
  // Target OSN
  static String TARGET_OSN = "azlinux";
  // Target ISA
  static String TARGET_ISA = "x86_64";
  // Some of the libraries for aztek-vega2 come from the txu directory and not
  // the vega2 directory. Hence this addtional distinction for ISA
  static String LIBS_TARGET_ISA = "x86_64";
  // The libraries/archives for j2se are located under amd64 or txu.
  // So hack up another variable to contruct this path
  static String J2SE_ARCH_DIR = "amd64";
  static String CORE_OR_JIT = "jit";

  static void setDefines(String target) {
    if (target.equals("azproxied-x86_64")) {
      TARGET_OSN      = "azproxied";
      TARGET_ISA      = "x86_64";
      LIBS_TARGET_ISA = "x86_64";
      J2SE_ARCH_DIR   = "amd64";
    } else if (target.equals("azlinux-x86_64")) {
      TARGET_OSN      = "azlinux";
      TARGET_ISA      = "x86_64";
      LIBS_TARGET_ISA = "x86_64";
      J2SE_ARCH_DIR   = "amd64";
    } else {
      throw new Error("Unknown target platform");
    }
  }

  // --- main ----------------------------------------------------------------
  static public void main( final String args[] ) throws IOException, InterruptedException {
    // --- First up: find build.java!
    // Where we exec'd java.exe from
    TOP = new File(".");
    TOP_PATH = TOP.getCanonicalPath();
    File f = new File(TOP,"build/build.java");
    while( !f.exists() ) {
      File p2 = new File(TOP,"..");
      if( p2.getCanonicalPath().equals(TOP_PATH) )
        throw new Error("build/build.java not found; build/build.java marks top of project hierarchy");
      TOP = p2;
      TOP_PATH = TOP.getCanonicalPath();
      f = new File(TOP,"build/build.java");
    }

    TOP_PATH_SLASH = TOP_PATH.replaceAll("\\\\","\\\\\\\\");

    // --- Track down default sandbox
    File sand = TOP;
    f = new File(sand,"sandbox");
    while( !f.exists() ) {
      File p2 = new File(sand,"..");
      String spath = sand.getCanonicalPath();
      if( p2.getCanonicalPath().equals(spath) )
        break;                  // No sandbox found
      sand = p2;
      f = new File(sand,"sandbox");
    }
    if( f.exists() ) SANDBOX = f.getCanonicalPath();

    JAVA_HOME = System.getProperty("java.home");
    if (JAVA_HOME.endsWith("/jre")) {
      JAVA_HOME = JAVA_HOME.substring(0, JAVA_HOME.lastIndexOf('/'));
    }

    // --- Strip out any flags; sanity check all targets before doing any of them
    int j = 0;
    boolean error = false;
    String targetPlatform = null;
    String buildvariant = null;
    String g_suffix = "";
    for( int i=0; i<args.length; i++ ) {
      final String arg = args[i];

      if( arg.charAt(0) == '-' ) {
        if( false ) ;
        else if( arg.equals("-bn"   ) ) _buildnumber = args[++i]; // TODO - Verify arg
        else if( arg.equals("-jv"   ) ) _jdkversion = args[++i]; // TODO - Verify arg
        else if( arg.equals("-rv"   ) ) _releaseversion = args[++i]; // TODO - Verify arg
        else if( arg.equals("-ai"   ) ) _avx_includes = args[++i];
        else if( arg.equals("-aav"  ) ) _aznix_api_version = Integer.parseInt(args[++i]);
        else if( arg.equals("-gccd" ) ) _gcc_dir = args[++i];
        else if( arg.equals("-bud"  ) ) _binutils_dir = args[++i];
        else if( arg.equals("-v"    ) ) _verbose = 1;
        else if( arg.equals("-vv"   ) ) _verbose = 2;
        else if( arg.equals("-w"    ) ) _warn= true;
        else if( arg.equals("-n"    ) ) _justprint = true;
        else if( arg.equals("-k"    ) ) _keepontrucking = true;
        else if( arg.equals("-clean") ) _clean = true;
        else if( arg.equals("-p"    ) ) _parallelism = Integer.parseInt(args[++i]);
        else if( arg.equals("-s"    ) ) SANDBOX = args[++i];
        else if( arg.equals("-t"    ) ) targetPlatform = args[++i];
        else if( arg.equals("-core" ) ) CORE_OR_JIT = "core";
        else if( arg.equals("-jit"  ) ) CORE_OR_JIT = "jit";
        else {
          error = true;
          System.out.println("Unknown flag "+arg);
        }
      } else if( arg.equals("debug") || arg.equals("fastdebug") ) {
        buildvariant = arg;
        g_suffix = "_g";
      } else if( arg.equals("optimized") || arg.equals("product") ) {
        buildvariant = arg;
      } else {
        // TODO - We should make the user explicitly specify a target and
        // that it is an avm build
        if (arg.contains("azproxied")) {
          setDefines("azproxied-x86_64");
        } else if (arg.contains("azlinux")) {
          setDefines("azlinux-x86_64");
        }
        args[j++] = arg;        // Compact out flags from target list
      }
    }

    if (targetPlatform != null) {
      // Target platform can be azproxied-x86_64, azlinux-x86_64
      if (targetPlatform.equals("azproxied-x86_64")) {
        args[j++] = "sandbox/obj/hotspot6/azproxied_x86_64_" + CORE_OR_JIT + "1.6/" + buildvariant + "/libjvm" + g_suffix + ".a";
        setDefines("azproxied-x86_64");
      } else if (targetPlatform.equals("azlinux-x86_64")) {
        // TODO - figure out what to ship, stripped or unstripped.
        //args[j++] = "sandbox/obj/hotspot6/azlinux_x86_64_" + CORE_OR_JIT + "1.6/" + buildvariant + "/libjvm" + g_suffix + ".so.stripped";
        args[j++] = "sandbox/obj/hotspot6/azlinux_x86_64_" + CORE_OR_JIT + "1.6/" + buildvariant + "/libjvm" + g_suffix + ".so";
        setDefines("azlinux-x86_64");
      } else {
        throw new Error("Unknown target platform specified with -t");
      }

      if (_verbose > 0) {
        System.out.println("Target is " + args[j-1]);
      }
    }

    try {
      if( error ) {
        System.out.println("");
        System.out.println("Usage: build -bn <num> -jv <jdkversion> -rv <releaseversion> -ai <AVX_INCLUDE_DIR> -aav <AZNIX_API_VERSION> -gccd <GCC_DIR> -bud <BINUTILS_DIR> [-v|-vv] [-n] [-w] [-k] [-clean] [-s SANDBOX] [-t PLATFORM] [-core|-jit] [debug|fastdebug|optimized|product]");
        System.out.println("\t -bn         : set build number");
        System.out.println("\t -jv         : set jdk version");
        System.out.println("\t -rv         : set release version");
        System.out.println("\t -ai         : set AVX_INCLUDE_DIR");
        System.out.println("\t -aav        : set AZNIX_API_VERSION");
        System.out.println("\t -gccd       : set GCC_DIR");
        System.out.println("\t -bud        : set BINUTILS_DIR");
        System.out.println("\t -v          : verbose output, overrides -vv option");
        System.out.println("\t -vv         : super verbose output, overrides -v option");
        System.out.println("\t -n          : do nothing, just print");
        System.out.println("\t -w          : show each command's stdout and stderr");
        System.out.println("\t -k          : don't stop on errors");
        System.out.println("\t -clean      : delete build output");
        System.out.println("\t -s SANDBOX  : set SANDBOX directory");
        System.out.println("\t -t PLATFORM : set target platform");
        System.out.println("\t -core       : build the \"core\" target, overrides -jit option");
        System.out.println("\t -jit        : build the \"jit\" target, overrides -core option");
        System.out.println("");
        throw new Error("Command line errors");
      }
      if( _verbose > 0 ) 
        System.out.println("Building in "+TOP.getCanonicalPath());
      if( SANDBOX == null )
        throw new Error("No sandbox");
      if( !SANDBOX.endsWith("/sandbox") && !SANDBOX.endsWith("/sandbox/") )
        throw new Error("Sandbox does not end with /sandbox");
      if( !new File(SANDBOX).exists() )
        throw new Error("Sandbox does not exist: "+SANDBOX);

      if( _gcc_dir == null )
        throw new Error("No GCC_DIR. -gccd <GCC_DIR> should be specified");
      if( !new File(_gcc_dir).exists() )
        throw new Error("GCC_DIR does not exist: "+_gcc_dir);

      if( _binutils_dir == null )
        throw new Error("No BINUTILS_DIR. -bud <BINUTILS_DIR> should be specified");
      if( !new File(_binutils_dir).exists() )
        throw new Error("BINUTILS_DIR does not exist: "+_binutils_dir);

      if( _avx_includes == null )
        throw new Error("No AVX_INCLUDE_DIR. -ai <AVX_INCLUDE_DIR> should be specified");
      if( !new File(_avx_includes).exists() )
        throw new Error("AVX_INCLUDES_DIR does not exist: "+_avx_includes);


      // Make SANDBOX a relative path, if it's shorter.  Much easier to read the output.
      String t=TOP_PATH;
      String up="";
      String rel_sand = SANDBOX;
      while( !rel_sand.startsWith(t) ) {
        up = up+"../";
        t = t.substring(0,t.lastIndexOf('/'));
      }
      rel_sand = up+rel_sand.substring(t.length()+1);
      if( rel_sand.length() < SANDBOX.length() )
        SANDBOX = rel_sand;

      if( _verbose > 0 ) 
        System.out.println("Sandbox is "+SANDBOX);

      // Command-line targets starting with "sandbox/" are essentially
      // shortcuts for "$SANDBOX/", but for our chosen SANDBOX not some OS
      // environment variable.
      for( int i=0; i<j; i++ )
        if( args[i].startsWith("sandbox/") )
          args[i] = SANDBOX+args[i].substring(7);

      // --- Put all named targets into a bogus top-level target
      if( j == 0 ) throw new Error("No default target and no targets specified; did you mean to build nothing?");
    } finally {
      // --- All Done!
      System.out.flush();
      System.err.flush();
    }

    build0.main0(Arrays.copyOf(args,j));
  }
}

// --- build0 -------------------------------------------------------------------
class build0 { 
  static private class BuildError extends Error { BuildError( String s ) { super(s); } }

  static final String _buildnumber = build._buildnumber;
  static final String _jdkversion = build._jdkversion;
  static final String _releaseversion = build._releaseversion;
  static final String _avx_includes = build._avx_includes;
  static final int _aznix_api_version = build._aznix_api_version;
  static final String _gcc_dir = build._gcc_dir;
  static final String _binutils_dir = build._binutils_dir;
  static final int _verbose = build._verbose; // Be noisy; 0 (quiet), 1 (noisy) or 2 (very noisy)
  static final boolean _warn = build._warn; // dump stdout and stderr from commands, default=false
  static final boolean _justprint = build._justprint; // Print, do not do any building
  static final boolean _clean = build._clean; // Remove target instead of building it
  static final boolean _keepontrucking = build._keepontrucking; // Do not stop at errors
  static final boolean _allow_timestamp_ties = true;
  // Top-level project directory
  static final File TOP = build.TOP; // File for the project directory
  static final String TOP_PATH = build.TOP_PATH; // Path to top-level
  static final String TOP_PATH_SLASH = build.TOP_PATH_SLASH; // Canonical path to top level
  // Where the results go
  final static String SANDBOX = build.SANDBOX;

  // Some common strings
  static final String CORE_OR_JIT = build.CORE_OR_JIT;
  static final String JAVA_HOME   = build.JAVA_HOME;
  static final String JAVAC       = JAVA_HOME + "/bin/javac";
  static final String JAVA        = JAVA_HOME + "/bin/java";

  static final String TARGET_OSN = build.TARGET_OSN;
  static final String TARGET_ISA = build.TARGET_ISA;
  static final String LIBS_TARGET_ISA = build.LIBS_TARGET_ISA;
  static final String J2SE_ARCH_DIR = build.J2SE_ARCH_DIR;
  static final String SANDBOX_LIB_DIR = SANDBOX + "/" + TARGET_OSN + "/lib/" + TARGET_ISA + "/";
  static final String SANDBOX_J2SE_LIB_DIR = SANDBOX + "/" + TARGET_OSN + "/j2sdk1.6/" + LIBS_TARGET_ISA + "/jre/lib/" + J2SE_ARCH_DIR + "/";
  static final String SANDBOX_X11_LIB_DIR = SANDBOX + "/" + TARGET_OSN + "/x11/" + LIBS_TARGET_ISA + "/usr/X11R6/lib/";

  // --- Build directory layout
  static final String OBJDIR = SANDBOX+"/obj/hotspot6/";
  static final String OBJDIR_AZLINUX_X86_CORE6    = OBJDIR+"azlinux_x86_64_core1.6/";
  static final String OBJDIR_AZLINUX_X86_JIT6     = OBJDIR+"azlinux_x86_64_jit1.6/";
  static final String OBJDIR_AZPROXIED_X86_CORE6  = OBJDIR+"azproxied_x86_64_core1.6/";
  static final String OBJDIR_AZPROXIED_X86_JIT6   = OBJDIR+"azproxied_x86_64_jit1.6/";

  static final String BUILD_DIR = OBJDIR + TARGET_OSN + "_" + TARGET_ISA + "_" + CORE_OR_JIT + "1.6/build";

  // A work-queue for the Main thread.  Tasks on this list need to have their
  // dependencies inspected.  This is normally once per task, but foo.P files
  // run an "extra_check" which expands out a list of source .h file
  // dependencies.
  static final LinkedBlockingQueue<Q> _FindDeps = new LinkedBlockingQueue<Q>(); 
  static final Q ALL_DONE_SENTINEL = new Q("no such target");

  // The work queue
  static final Runtime RUN = Runtime.getRuntime();
  static final int numCPUs    = (build._parallelism>0) ? build._parallelism : RUN.availableProcessors();
  static final int numWorkers = (build._parallelism>0) ? build._parallelism
                                                       : ((numCPUs<3) ? (numCPUs+1) : ((numCPUs<8)?(numCPUs+2):(numCPUs+(numCPUs>>2))));

  static final ThreadPoolExecutor TPE = 
    new ThreadPoolExecutor( numCPUs/*starting thread count*/, numWorkers/*max threads*/, 1L, TimeUnit.SECONDS, 
                            new PriorityBlockingQueue<Runnable>());


  // --- main0 ---------------------------------------------------------------
  static public void main0( final String args[] ) throws IOException, InterruptedException {
    // --- Next up: always re-make self as needed
    // _build_c - is the class file for build.java (this file!)
    _build_c.find_deps();
    _FindDeps.take().find_deps();
    assert _FindDeps.size() == 0 ;
    long bc_time = _build_c._modtime;
    _build_c.build_step(0);
    _FindDeps.take();
    if( _build_c._modtime != bc_time ) {
      // Since we remade ourself, launch & run self in a nested process to do
      // the actual 'build' using the new version of self.
      TPE.shutdown();
      String a = JAVA + " -cp "+BUILD_DIR+" build -s "+SANDBOX+" ";
      if( _verbose == 1 ) a += "-v ";
      if( _verbose == 2 ) a += "-vv ";
      for( String arg : args )
        a += arg+" ";
      System.err.println(a);
      sys_exec(a,null,true).writeTo(System.out);
      System.out.flush();
      System.err.flush();
      System.exit(0);
    }

    if ( _verbose > 0 ) {
      System.out.println("Parallelism: " +numCPUs+ " starting threads, " +numWorkers+ " maximum threads");
    }

    try {
      Q qs[] = new Q[args.length];
      boolean error = false;
      for( int i=0; i<args.length; i++ ) { // For all targets
        qs[i] = Q.FILES.get(args[i]);
        if( qs[i] == null ) {
          error = true;
          System.err.println("Unknown target "+args[i]);
        }
      }
      if( error ) throw new BuildError("Command line errors");

      // When the top-level targets 'do_it' runs, it will do nothing but put the
      // end-of-lazily-found deps on the _FindDeps queue, which will break the
      // main thread back out and start shutting down the thread pools.
      _FindDeps.put(new Q("targets",' ',qs));
      
      // --- Now spin on the _FindDeps queue in the Main thread (only),
      // processing tasks to produce any dependent tasks.  Tasks can be added by
      // worker threads asynchronously.
      Thread.currentThread().setPriority(Thread.currentThread().getPriority()+1);
      while( true ) {
        final Q q = _FindDeps.take(); // Blocking call to get tasks.
        if( q == ALL_DONE_SENTINEL )
          break;
        q.find_deps();
      }
    } catch( RejectedExecutionException e ) {
    } finally {
      // --- All Done!
      TPE.shutdown();
      System.out.flush();
      System.err.flush();
    }
    // End of main
  }

  // --- StreamEater ---------------------------------------------------------
  // Used to 'eat' the output stream from a remote process and store it in a
  // local ByteArrayOutputStream.
  static private class StreamEater extends Thread {
    final private InputStream _is;
    final public ByteArrayOutputStream _buf = new ByteArrayOutputStream();
    private IOException _e;
    StreamEater( InputStream is ) { _is = is; start(); }
    public void run() {
      byte[] buf = new byte[1024];
      try {
        int len;
        while( (len=_is.read(buf)) != -1 ) {
          _buf.write(buf,0,len);
        }
      } catch( IOException e ) {
        _e = e;                 // Catch it for later, we're in the wrong thread
      }
    }
    public void close() throws IOException, InterruptedException {
      // called from the main thread on the StreamEater object, but not in the
      // StreamEater thread.
      join();
      if( _e != null ) throw _e; // Rethrow any exception in the main thread
    }
  }

  // --- sys_exec ------------------------------------------------------------
  // Run the command string as a new system process.  Throw an error if the
  // return value is not zero, or any number of other errors happen.  On an
  // error, all process output is dumped to System.out (stdout).  On success
  // the output is buffered and the caller can decide how to dump.
  static ByteArrayOutputStream sys_exec( String exec, File cwd, boolean die_on_fail ) {
    if( exec.length() == 0 ) return null; // Vacuously works for empty commands

    // The standard 'exec' call does not handle quoted strings very
    // nicely; this makes it hard to e.g. pass a quoted string to a
    // 'sh' shell thus allowing multiple commands in a single step.
    String[] execs = null;
    int x = exec.indexOf('"');
    if( x != -1 && exec.charAt(x-1)==' ' ) { // exec String contains quotes?
      // nothing yet
      ArrayList<String> toks = new ArrayList<String>();
      int tok=0;
      for( int i=0; i<exec.length(); i++ ) {
        char c = exec.charAt(i);
        if( c == ' ' || c == '"' ) {
          if( tok<i ) toks.add(exec.substring(tok,i));
          tok=i+1;              // next token starts after this whitespace
        } 
        if( c == '"' ) {
          i++;
          while( exec.charAt(i) != '"' ) i++;
          toks.add(exec.substring(tok,i));
          tok = i+1;
        }
      }
      if( tok<exec.length() ) toks.add(exec.substring(tok,exec.length()));
      execs = new String[toks.size()];
      toks.toArray(execs);
    }

    // Now run the command string in a seperate process.  Buffer all output,
    // so that error output from parallel processes can be dumped out without
    // interleaving.  Also if the command finishes without any errors and we
    // are not running very-verbose then we don't dump the commands output.
    StreamEater err = null, out = null;
    // This try/catch block will dump any output from the process before make dies
    try {
      Process p = null;
      // This try/catch block will catch any I/O errors and turn them into BuildErrors
      try {
        // Run the 'exec' String in a seperate process.
        p = execs == null ? RUN.exec(exec,null,cwd) : RUN.exec(execs,null,cwd);
        err = new StreamEater(p.getErrorStream()); // Start StreamEater threads
        out = new StreamEater(p.getInputStream());
        final int status = p.waitFor();

        if( status != 0 )
          throw new BuildError("Status "+status+" from "+exec);
        out.close();            // catch runaway StreamEater thread
        err.close();            // catch runaway StreamEater thread
      } catch( IOException e ) {
        if( err == null ) System.out.println(""+e);
        throw new BuildError("IOException from "+exec);
      } catch( InterruptedException e ) {
        throw new BuildError("Interrupted while waiting on "+exec);
      } finally {
        if( p != null ) p.destroy(); // catch runaway process
      }
    } catch( BuildError be ) {
      // Build-step choked.  Dump any output
      if( out != null ) try { out._buf.writeTo(System.out); } catch( IOException e ) { throw new BuildError(e.toString()); }
      if( err != null ) try { err._buf.writeTo(System.out); } catch( IOException e ) { throw new BuildError(e.toString()); }
      if( die_on_fail )  {      // For nested builds
        System.out.flush();
        System.err.flush();
        System.exit(0); 
      }
      throw be;                 // Rethrow after dumping output
    }

    // concat err onto the end of out so that both are returned
    if ( _warn ) {
        // create a new output stream
        ByteArrayOutputStream tmp = new ByteArrayOutputStream(); 
        
        // write the command to the head of the tmp buffer.
        try {
            tmp.write(exec.getBytes());
        } catch( IOException e ) {}
        
        // copy out to the tmp stream
        if (out._buf.size() > 0) {
            try {
                tmp.write('\n');
                out._buf.writeTo(tmp);
            } catch( IOException ioe) {}
        }
        // copy err to the tmp stream
        if (err._buf.size() > 0) {
            try {
                tmp.write('\n');
                err._buf.writeTo(tmp);
            } catch( IOException ioe) {}
        }
        return tmp;                // No errors?  Then here is the buffered output
    } else {
        return out._buf;           // No errors?  Then here is the buffered output
    }

    // shouldn't get to here ...

  }

  // Compute the base file name for a CPP file.
  // Silently chokes for non-CPP files.
  static final String basename( final String name ) {
    final String cppname = name.substring(name.lastIndexOf('/')+1);
    final String basename = cppname.substring(0,cppname.length()-4);
    return basename;
  }

  // --- A dependency --------------------------------------------------------
  static private class Q implements Comparable, Runnable {

    // A table of all dependencies & target files in the world
    static ConcurrentHashMap<String,Q> FILES = new ConcurrentHashMap<String,Q>();

    // Basic definition of a dependency
    final String _target;       // Target file name
    final char _src_sep;        // Standard string seperator used between dependent file names
    // I assume that calling lastModified() is a modestly expensive OS call.
    // I *know* that some OS's report file timestamps rounded down badly, to
    // the nearest 1 second on linux.
    long _modtime;           // Cache of _dst.lastModified() (or System.CTM for -n builds)

    int _priority;              // hint for build-order speed
    int _cum_prior;             // Cumlative priority from here to root

    // These next 5 fields need to be atomically updated.  
    // They are updated and read concurrently by many threads.

    // The _srcs array is updated by 'extra_check' for ".o" files - we read
    // the matching ".P" file for a list of dependent ".hpp" files.  It never
    // otherwise changes.  No other thread should be reading it until the
    // sync'd extra_check returns - and then it should bounce over to the main
    // thread for a re-check of dependencies.

    Q[] _srcs; // Array of dependent files; not final because HPP dependencies get auto-added later
    
    // Cleared by _extra_check so that main knows to re-inspect hpp lists.
    File _dst;                  // Actual OS file; set once during find_deps, cleared by extra_check

    // Count of _ready_children.  This field changes when
    // (1) the _srcs change, hence changing the number of children
    // (2) any child changes state
    int _ready_children; // Count of ready children 
    // Valid states are- 
    // - null - never inspected
    // - "done" - all children are "done" and this build-step fired if needed
    // - "extra_check" - extra deps added; do not attempt to add again
    // - "failed"
    String _state;
    
    // Parent dependences of this dep.  Changed by a child extra_step.
    final Vector<Q> _parents = new Vector<Q>();

    // --- Constructor for a list of source dependencies
    Q( String target, char src_sep, Q[] srcs ) {
      _target = target;
      _src_sep = src_sep;
      _srcs = srcs;

      // Basic sanity checking
      if( target.indexOf('%') != -1 ) 
        throw new IllegalArgumentException("dependency target has a '%': "+target);
      
      // Install the target/dependency mapping in a global flat table
      Q old = FILES.put(_target,this);
      if( old != null ) 
        throw new IllegalArgumentException("More than one dependency for target "+
                                           target+((_srcs.length > 0) ? (" with source "+_srcs[0]._target):""));

      // Set any childs' parent-pointers to me
      for( Q src : _srcs ) {
        synchronized(src) {
          src._parents.add(this);
        }
      }
    }

    // --- Constructor for a root file; no dependencies
    static final private Q[] NONE = new Q[0];
    Q( String target ) {
      this(target,' ',NONE);
    }

    // --- Factory for a single dependency
    static Q Q1( String target, Q src ) {
      Q qs[] = new Q[1];
      qs[0] = src;
      return new Q(target,' ',qs);
    }

    // Special version for dynamically added targets with no deps (ala system
    // include files): does a putIfAbsent into FILES.
    private Q( String target, boolean f ) { _target = target; _src_sep = ' '; _srcs = NONE; }
    static Q new_dynamic( String target ) {
      Q q = FILES.get(target);
      if( q != null ) return q; // Quick cutout
      q = new Q(target,false);
      FILES.putIfAbsent(target,q); // returns OLD value in table
      return FILES.get(target);    // Return current table contents
    }

    // --- addDep
    // A handful of files need some #include deps "hand added" instead of using
    // the auto-discovery mechanism.
    synchronized Q addDep( Q ... deps ) {
      // Rather clunky dup-dependency-removal, to help with debugging
      int found = 0;            
      for( Q x  : deps ) {
        for( Q y : _srcs ) {
          if( x==y ) { found++; break; }
        }
      }
      if( found == deps.length ) return this;

      Q[] srcs = new Q[_srcs.length+deps.length];
      System.arraycopy(_srcs,0,srcs,0,_srcs.length);
      System.arraycopy( deps,0,srcs,_srcs.length,deps.length);
      _srcs = srcs;
      for( Q dep : deps )
        synchronized(dep) { dep._parents.add(this); }
      return this;
    }

    // --- find_deps
    // One-shot per Q, make sure each _src has _dst File.
    // Sync'd to cover setting _dst & reading _ready_children count
    synchronized void find_deps() throws InterruptedException {
      if( _dst == null ) {      // First time thru only
        if( this instanceof QLinkerOption ) {
          _dst = new File(TOP,"");
          _modtime = 1;
        } else {
          _dst = _target.charAt(0)=='/' // Absolute path?
            ? new File(_target)         // Then use it absolutely
            : new File(TOP,_target);    // Else assume TOP relative
          _modtime = _dst.lastModified(); // Cache OS time
          // Zero-length files are almost always errors sometimes caused by
          // hitting ^C at a bad moment.  Just assume they need to be built.
          if( _dst.length() == 0 ) _modtime = 0; // Pretend zero-length files don't exist
        }
        _priority = 0;
        if( _modtime==0 )               // if file does not exist
          _priority += 99999999;        // do it first
        if( _target.endsWith("P") )     // If file creates more work
          _priority += 59999999;        // do it 2nd
        _priority += _dst.length();     // Otherwise do slower (bigger) files before faster files
        _cum_prior = _priority;         // Add in your parent's priority also
        for( Q p : _parents )           // Also include all parents' priorities
          _cum_prior += p._cum_prior;
        if( _modtime == 0 && _srcs.length == 0 ) 
          throw new BuildError("Source file "+_target+" is missing, used by at least "+_parents.get(0)._target);
      } else if( _state != "extra_check" ) { // Tossed onto finddeps extra times?
        // _dst already set, so already ran finddeps
        return;                     // Nothing to do here
      }

      // Put children on as well
      for( Q src : _srcs ) {
        synchronized(src) {
          if( src._dst == null || src._state == "extra_check" )
            _FindDeps.put(src);
          else
            src._cum_prior += _cum_prior;
        }
      }
      // Put trivially ready things on the WorkQueue
      if( _state == null && _ready_children == _srcs.length )
        TPE.execute(this);
    }


    // Make a single String with all sources, pre-pended with the top-level
    // path and seperated by the 'sep' char.
    String flat_src( char sep ) {
      String s = "";
      if( _srcs.length==0 ) return s;
      for( int i=0; i<_srcs.length-1; i++ ) {
        if( _srcs[i]._target.charAt(0)!='/' &&
            !(_srcs[i] instanceof QLinkerOption) )
          s += TOP_PATH_SLASH+"/";
        s += _srcs[i]._target+sep;
      }
      if(( _srcs[_srcs.length-1]._target.charAt(0)!='/' ) &&
            !(_srcs[_srcs.length-1] instanceof QLinkerOption) ) {
        s += TOP_PATH_SLASH+"/";
      }
      s += _srcs[_srcs.length-1]._target;
      return s;
    }

    // The void 'do_it' function, with no output.  Override this to do
    // a real build-step.  Also print a 1-liner on what the step is
    // doing, WITH printing a newline in a single output write.
    protected ByteArrayOutputStream do_it( ) {
      return null;
    }

    // Override if your build-step adds any dependencies
    // Return 1 - added child deps that need to be checked
    // Return 2 - force build step to happen
    protected int extra_check() {
      return 0;
    }

    // Building priorities encoded here
    // 1- missing files (probably a failed prior build-step with syntax errors
    // 2- jobs making more work (.P files, ADLC, or JVMTI XML)
    // 3- big jobs
    // Lower priorities are pulled from the work queue first
    public int compareTo(Object o) {
      return ((Q)o)._cum_prior - _cum_prior;
    }

    public void findDeps_queue() {
      try {
        _FindDeps.put(this); // Find'em all...
      } catch( InterruptedException e ) {
        throw new Error(e); // Rethrow as an unchecked exception - we're dead
      }
    }

    // --- Do the build-step, as needed
    public String build_step( final int xc ) {
      // Cleaning?  Nuke target and return
      if( _clean ) {
        if( _srcs.length == 0 ) return "done"; // Do not remove source files
        if( !_dst.exists() ) return "done";    // Already removed
        if( this == _build_c ) return "done";  // Highly annoying to delete own class file
        System.out.println("rm "+_target);
        if( !_justprint ) _dst.delete();
        return "done";
      }
        
      // See if all is up-to-date.  Collect out-of-date filenames.  Collect
      // youngest source file (output file produced by this build step should
      // end up newer than the youngest source file).
      long last_src = 0;
      String newer = null;
      for( Q src : _srcs ) {
        if( last_src < src._modtime )
          last_src = src._modtime;      // Latest source time
        if( _modtime < src._modtime ) { // Out of date?
          if( newer == null ) {         // No filenames yet?
            newer = src._target;        // Collect 1st filename
          } else {                      // 2nd or later filename
            newer += " "+src._target;   // Collect filenames
          }
        }
      }
      if( xc <= 1 && newer == null ) { // Found no out-of-date source files?
        if( _verbose > 1 ) {
          if( _srcs.length != 0 ) {
            System.out.println("-- " + _target + " > { "+flat_src(' ')+" } : already up to date");
          } else {
            System.out.println("-- " + _target + " is a source file : already up to date");
          }
        }
        return "done";
      }

      // Else out-of-date and must build
      // ---
      // Files out of date; must do this build step
      if( _verbose > 0 ) {
        if( _modtime == 0 ) System.out.println("-- " + _target+ " is missing");
        else                System.out.println("-- " + _target+ " <= { "+newer+" }"+
                                               (newer==null?", forced build step because of new dependencies":""));
      }
      
      // About to attempt to make the file; force any needed directories
      File target_file = new File(_target);
      File parent_dir = target_file.getParentFile();
      if (parent_dir != null) parent_dir.mkdirs();
      
      try {
        // Do the build-step, capturing stdout and stderr.  If (_warn), then 
        // dump build-step's stdout+stderr.  Failed build-steps always dump
        // their stdout+stderr.
        ByteArrayOutputStream buf = do_it();
        if( ( _warn ) && ( buf != null) ){
            try {
                buf.writeTo(System.out);
                System.out.println();
            } catch( IOException e) {}
        }
      } catch( BuildError be ) { // Catch obvious build failures
        // Failed; make sure target is deleted (except for build.class)
        if( this != _build_c ) // Highly annoying to delete own class file
          _dst.delete(); 
        if( !_keepontrucking ) { // single error is fatal
          try { Thread.sleep(1000); } catch( InterruptedException e ) { }
          TPE.shutdown();
          System.out.flush();
          System.err.flush();
          System.exit(-1);
        }
        return "failed";
      }

      if( _justprint ) {        // No expectation of any change
        _modtime = last_src+1;  // Force modtime update          
        return "done";
      }
          
      // Double-check that the source files were not modified by the
      // build-step.  It's a fairly common error if file-names get swapped,
      // etc.  This exception is uncaught, so it aborts everything.  It
      // basically indicate a broken build file - the build-step is changing
      // the wrong file.
      for( Q src : _srcs )
        if( !(src instanceof QLinkerOption) &&
            src._modtime != src._dst.lastModified() )
          throw new IllegalArgumentException("Timestamp for source file "+src._target+
                                             " apparently changed by building "+_target+
                                             " last recorded time="+src._modtime+
                                             " and now the filesystem reports="+src._dst.lastModified());
      
      // Double-check that this step made progress.  Again, failure here is
      // likely a build-step failure to modify the correct file.
      long x = _dst.lastModified();
      int sleep=100;
      while( _modtime == x && sleep >0 ) {
        System.out.println("Timestamp for "+_target+" not changed by building; time="+x);
        try { Thread.sleep(1); } catch( InterruptedException e ) { };
        sleep--;
      }
      _modtime = x;
      long now = System.currentTimeMillis();
      //
      // We'd like to verify that the modtime on a file is 'reasonable'.  But when building
      // on an nfs mounted filer, the time stamps can be off by a couple of seconds.
      // So don't complain about differnces less than 3 seconds.
      //
      // NOTE:  This just a workaround while we come up with a better way to handle
      // modification times.
      //
      while( (now+3000) < _modtime ) {
        System.out.println("Timestamp for "+_target+" moved "+(_modtime-now)+"ms into the future by building; sleeping until the Future is Now");
        try { Thread.sleep(_modtime-now); } catch( InterruptedException e ) { };
        now = System.currentTimeMillis();
      }
      if( _modtime < last_src )
        throw new IllegalArgumentException("Timestamp for "+_target+" not changed by building "+_target);
      // Invariant: last_src <= _modtime <= now
        
        
      // For very fast build-steps, the target may be updated to a time equal
      // to the input file times after rounding to the file-system's time
      // precision - which might be as bad as 1 whole second.  Assume the
      // build step worked, but give the result an apparent time just past the
      // src file times to make later steps more obvious.
      if( !_allow_timestamp_ties ) {
        while( x == last_src ) {     // Aaahh, we have 'tied' in the timestamps!!!
          // Sleep/spin until the OS's version of a rounded timestamp shows real progress
          try { Thread.sleep(1); } catch( InterruptedException e ) { };
          now = System.currentTimeMillis();
          _dst.setLastModified(now); // Pretend file was made 'right NOW!'
          x = _dst.lastModified(); // Reload, after OS rounding
        }
        if( _modtime == last_src )
          System.out.println("Yawners... had to sleep "+(System.currentTimeMillis()-_modtime)+" msec to get timestamp to advance");
      }
      _modtime = x;             // Record apparent mod-time
      return "done";
    }

    // --- What happens 'FutureTask.get' is called on a Q?
    public void run() {
      if( _parents.size() == 0 ) { // Ready to build the top-level fake target?
        _state = "done";
        ALL_DONE_SENTINEL.findDeps_queue(); 
        return;
      }
      
      // Assert we never been run before, or only run thru 'extra_check'
      String st;
      synchronized(this) { st = _state; }
      assert st == "extra_check" || st == null : "1Running "+_target+" with st="+st+" and rdy="+_ready_children+":"+_srcs.length;
      assert _ready_children == _srcs.length   : "2Running "+_target+" with st="+st+" and rdy="+_ready_children+":"+_srcs.length;

      int xc = 0;               // Is build-step forced?  (generally by a missing file?)
      if( st == null ) {        // Not ready extra-step 
        try {
          xc = extra_check();   // Producing extra dependencies?
        } catch( Exception e ) {
          System.err.println(e.getMessage());
          TPE.shutdown();
          System.out.flush();
          System.err.flush();
          System.exit(-1);
        }
        if( xc == 1 ) return; // No more work here, until deps are all found & built
      }
        
      // See if any child failed
      String state = null;
      for( Q src : _srcs ) {     // Assert all children already done
        String cstate;
        synchronized(src) { cstate= src._state;}
        assert cstate == "done" || cstate == "failed" : "Running "+_target+" but child "+src._target+" has state="+cstate;
        if( cstate == "failed" )
          state = "failed";
      }

      // Do build step (if we aint dead yet)
      if( state == null )       // No failed children?
        state = build_step(xc); // Get new state from build_step

      // Change state under lock, but also clone parent list.  When the lock
      // releases, other parents may be added and they need to handle the
      // exposed _state (i.e., they just added a 'done' child so they better
      // update their ready_children count.
      Vector<Q> pclone;
      synchronized(this) {
        _state = state;
        pclone = new Vector<Q>(_parents);
      }

      // Inform all parents that this child is ready 
      for( Q p : pclone )
        p.child_is_ready(this);
    }

    // --- child_is_ready
    // Some child completed successfully.
    // Check _dst under lock; 
    synchronized void child_is_ready(Q c) {
      assert _ready_children < _srcs.length : " "+_target+" "+_ready_children+"<"+_srcs.length+ " child="+c._target;
      _ready_children++;

      // Happens on parents (with ready children) not involved in any
      // top-level build target - they become ready to build, but no target
      // needs them.
      if( _dst == null ) return; 

      if( _ready_children == _srcs.length ) {
        try {
          TPE.execute(this);
        } catch( RejectedExecutionException ree ) {
          // ignore if shutting down
        }
      }
    }

    // Insert a brief pause on the file systems' behalf - but only for specific
    // large-file-count build steps.
    void brief_pause() {
    }
  };

  // --- A dependency with an exec string ------------------------------------
  // Mostly just a normal dependency; it "execs" a String to do the build-step.
  static private class QS extends Q {
    final String _exec;
    String _parsed_exec;

    // --- Constructor for a list of source dependencies
    QS( String target, String exec, char src_sep, Q ... srcs ) {
      super(target, src_sep,srcs);
      _exec = exec;
      for( int i=_exec.indexOf('%'); i!= -1; i = _exec.indexOf('%',i+1) ) {
        if( false ) {
        } else if( _exec.startsWith("dst",i+1) ) { 
        } else if( _exec.startsWith("src",i+1) ) { 
        } else if( _exec.startsWith("top",i+1) ) { 
        } else
          throw new IllegalArgumentException("dependency exec has unknown pattern: "+_exec.substring(i));
      }
    }

    QS( String target, String exec, Q src ) {
      this(target,exec,' ',src);
    }

    // --- parse_exec
    // The _exec String contains normal text, plus '%src' and '%dst' strings.
    String parse_exec() {
      if( _parsed_exec == null ) {
        if( _srcs.length > 0 ) _parsed_exec =        _exec.replaceAll("%src0",_srcs[0]._target);
        if( _srcs.length > 1 ) _parsed_exec = _parsed_exec.replaceAll("%src1",_srcs[1]._target);
        if( _srcs.length > 2 ) _parsed_exec = _parsed_exec.replaceAll("%src2",_srcs[2]._target);
        _parsed_exec = _parsed_exec
          .replaceAll("%dst",_target)
          .replaceAll("%src",flat_src(_src_sep))
          .replaceAll("%top",TOP_PATH_SLASH);
      }
      return _parsed_exec;
    }

    protected ByteArrayOutputStream do_it( ) {
      final String exec = parse_exec();
      System.out.println(exec);   // Print 1-liner on what the step is doing
      brief_pause();              // let input files settle out
      return _justprint ? null : sys_exec(exec, null, false);
    }
  }

  // --- A dependency for a C++ compile --------------------------------------
  // The source is a foo.P file; read it for more dependencies.
  // Add the dependencies in 'extra_check'.
  // The 'doit' step just changes the default printout from QS - otherwise
  // it just does a normal string exec.
  static private class QC extends QS {
    // Given a path to a C++ file (and a compile string), build a foo.o
    // dependency to build from a foo.P file.
    static QC gen( String tpath, String flavor, String exec, String basename, String ext, Q ... extras ) { 
      final String target = tpath+flavor+basename+".o";
      final String Pname = tpath+"incls/"+basename+ext;
      final Q src = FILES.get(Pname);
      if( src == null ) throw new IllegalArgumentException("Missing dep for "+Pname+"\n");
      if( extras!=null && extras.length>0 )
        src.addDep(extras);
      return new QC(target,exec,src);
    }
    private QC( String target, String exec, Q     src  ) { super(target,exec,src ); }

    // Given a list of C++ files (and a compile string), build an array of foo.o
    // dependencies to build, from the list of C++ files.
    static Q[] gen_all( Q[] cs, String tpath, String flavor, String exec, Q ... extras ) { 
      final Q os[] = new Q[cs.length];
      for( int i=0; i<cs.length; i++ )
        os[i] = gen( tpath, flavor, exec+" "+cs [i]._target, basename(cs [i]._target), ".PP", extras );
      return os;
    }

    // Override the default non-verbose printout.
    protected ByteArrayOutputStream do_it( ) {
      final String exec = parse_exec();
      if( _verbose > 0 ) System.out.println(exec); // Print 1-liner on what the step is doing
      else System.out.println("Compiling "+_target);
      return _justprint ? null : sys_exec(exec, null, false);
    }
    
    // Read the .P file for extra dependencies
    protected int extra_check() {
      Q pfile=null;
      for( Q x : _srcs ) {
        if( x._target.endsWith("P") ) {
          pfile=x;
          break;
        }
      }
      if( pfile==null ) throw new BuildError("Missing P file dep for "+_target);
      try {
        // Read & parse the deps file
        final File deps = new File(pfile._target);
        final int len = (int)deps.length();
        final char[] cbuf = new char[len];
        if( len != new FileReader(deps).read(cbuf,0,len) )
          throw new IOException("Unexpected short read");
        // Split the string based on:
        // ' ' - blank
        // '\' - Backslash.  Must be encoded as 4 backslashes in the split string
        // '\n' - newline.
        final String[] ss = new String(cbuf).split("[ \\\\\n:]+");
        // The first String in ss should be of the form "foo.o".
        if( ss.length < 1 || !_target.endsWith(ss[0]) )
          throw new IllegalArgumentException("Expected first dep of "+pfile._target+" to refer to "+_target+"; badly formed .PP file; please delete it");
        // The 2nd String should be of the form TOP+".../foo.cpp"
        String srcname="";
        if( ss.length < 2 || !(srcname=(TOP_PATH_SLASH+"/"+pfile._srcs[0]._target)).endsWith(ss[1]) )
          throw new IllegalArgumentException("Expected second dep to refer to "+srcname+" but found "+ss[1]+"; badly formed .PP file; please delete "+pfile._target);

        // Copy the initial _srcs into a HashMap, to remove dup strings from
        // the dependency file
        final HashMap<String,Q> srcs = new HashMap<String,Q>();
        for( Q s : _srcs )
          srcs.put(s._target,s);

        // The remaining Strings will be proper dependencies.  They either:
        // -  start with 'src' and are a relative name and must be in the global FILES list, or
        // -  start with TOP_PATH_SLASH, then 'src' and are treated as above, or 
        // -  they start with '/' and refer to a system include file, or 
        // -  start with 'sandbox' and refer to an OS include file,
        for( int i=2; i<ss.length; i++ ) {
          String s = ss[i];
          if( s.startsWith("/") || s.startsWith("../") ) {
            srcs.put(s,Q.new_dynamic(s)); // sandbox or system .h files are just assumed valid
          } else if( s.startsWith("src/") ) { // these should exist in build.java, for some kind of safety
            Q dep = Q.FILES.get(s);
            String fname = TOP_PATH_SLASH+"/"+s;
            boolean file_found = new File(fname).canRead();
            if( dep == null ) { // Source file not mentioned in build.java
              if( !file_found ) { // File not found?
                if( _verbose > 0 )
                  System.out.println("--- File "+fname+" not found, forcing rebuild");
                // File not found, and not in build.java - so dep file is
                // wrong; force a recompile of the C++ program.  It should
                // fail (after all an include file is missing).  If the C++
                // file is edited to remove the missing include file, that
                // should trigger a rebuild of the dep file - which will no
                // long mention the missing include file.
                return 2;       // force build step
              }
              // Dep file mentions a name that is not in build.java, but the
              // file still exists - assume it is a missing dependence in the
              // build file.
              throw new IllegalArgumentException("build.java does not have a dependence for file "+s);
            } else if( !file_found ) {
              // Dep file and build.java both mention a file that does not exist.
              // Probably the file was removed and build.java should be cleaned up.
              throw new IllegalArgumentException("build.java has a dependence for missing file "+fname);
            }
            srcs.put(s,dep);
          } else {
            throw new IllegalArgumentException("Dependency filename does not start with '/' or 'sandbox' or 'src' "+s);
          }
        }

        // Anything change?
        if( srcs.size() == _srcs.length ) return 0;

        final Q qsrcs[] = srcs.values().toArray(_srcs); // Update _srcs list; more deps found so it grew
        // Source list changed; update it, and the parent/child relations and counts
        // 'this' is locked
        synchronized(this) {    // Lock self; no updating _ready_children
          _state = "extra_check"; // Change state: we'll need an extra go'round in find_deps
          _srcs = qsrcs;        // Change list of children
          _ready_children = 0; // Recompute readiness; some children are ready and some are not
          for( Q src : qsrcs ) {
            synchronized(src) {
              src._parents.add(this);
              if( src._state == "done" || src._state == "failed" )
                _ready_children++;
            }
          }
          if( _ready_children == qsrcs.length )
            return 0;           // All new children all ready to go!
          findDeps_queue();     // Else must FindDep'em all...
          return 1;             // Added new deps!!!
        }

      } catch( FileNotFoundException e ) {
        if( _justprint || _clean ) return 0; // Just printing - so no new deps discovered
        throw new IllegalArgumentException("make is busted: did not spot missing file: "+e);
      } catch( IOException e ) { // Some I/O issue?
        throw new BuildError(e.toString()); // Rethrow as a BuildError
      }

    } // extra_check

  }

  // --- A dependency for an archive -----------------------------------------
  static private class QA extends QS {
    QA( String target, String exec, char src_sep, Q ... srcs ) { super(target,exec,src_sep,srcs); }
    // Override the default non-verbose printout.
    protected ByteArrayOutputStream do_it( ) {
      final String exec = parse_exec();
      if( _verbose > 0 ) System.out.println(exec); // Print 1-liner on what the step is doing
      else System.out.println("Archiving "+_target);
      return _justprint ? null : sys_exec(exec, null, false);
    }

    // To help avoid NFS wackiness, where the clocks are slightly askew I insert
    // a tiny delay before the archiving step.  This lets the filer system have
    // a change to swallow all files being archived and get it's file timestamps
    // settled out before the archiving promptly asks for them all.
    void brief_pause() {
      try { Thread.sleep(2); } catch( InterruptedException e ) { };
    }

  }
    
  static Q[] flatten_to_Q( Object ... srcs ) {
    int sz=0;
    for( Object o : srcs )
      sz += (o instanceof Q[]) ? ((Q[])o).length : 1;
    Q qs[] = new Q[sz];
    int i=0;
    for( Object o : srcs )
      if( o instanceof Q[] ) {
        Q[] sqs = (Q[])o;
        System.arraycopy(sqs,0,qs,i,sqs.length);
        i += sqs.length;
      } else {
        qs[i++] = (Q)o;
      }
    return qs;
  }

  // --- A dependency for a link ---------------------------------------------
  static private class QL extends QS {
    QL( String target, String exec, char src_sep, Object ... srcs ) { 
      super(target,exec,src_sep,flatten_to_Q(srcs)); 
    }
    QL( String target, String exec, Q[] srcs ) { 
      super(target,exec,' ',srcs);
    }

    // Override the default non-verbose printout.
    protected ByteArrayOutputStream do_it( ) {
      final String exec = parse_exec();
      if( _verbose > 0 ) System.out.println(exec); // Print 1-liner on what the step is doing
      else System.out.println("Linking   "+_target);
      return _justprint ? null : sys_exec(exec, null, false);
    }
  }
    
  // --- A bogus dependency for adding a linker option in the middle ---------
  static private class QLinkerOption extends Q {
    QLinkerOption( String opt ) { super(opt); }
    public String build_step( final int xc ) { return "done"; }
  }

  // --- Strip and Sign a Binary, in 1 step ---------
  static private class QStripSign extends Q {
    private QStripSign( String dst, Q[] srcs ) { super(dst,' ',srcs); }
    public static QStripSign make( String dst, Q src ) { Q[] qs = {src}; return new QStripSign(dst,qs); }
    
    protected ByteArrayOutputStream do_it( ) {
      final String exec0 = STRIP_X86+" -d -o "+_target+" "+_srcs[0]._target;
      if( _verbose > 0 ) System.out.println(exec0); // Print 1-liner on what the step is doing
      else System.out.println("Stripping "+_target);
      ByteArrayOutputStream bas0 = _justprint ? null : sys_exec(exec0, null, false);

      final String exec1 = ELFSIGN+" "+_target;
      if( _verbose > 0 ) System.out.println(exec1); // Print 1-liner on what the step is doing
      else System.out.println("Signing   "+_target);
      ByteArrayOutputStream bas1 = _justprint ? null : sys_exec(exec1, null, false);
      return bas1;
    }
  }

  // --- Construct foo.P dependencies from all foo.cpp files -----------------
  // Find all 'cpp' files in the FILES list.  For each src/.../foo.cpp, create
  // a 'path/incls/foo.P' dependency.  The dependency will cause the foo.P
  // file to be made from the 'bld' string if the foo.cpp file changes.  A
  // foo.P file lists all the other '#include' files used by foo.cpp.
  static private class QP extends QS {
    private QP( final String target, final String bld, char src_sep, final Q ... srcs ) { super(target,bld,src_sep,srcs); }
    private QP( final String target, final String bld, final Q src ) { super(target,bld,src); }
    static void build_P_deps( String path, String bld ) {
      for( String key : Q.FILES.keySet() ) {
        if( key.endsWith("cpp") ) {
          final String basename = basename(key);
          final String Pname = basename+".PP";
          Q pp = new QP(path + "incls/"+Pname,bld,' ',FILES.get(key));
        }
      }
    }
    // Override the default non-verbose printout.
    protected ByteArrayOutputStream do_it( ) {
      final String exec = parse_exec();
      if( _verbose > 1 ) System.out.println(exec); // Print 1-liner on what the step is doing
      else System.out.println("Depending "+_target);
      return _justprint ? null : sys_exec(exec, null, false);
    }
  }

  // --- A dependency, just 'touch'ing the target ----------------------------
  // Mostly just a normal dependency
  static private class Q_touch extends Q {
    Q_touch( final String target, final Q ... srcs ) { super(target,' ',srcs); }
    protected ByteArrayOutputStream do_it( ) {
      System.out.println("touch "+_target); // 1-liner of build-step
      if( _justprint ) return null;
      File f = new File(TOP_PATH_SLASH+"/"+_target);
      try { 
        f.delete();
        f.createNewFile(); 
        // You would think that to delete & create the file would update the
        // lastMod time accurately, but on linux at least it appears it can be
        // created at least 1 msec in the past.
        long t = System.currentTimeMillis();
        f.setLastModified(t);
      } catch( IOException e ) {
        throw new BuildError("Unable to make file "+_target+": "+e.toString());
      }
      return null;              // No output from a 'touch'
    }
  }


  // --- cat_files -----------------------------------------------------------
  static void cat_files( final Q dst, final Q[] srcs ) {
    FileChannel targ = null;
    FileChannel srcx = null;
    try { 
      try {
        File f = new File(dst._target);
        f.createNewFile();
        targ = new FileOutputStream(f).getChannel();
        int offset = 0;
        for( Q src : srcs ) {
          if( srcx != null ) srcx.close();
          File f0 = new File(src._target);
          srcx = new FileInputStream(f0).getChannel();
          long crunk = targ.transferFrom(srcx, offset, srcx.size());
          offset += srcx.size();
        }
      } finally {
        if( targ != null ) targ.close();
        if( srcx != null ) srcx.close();
      }
    } catch( IOException e ) {
      throw new BuildError("Unable to cat into file "+dst._target+": "+e.toString());
    }
  }

  // --- copy/concat files ---------------------------------------------------
  // Copy/concat files
  static private class Qcat extends Q {
    Qcat( final String target, final Q ... srcs ) { super(target,' ',srcs); }
    protected ByteArrayOutputStream do_it( ) {
      if( _verbose > 1 ) System.out.println("cat "+_srcs+" > "+_target);
      else               System.out.println("Concating "+_target);
      if( _justprint ) return null;
      cat_files( this, _srcs );
      return null;              // No output from a 'concat'
    }
  }

  // --- ADLC ----------------------------------------------------------------
  // Run the ADLC
  static private class Qadlc extends QS {
    final String _adlc_dir;
    boolean _ran_adlc = false;
    Qadlc( final String adlc_dir, final String target, final String exec, final Q ... srcs ) { 
      super(adlc_dir+target,exec,' ',srcs); 
      _adlc_dir = adlc_dir;
    }

   protected ByteArrayOutputStream do_it( ) {
      final String exec = parse_exec();
      if( _verbose > 1 ) System.out.println(exec); // Print 1-liner on what the step is doing
      else System.out.println("ADLCing   "+_target);
      _ran_adlc = true;
      return _justprint ? null : sys_exec(exec, new File(_adlc_dir).getAbsoluteFile(), false); 
    }
  }

  // --- Compare/copy files --------------------------------------------------
  // Compare/copy files.  If the file "XXXX.?pp" differs from "XXXX_tmp.?pp"
  // then force a build-step.  The forced step is a copy from "XXXX_tmp.?pp"
  // over onto "XXXX.?pp".  Used by the ADLC to notice when files changed
  // content and not just date after running the ADLC.
  static private class Qcmp_cp extends Q {
    final Q _tmp;
    private Qcmp_cp( final String target, final Q tmp, final Q src ) { 
      super(target,' ',new Q[] {src});
      _tmp = tmp;
    }

    protected ByteArrayOutputStream do_it( ) {
      if( _verbose > 1 ) System.out.println("cat "+_tmp._target+" > "+_target);
      else               System.out.println("Copying   "+_target);
      if( _justprint ) return null;
      cat_files( this, new Q[] {_tmp} );
      return null;              // No output from a 'concat'
    }

    protected int extra_check() {
      // See if the tmp file has been recently built.  If so we need to also
      // do a file-contents compare.  If not... then there's no reason to
      // update the target file.
      if( _srcs[0]._modtime < _modtime ) return 0;
      if( _modtime==0 ) return 0; // our file does not exist, Do The Normal Thing
      // The parent is a ADLC-produced tmp file.  If we did not need to
      // run the ADLC, then the tmp cannot have changed.
      if( ((Qadlc)_srcs[0])._ran_adlc ) {
        // Ok, the ADLC ran producing a possibly-new tmp file.  See if these 2
        // files differ.  If not, then the existing file can keep it's current
        // timestamp.
        FileInputStream fsdst = null;
        FileInputStream fssrc = null;
        try {
          try { 
            final int len = (int)_dst.length();
            File tmpdst = new File(TOP,_tmp._target); // Assume TOP relative
            if( len != (int)tmpdst.length() ) return 2; // files differ: force "build" step

            fsdst = new FileInputStream(_dst);
            fssrc = new FileInputStream(tmpdst);
            final byte[] dbuf = new byte[len];
            for( int rd = 0; rd < len; rd += fsdst.read(dbuf,rd,len-rd) ) ;
            final byte[] sbuf = new byte[len];
            for( int rs = 0; rs < len; rs += fssrc.read(sbuf,rs,len-rs) ) ;
            for( int i=0; i<dbuf.length; i++ )
              if( dbuf[i] != sbuf[i] ) 
                return 2;         // files differ: force "build" step
            // Files are equal, no copy!
          } finally {
            if( fsdst != null ) fsdst.close();
            if( fssrc != null ) fssrc.close();
          }
        } catch( IOException e ) {
          throw new BuildError("Unable to cmp into file "+_target+": "+e.toString());
        }
      }
      // Files did not change, so remove the dependency of "ad_x86.hpp" from
      // "ad_x86_tmp.hpp" because "ad_x86.hpp" is not changing.  Note that the
      // temp file ad_x86_tmp.hpp can have a later timestamp than ad_x86.hpp
      // but since the tmp file is the same as the normal file, we do not need
      // to do anything to ad_x86.hpp to make it "up to date".
      synchronized(this) { _srcs = new Q[0]; }
      return 0;
    }
  }

  // =========================================================================
  // --- The Dependencies ----------------------------------------------------
  // =========================================================================


  // The build-self dependency every project needs
  static final Q _build_j = new Q("build/build.java");
  static final Q _build_c = new QS(BUILD_DIR+"/build.class", JAVAC + " -cp " + BUILD_DIR+" -d "+BUILD_DIR+" %src",_build_j);

  // --- Some tools
  static final String AZ_SWTOOLS_GCC_X86_DIR = _gcc_dir;
  static final String AZ_SWTOOLS_X86_DIR     = _binutils_dir;

  // ----------------------------------------------------------------------------
  // Compiler Warnings  
  // ----------------------------------------------------------------------------
  // The -w flag controls whether or not we generate and report warnings.  
  // After we've cleaned up the warnings, we'll make all warnings into errors.
  // ----------------------------------------------------------------------------
  static String gcc_warn()  { 
    String result;
    String warns=" -fdiagnostics-show-option "; // show options to control warnings
    String errors;
    if( _warn ) {  warns = warns + " -Wall "; }
    
    // These are warnings we don't care about and will allow
    warns = warns 
        + " -Wno-endif-labels "     // don't warn if there is text after a #endif or #else
        + " -Wno-reorder  "         // variables are initialized out of order
        + " -Wno-switch "           // A switch statement on an enum where not all values
                                    // are in a case statment and there is no default clause.
                                    // I think this should be an error, but the hotspot code
                                    // is too full of this stuff. so we'll ignore it.
        + " -Wno-unused-function "  // defined and not referenced funciton
        + " -Wno-unused-value  "    // found a null statement, for example  (0);
        + " -Wno-unused-variable"   // don't warn about declared and unused variables
        + " -Wno-write-strings "    // assigning string to char*. not a problem
        ;

    // Always make these warnings into errors.
    errors = " "
        + " -Waddress "                 // suspicious use of address  
        + " -Werror=char-subscripts "   // don't use char as an array index
        + " -Werror=comment "           // no nested comments
        + " -Wformat -Werror=format "   // ensure var args of printf (and others) match
        + " -Werror=format-extra-args " // no extra args at end of printf()
        + " -Werror=format-zero-length " // can't have a zero-length format statement
        + " -Werror=missing-braces "    // proper initialization format for arrays of structs
        + " -Werror=overflow "          // catch assigning 64-bit to 32-bit values
        + " -Werror=parentheses "       // no confusing expressions
        + " -Werror=pointer-arith "     // don't use NULL in integer comparisons        
        + " -Werror=return-type "       // make sure every function returns a value.
        + " -Werror=sign-compare "      // don't compare signed and unsigned quatities
        + " -Werror=sequence-point "    // unsafe/unpredictiable expression because of
                                        // sequence point violation(s)
        + " -Werror=strict-aliasing "   // code breaks strict aliasing rules
        + " -Werror=strict-overflow "   // detect optimizations that fail on overflow
                                        // see inline.hpp for explanation.
        + " -Werror=unused-label  "     // label declared and not used
        + " -Werror=volatile-register-var " // don't mark register variables as volatile
        ;

    //  -Werror=array-bounds    
    //  Still a bug in gc code when compiled -O3 -DASSERT.  Compile with -w option to see it.
    //  When that is fixed, then we can add this to the list of warnings that should be errors.
    
    //       -Werror=uninitialized
    //  This can be used only when optimizing.  
    //  It is put in the string GCC_WARN_OPTIMIZED. (see below)

    // return gcc warning string
    result = warns + errors;

    return result;
  }

  static String gcc_warn_optimized() {
    String errors="";
    if ( TARGET_ISA.equals("x86_64") ) {
        errors=" -Werror=uninitialized";
    }
    return errors;
  }


  static final String GCC_WARN           = gcc_warn();
  static final String GCC_WARN_OPTIMIZED = gcc_warn_optimized();

  // ----------------------------------------------------------------------------
  // THESE ARE COMPILER WARNINGS THAT SHOULD BE FIXED, as opposed to ignoring them
  //
  // I'll leave the comments here as documentation of the issues.  See the errors=...
  // list, above, to tell which have been dealt with.
  // ----------------------------------------------------------------------------
  //  -Wparentheses         Missing parentheses in expressions that might be wrong
  //                        and certainly cause confusion.  
  //                        [9/11/09 fixed. bean]
  // inline_function_used_but_never_defined 
  //                        This warning is a side effect of using the *.inline.hpp files.
  //                        Some code includes the *.hpp file but not the *.inline.hpp file.
  //                        In this case, it has a declaration of a method in the class that
  //                        is not ever defined.  
  //                        NOTE:  I have little hope of ever being able to  fix this.
  //  -Wunitialized         uninitialized variables.  We're starting off with about 600 cases.
  //                        [9/9/09 fixed all but one case in gpgc_marks.hpp:236]
  //  -Wmissing-braces      Warn if an aggregate or union initialized is not fully bracketed.
  //                        currently, found only in src/os/linux/vm/jvm_linux.cpp
  //                        [9/8/09 fixed. bean]
  //  -Wpointer-arith       Complains when NULL is used in an expression with an integer.
  //                        [fixed 9/10/09 bean]
  //  -Woverflow            I've seen two different problems that are likely errors.
  //                        (1) in arguments.cpp there is an integer overflow in compile time
  //                        computations. (2) in two other files, a 64bit signed integer is
  //                        truncated to a 32bit unsigned int in the middle of a computation.
  //                        Both of these are probably errors.
  //                        Example 1:  Copy::file_to_word(void*,size_t,int) is called from a couple
  //                        of places passing in (::badHeapWordVal), which is a 64 bit constant
  //                        0xBAADBABEBAADBABE.  But because of this error, memory is filled with
  //                        0xFFFFFFFFBAADBABE.  Later, when we check for badHeapWordVal, we 
  //                        wont find it.  called from space.cpp and psPromotionLAB.cpp.
  //                        This seems to clearly be an error.
  //                        Example 2:  C1CompileThreshold=(1<<31)-1 in arguments.cpp.
  //                        C1CompileThreshold is 64bits and this expression is maxint32 = 0x7fffffff.
  //                        Is this what was intended?  error?
  //                        Example 3: (related issue).  LIR_Assembler::profile_entry()
  //                        does cmp4i against C1CompileThreshold.  is this an error?
  //                        [9/1?/09: fixed]
  // overriding <method_name>     
  // deprecated covariant return type <method_name>
  //                        In a few places, a virtual method returning a pointer is initialized
  //                        to 0.  And then an implementation of the method in a subclass produces
  //                        this pair of warnings.  (I'm having a brain fart and don't understand
  //                        the problem)  This occurs in a few places and should just be fixed.
  //                        [fixed 9/10/09 bean]
  // left shift count >= width of type
  //                        This occurs only in src/src/vm/ci/ciInstanceKlass.cpp.  This *IS*
  //                        an error.  Currently, we build a mask incorrectly in this file.
  //                        [fixed 9/10/09 bean]
  // taking address of temporary
  //                        src/share/vm/prims/jvmtiExport.cpp:402
  //                        src/share/vm/classfile/vmSymbols.cpp:144,268
  //                        There isn't a way to turn off these warnings, so they need to be fixed.
  //                        I may be reading the code wrong, but it looks like the code takes an 
  //                        address of a reference, which is probably not what was intended.
  // -fcheck-new is in effect
  //                        The actual message is 'operator new' must not return NULL unless it
  //                        is declare ' throw() ' (or -fcheck-new is in effect)
  //                        src/share/vm/oops/klass.cpp:242
  //                        src/share/vm/memory/allocation.cpp:25,28
  //                        These allocators should throw an exception when they fail instead of
  //                        silently return NULL.  That is the C++ standard requirement for an
  //                        allocator.  (Using the -fcheck-new flag, the compiler will always 
  //                        do a NULL check anyway, by the way.)
  // operation on <var> may be undefined
  //                        src/share/vm/c1/c1_InstructionPrinter.cpp:233 
  //                        This should be fixed.  It is clearly a typo.
  //                        src/share/vm/c1/c1_LIRGenerator.cpp:1716 
  //                        This should be fixed.  It is clearly a typo.
  // -Wformat-extra-args
  //                        occurs in one place: src/hare/vm/runtime/compilationPolicy.cpp:364
  //                        and is clearly a bug.  It looks like the first argument 'caller_cnt' 
  //                        should not be there.
  // the use of 'tempnam' is dangerous, better use 'mkstemp'
  //                        This is in j2se6 code.  And while this is not a bug, I don't see any 
  //                        way to turn off this warning, so we may have to fix it.  Damn.
  //                        src/share/native/com/sun/java/util/jar/pack/unpack.cpp:4647.
  //                        [9/10/09 "fixed".  This is not a compiler warning, but
  //                        rather a linker warning that we can ignore.]
  // 'noreturn ' function does return
  // function declared 'noreturn' has a 'return ' statement
  //                        occurs in src/share/vm/utilities/debug.cpp:217
  //                        report_fatal() is marked as _NORETURN_ in the headerfile but the
  //                        actual implementation does return.  This can screw up the optimizer.
  //                        The right way to fix this is to create _report_fatal() _NORETURN_ 
  //                        and change report_fatal() to do the debugging check and call _report_fatal()
  //                        if it really wants to report and die.
  // -Wsign-compare         158 places in the code where we compare signed and unsigned values.  The 
  //                        first few I looked at were ok, but they can sometimes be real errors.
  //                        The most common case was that we'd declare a buflen to be an int and then 
  //                        compare it with a size_t (which is unsigned long).  I'd like to fix this,
  //                        but if we can't, it would be acceptable to turn off this warning.
  //                        [9/10/09 fixed all but one case in gpgc_relocation.cpp:89,90,115,116]
  // compare between        one spot compares values from two different enumerated types.  This 
  //                        should be fixed.  compare 'enum FRegister' and 'enum Register'
  //                        src/cpu/x86/vm/interpreterRT_x86.cpp:429
  //                        The defininitions are in src/cpu/x86/vm/register_pd.hpp, by the way.
  //                        [fixed 9/11/09 bean]
  // address of local variable returned
  //                        These look like real errors, but it's a little hard to tell.  They need 
  //                        to be fixed.
  //                        *  src/share/vm/prims/jvmtiExport.cpp:402
  //                        *  src/share/vm/classfile/vmSymbols.cpp:144,268
  //                        And this one is just plain hokey.  It attemps to guess at the current
  //                        stack pointer by taking the address of a local variable.  In stead it
  //                        should use an asm instruction.
  //                        *  src/os/linux/vm/os_linux.cpp:476
  // -Wstrict-overflow      This warns that the compiler is assuming an integer expression does not
  //                        overflow and, therefore, the compiler can optimize the expression.  
  //                        For example, the optimizer will assume (x<x+1) will always be true 
  //                        because it is equivalent to (0<1).
  //                        Example in our code:  has_hole_between() in c1_LinearScan.cpp has a 
  //                        line assert(hole_from<hole_to) that gets optimized away when compiled
  //                        -O3 because after inlining, it becomes assert(op_id, op_id+2).  There 
  //                        are several places where this occurs.
  //                        Example in our code:  patch_branches_impl() in assembler_x86.cpp calls
  //                        at(i) in several places.  The implementation of at(i) has an assert()
  //                        that is optimized away, similar to the above example.
  //                        Note:  there is an option, -fwrapv, that tells the compiler that  
  //                        integer overflow wraps.  Using this option, the compiler will 
  //                        optimize correctly, *but* you may pay a big performance penalty 
  //                        for using -fwrapv.  
  //                        Note:  I don't know how to fix this.  
  //                        [9/10/09 fixed]
  // -Warray-bounds         The compiler complains because it knows that an array bound is
  //                        out of range.
  //                        Example in our code:  update_and_deadwood_in_dense_prefix() gets
  //                        invoked with space_id being out of range.  I can't figure out how
  //                        this happens.  I hope someone can figure this out and fix it.
  //                    
  // ----------------------------------------------------------------------------
                                   
  static final String GCC_FLAGS   = "-fpic -fno-rtti -fno-exceptions -pipe -fmessage-length=0 " + GCC_WARN;


  static final String GCC_X86   = AZ_SWTOOLS_GCC_X86_DIR + "/g++ -fno-strict-aliasing -Di86pc -DAZPROF_NO_EVENTS " + GCC_FLAGS;
  static final String STRIP_X86 = AZ_SWTOOLS_X86_DIR + "/strip";
  static final String AR_X86    = AZ_SWTOOLS_X86_DIR + "/ar";

  static final String ELFSIGN   = SANDBOX+"/linux/bin/i686/elfsign -k "+SANDBOX+"/linux/keys.private/binpriXX.pem ";


  // --- Some HotSpot CPU-specific source files

  static final String CPU_X86                    = "src/cpu/x86/vm/";
  static final Q _assembler_x86_cpp              = new Q(CPU_X86+"assembler_x86.cpp");
  static final Q _assembler_pd_hpp               = new Q(CPU_X86+"assembler_pd.hpp");
  static final Q _assembler_x86_hpp              = new Q(CPU_X86+"assembler_x86.hpp");
  static final Q _bytecodes_x86_cpp              = new Q(CPU_X86+"bytecodes_x86.cpp");
  static final Q _bytecodes_x86_hpp              = new Q(CPU_X86+"bytecodes_pd.hpp");
  static final Q _bytes_pd_hpp                   = new Q(CPU_X86+"bytes_pd.hpp");
  static final Q _bytes_x86_hpp                  = new Q(CPU_X86+"bytes_x86.hpp");
  static final Q _c1_CodeStubs_x86_cpp           = new Q(CPU_X86+"c1_CodeStubs_x86.cpp");
  static final Q _c1_Defs_pd_hpp                 = new Q(CPU_X86+"c1_Defs_pd.hpp");
  static final Q _c1_Defs_x86_hpp                = new Q(CPU_X86+"c1_Defs_x86.hpp");
  static final Q _c1_FrameMap_x86_cpp            = new Q(CPU_X86+"c1_FrameMap_x86.cpp");
  static final Q _c1_FrameMap_pd_hpp             = new Q(CPU_X86+"c1_FrameMap_pd.hpp");
  static final Q _c1_FrameMap_x86_hpp            = new Q(CPU_X86+"c1_FrameMap_x86.hpp");
  static final Q _c1_LIRAssembler_x86_cpp        = new Q(CPU_X86+"c1_LIRAssembler_x86.cpp");
  static final Q _c1_LIRAssembler_x86_hpp        = new Q(CPU_X86+"c1_LIRAssembler_pd.hpp");
  static final Q _c1_LIRGenerator_x86_cpp        = new Q(CPU_X86+"c1_LIRGenerator_x86.cpp");
  static final Q _c1_LinearScan_x86_cpp          = new Q(CPU_X86+"c1_LinearScan_x86.cpp");
  static final Q _c1_LinearScan_pd_hpp           = new Q(CPU_X86+"c1_LinearScan_pd.hpp");
  static final Q _c1_LinearScan_x86_hpp          = new Q(CPU_X86+"c1_LinearScan_x86.hpp");
  static final Q _c1_MacroAssembler_x86_cpp      = new Q(CPU_X86+"c1_MacroAssembler_x86.cpp");
  static final Q _c1_MacroAssembler_pd_hpp       = new Q(CPU_X86+"c1_MacroAssembler_pd.hpp");
  static final Q _c1_MacroAssembler_x86_hpp      = new Q(CPU_X86+"c1_MacroAssembler_x86.hpp");
  static final Q _c1_Runtime1_x86_cpp            = new Q(CPU_X86+"c1_Runtime1_x86.cpp");
  static final Q _c1_globals_pd_hpp              = new Q(CPU_X86+"c1_globals_pd.hpp");
  static final Q _c1_globals_x86_hpp             = new Q(CPU_X86+"c1_globals_x86.hpp");
  static final Q _c2_globals_pd_hpp              = new Q(CPU_X86+"c2_globals_pd.hpp");
  static final Q _c2_globals_x86_hpp             = new Q(CPU_X86+"c2_globals_x86.hpp");
  static final Q _constants_x86_hpp              = new Q(CPU_X86+"constants_pd.hpp");
  static final Q _copy_pd_hpp                    = new Q(CPU_X86+"copy_pd.hpp");
  static final Q _copy_x86_hpp                   = new Q(CPU_X86+"copy_x86.hpp");
  static final Q _debug_x86_cpp                  = new Q(CPU_X86+"debug_x86.cpp");
  static final Q _disassembler_x86_cpp           = new Q(CPU_X86+"disassembler_x86.cpp");
  static final Q _disassembler_x86_hpp           = new Q(CPU_X86+"disassembler_pd.hpp");
  static final Q _frame_x86_cpp                  = new Q(CPU_X86+"frame_x86.cpp");
  static final Q _frame_x86_hpp                  = new Q(CPU_X86+"frame_pd.hpp");
  static final Q _frame_x86_inline_hpp           = new Q(CPU_X86+"frame_pd.inline.hpp");
  static final Q _global_defs_x86_hpp            = new Q(CPU_X86+"globalDefinitions_pd.hpp");
  static final Q _globals_pd_hpp                 = new Q(CPU_X86+"globals_pd.hpp");
  static final Q _globals_x86_hpp                = new Q(CPU_X86+"globals_x86.hpp");
  static final Q _gpgc_traps_x86_hpp             = new Q(CPU_X86+"gpgc_traps_pd.hpp");
  static final Q _heapRef_x86_hpp                = new Q(CPU_X86+"heapRef_pd.hpp");
  static final Q _heapRef_x86_inline_hpp         = new Q(CPU_X86+"heapRef_pd.inline.hpp");
  static final Q _icache_x86_cpp                 = new Q(CPU_X86+"icache_x86.cpp");
  static final Q _icache_pd_hpp                  = new Q(CPU_X86+"icache_pd.hpp");
  static final Q _icache_x86_hpp                 = new Q(CPU_X86+"icache_x86.hpp");
  static final Q _interp_masm_x86_cpp            = new Q(CPU_X86+"interp_masm_x86_64.cpp");
  static final Q _interp_masm_pd_hpp             = new Q(CPU_X86+"interp_masm_pd.hpp");
  static final Q _interp_masm_x86_hpp            = new Q(CPU_X86+"interp_masm_x86_64.hpp");
  static final Q _interpreterRT_x86_cpp          = new Q(CPU_X86+"interpreterRT_x86.cpp");
  static final Q _interpreterRT_x86_hpp          = new Q(CPU_X86+"interpreterRT_pd.hpp");
  static final Q _interpreter_x86_cpp            = new Q(CPU_X86+"interpreter_x86.cpp");
  static final Q _interpreter_x86_hpp            = new Q(CPU_X86+"interpreter_pd.hpp");
  static final Q _javaFrameAnchor_x86_hpp        = new Q(CPU_X86+"javaFrameAnchor_pd.hpp");
  static final Q _jniTypes_pd_hpp                = new Q(CPU_X86+"jniTypes_pd.hpp");
  static final Q _jniTypes_x86_hpp               = new Q(CPU_X86+"jniTypes_x86.hpp");
  static final Q _jni_md_pd_hpp                  = new Q(CPU_X86+"jni_md_pd.h");
  static final Q _jni_x86_hpp                    = new Q(CPU_X86+"jni_x86.h");
  static final Q _lvb_x86_hpp                    = new Q(CPU_X86+"lvb_pd.hpp");
  static final Q _lvb_pd_inline_hpp              = new Q(CPU_X86+"lvb_pd.inline.hpp");
  static final Q _nativeInst_pd_hpp              = new Q(CPU_X86+"nativeInst_pd.hpp");
  static final Q _nativeInst_x86_hpp             = new Q(CPU_X86+"nativeInst_x86.hpp");
  static final Q _nativeInst_x86_cpp             = new Q(CPU_X86+"nativeInst_x86.cpp");
  static final Q _objectRef_x86_cpp              = new Q(CPU_X86+"objectRef_x86.cpp");
  static final Q _objectRef_x86_hpp              = new Q(CPU_X86+"objectRef_pd.hpp");
  static final Q _objectRef_x86_inline_hpp       = new Q(CPU_X86+"objectRef_pd.inline.hpp");
  static final Q _pauselessTraps_x86_hpp         = new Q(CPU_X86+"pauselessTraps_pd.hpp");
  static final Q _refsHierarchy_x86_hpp          = new Q(CPU_X86+"refsHierarchy_pd.hpp");
  static final Q _register_x86_cpp               = new Q(CPU_X86+"register_x86.cpp");
  static final Q _register_x86_hpp               = new Q(CPU_X86+"register_pd.hpp");
  static final Q _register_x86_inline_hpp        = new Q(CPU_X86+"register_pd.inline.hpp");
  static final Q _sharedRuntime_x86_cpp          = new Q(CPU_X86+"sharedRuntime_x86_64.cpp");
  static final Q _stackRef_x86_cpp               = new Q(CPU_X86+"stackRef_x86.cpp");
  static final Q _stackRef_x86_hpp               = new Q(CPU_X86+"stackRef_pd.hpp");
  static final Q _stackRef_x86_inline_hpp        = new Q(CPU_X86+"stackRef_pd.inline.hpp");
  static final Q _stubGenerator_x86_cpp          = new Q(CPU_X86+"stubGenerator_x86_64.cpp");
  static final Q _stubRoutines_x86_cpp           = new Q(CPU_X86+"stubRoutines_x86_64.cpp");
  static final Q _stubRoutines_pd_hpp            = new Q(CPU_X86+"stubRoutines_pd.hpp");
  static final Q _stubRoutines_x86_hpp           = new Q(CPU_X86+"stubRoutines_x86_64.hpp");
  static final Q _templateTable_x86_cpp          = new Q(CPU_X86+"templateTable_x86_64.cpp");
  static final Q _templateTable_pd_hpp           = new Q(CPU_X86+"templateTable_pd.hpp");
  static final Q _templateTable_x86_hpp          = new Q(CPU_X86+"templateTable_x86_64.hpp");
  static final Q _tickProfiler_x86_cpp           = new Q(CPU_X86+"tickProfiler_x86.cpp");
  static final Q _thread_x86_hpp                 = new Q(CPU_X86+"thread_pd.hpp");
  static final Q _vm_version_x86_cpp             = new Q(CPU_X86+"vm_version_x86_64.cpp");
  static final Q _vm_version_pd_hpp              = new Q(CPU_X86+"vm_version_pd.hpp");
  static final Q _vm_version_x86_hpp             = new Q(CPU_X86+"vm_version_x86_64.hpp");
  static final Q _vtableStubs_x86_cpp            = new Q(CPU_X86+"vtableStubs_x86_64.cpp");
  static final Q _x86_ad                         = new Q(CPU_X86+"x86_64.ad");

  // --- Some HotSpot OS-specific source files

  static final String INCLUDES_AVX               = " -I "+_avx_includes;
  static final String INCLUDES_AZLINUX           = INCLUDES_AVX + " -I "+SANDBOX+"/azlinux/include/";
  static final String INCLUDES_AZPROXIED         = INCLUDES_AVX + " -I "+SANDBOX+"/azproxied/include/";

  static final String OS_LINUX                   = "src/os/linux/vm/";
  static final Q _attachListener_linux_cpp       = new Q(OS_LINUX+"attachListener_linux.cpp");
  static final Q _c1_globals_os_hpp              = new Q(OS_LINUX+"c1_globals_os.hpp");
  static final Q _c1_globals_linux_hpp           = new Q(OS_LINUX+"c1_globals_linux.hpp");
  static final Q _c2_globals_os_hpp              = new Q(OS_LINUX+"c2_globals_os.hpp");
  static final Q _c2_globals_linux_hpp           = new Q(OS_LINUX+"c2_globals_linux.hpp");
  static final Q _chaitin_linux_cpp              = new Q(OS_LINUX+"chaitin_linux.cpp");
  static final Q _global_defs_linux_hpp          = new Q(OS_LINUX+"globalDefinitions_os.hpp");
  static final Q _globals_os_hpp                 = new Q(OS_LINUX+"globals_os.hpp");
  static final Q _globals_linux_hpp              = new Q(OS_LINUX+"globals_linux.hpp");
  static final Q _gpgc_linux_cpp                 = new Q(OS_LINUX+"gpgc_linux.cpp");
  static final Q _hpi_linux_cpp                  = new Q(OS_LINUX+"hpi_linux.cpp");
  static final Q _hpi_os_hpp                     = new Q(OS_LINUX+"hpi_os.hpp");
  static final Q _hpi_linux_hpp                  = new Q(OS_LINUX+"hpi_linux.hpp");
  static final Q _jvm_linux_cpp                  = new Q(OS_LINUX+"jvm_linux.cpp");
  static final Q _jvm_os_h                       = new Q(OS_LINUX+"jvm_os.h");
  static final Q _jvm_linux_h                    = new Q(OS_LINUX+"jvm_linux.h");
  static final Q _mutex_linux_cpp                = new Q(OS_LINUX+"mutex_linux.cpp");
  static final Q _osThread_linux_cpp             = new Q(OS_LINUX+"osThread_linux.cpp");
  static final Q _osThread_os_hpp                = new Q(OS_LINUX+"osThread_os.hpp");
  static final Q _osThread_linux_hpp             = new Q(OS_LINUX+"osThread_linux.hpp");
  static final Q _os_linux_cpp                   = new Q(OS_LINUX+"os_linux.cpp");
  static final Q _os_os_hpp                      = new Q(OS_LINUX+"os_os.hpp");
  static final Q _os_linux_hpp                   = new Q(OS_LINUX+"os_linux.hpp");
  static final Q _os_linux_os_hpp                = new Q(OS_LINUX+"os_os.inline.hpp");
  static final Q _os_linux_inline_hpp            = new Q(OS_LINUX+"os_linux.inline.hpp");
  static final Q _thread_linux_inline_hpp        = new Q(OS_LINUX+"thread_os.inline.hpp");
  static final Q _vmError_linux_cpp              = new Q(OS_LINUX+"vmError_linux.cpp");

  // --- Some HotSpot OS-CPU-specific source files
  static final String OS_CPU_LINUX_X86           = "src/os_cpu/linux_x86/vm/";
  static final Q _assembler_linux_x86_cpp        = new Q(OS_CPU_LINUX_X86+"assembler_linux_x86_64.cpp");
  static final Q _atomic_os_pd_inline_hpp        = new Q(OS_CPU_LINUX_X86+"atomic_os_pd.inline.hpp");
  static final Q _atomic_linux_x86_inline_hpp    = new Q(OS_CPU_LINUX_X86+"atomic_linux_x86.inline.hpp");
  static final Q _gctrap_linux_x86_cpp           = new Q(OS_CPU_LINUX_X86+"gctrap_linux_x86.cpp");
  static final Q _gctrap_linux_x86_inline_hpp    = new Q(OS_CPU_LINUX_X86+"gctrap_os_pd.inline.hpp");
  static final Q _globals_os_pd_hpp              = new Q(OS_CPU_LINUX_X86+"globals_os_pd.hpp");
  static final Q _globals_linux_x86_hpp          = new Q(OS_CPU_LINUX_X86+"globals_linux_x86.hpp");
  static final Q _orderAccess_os_pd_hpp          = new Q(OS_CPU_LINUX_X86+"orderAccess_os_pd.inline.hpp");
  static final Q _orderAccess_linux_x86_hpp      = new Q(OS_CPU_LINUX_X86+"orderAccess_linux_x86.inline.hpp");
  static final Q _os_linux_x86_cpp               = new Q(OS_CPU_LINUX_X86+"os_linux_x86.cpp");
  static final Q _os_linux_x86_hpp               = new Q(OS_CPU_LINUX_X86+"os_os_pd.hpp");
  static final Q _prefetch_os_pd_hpp             = new Q(OS_CPU_LINUX_X86+"prefetch_os_pd.inline.hpp");
  static final Q _prefetch_linux_x86_hpp         = new Q(OS_CPU_LINUX_X86+"prefetch_linux_x86.inline.hpp");
  static final Q _threadLS_linux_x86_hpp         = new Q(OS_CPU_LINUX_X86+"threadLS_os_pd.hpp");
  static final Q _thread_linux_x86_cpp           = new Q(OS_CPU_LINUX_X86+"thread_linux_x86.cpp");
  static final Q _thread_os_pd_hpp               = new Q(OS_CPU_LINUX_X86+"thread_os_pd.hpp");
  static final Q _thread_linux_x86_hpp           = new Q(OS_CPU_LINUX_X86+"thread_linux_x86.hpp");
  static final Q _linux_x86_64_ad                = new Q(OS_CPU_LINUX_X86+"linux_x86_64.ad");

  // --- Some HotSpot source files
  static final String AZUL_CODE                  = "src/azshare/vm/code/";
  static final Q _codeBlob_cpp                   = new Q(AZUL_CODE+"codeBlob.cpp");
  static final Q _codeBlob_hpp                   = new Q(AZUL_CODE+"codeBlob.hpp");
  static final Q _codeCache_cpp                  = new Q(AZUL_CODE+"codeCache.cpp");
  static final Q _codeCache_hpp                  = new Q(AZUL_CODE+"codeCache.hpp");
  static final Q _compiledIC_cpp                 = new Q(AZUL_CODE+"compiledIC.cpp");
  static final Q _compiledIC_hpp                 = new Q(AZUL_CODE+"compiledIC.hpp");
  static final Q _commonAsm_cpp                  = new Q(AZUL_CODE+"commonAsm.cpp");
  static final Q _commonAsm_hpp                  = new Q(AZUL_CODE+"commonAsm.hpp");
  static final Q _pcMap_cpp                      = new Q(AZUL_CODE+"pcMap.cpp");
  static final Q _pcMap_hpp                      = new Q(AZUL_CODE+"pcMap.hpp");
  static final Q _vreg_hpp                       = new Q(AZUL_CODE+"vreg.hpp");

  static final String AZUL_GC_SHARED             = "src/azshare/vm/gc_implementation/shared/";
  static final Q _auditTrail_cpp                 = new Q(AZUL_GC_SHARED+"auditTrail.cpp");
  static final Q _auditTrail_hpp                 = new Q(AZUL_GC_SHARED+"auditTrail.hpp");
  static final Q _auditTrail_inline_hpp          = new Q(AZUL_GC_SHARED+"auditTrail.inline.hpp");
  static final Q _auditTrail_inline2_hpp         = new Q(AZUL_GC_SHARED+"auditTrail.inline2.hpp");
  static final Q _cycleCounts_cpp                = new Q(AZUL_GC_SHARED+"cycleCounts.cpp");
  static final Q _cycleCounts_hpp                = new Q(AZUL_GC_SHARED+"cycleCounts.hpp");
  static final Q _heapRefBuffer_cpp              = new Q(AZUL_GC_SHARED+"heapRefBuffer.cpp");
  static final Q _heapRefBuffer_hpp              = new Q(AZUL_GC_SHARED+"heapRefBuffer.hpp");
  static final Q _lvb_cpp                        = new Q(AZUL_GC_SHARED+"lvb.cpp");
  static final Q _lvb_hpp                        = new Q(AZUL_GC_SHARED+"lvb.hpp");
  static final Q _lvbClosures_hpp                = new Q(AZUL_GC_SHARED+"lvbClosures.hpp");
  static final Q _markWord_cpp                   = new Q(AZUL_GC_SHARED+"markWord.cpp");
  static final Q _markWord_hpp                   = new Q(AZUL_GC_SHARED+"markWord.hpp");
  static final Q _markWord_inline_hpp            = new Q(AZUL_GC_SHARED+"markWord.inline.hpp");
  static final Q _nmt_hpp                        = new Q(AZUL_GC_SHARED+"nmt.hpp");
  static final Q _pauselessTraps_hpp             = new Q(AZUL_GC_SHARED+"pauselessTraps.hpp");
  static final Q _pgcTaskManager_cpp             = new Q(AZUL_GC_SHARED+"pgcTaskManager.cpp");
  static final Q _pgcTaskManager_hpp             = new Q(AZUL_GC_SHARED+"pgcTaskManager.hpp");
  static final Q _pgcTaskThread_cpp              = new Q(AZUL_GC_SHARED+"pgcTaskThread.cpp");
  static final Q _pgcTaskThread_hpp              = new Q(AZUL_GC_SHARED+"pgcTaskThread.hpp");
                                                 
  static final String AZUL_GC_GPGC               = "src/azshare/vm/gc_implementation/genPauseless/";
  static final Q _gpgc_cardTable_cpp             = new Q(AZUL_GC_GPGC+"gpgc_cardTable.cpp");
  static final Q _gpgc_cardTable_hpp             = new Q(AZUL_GC_GPGC+"gpgc_cardTable.hpp");
  static final Q _gpgc_closures_cpp              = new Q(AZUL_GC_GPGC+"gpgc_closures.cpp");
  static final Q _gpgc_closures_hpp              = new Q(AZUL_GC_GPGC+"gpgc_closures.hpp");
  static final Q _gpgc_collector_cpp             = new Q(AZUL_GC_GPGC+"gpgc_collector.cpp");
  static final Q _gpgc_collector_hpp             = new Q(AZUL_GC_GPGC+"gpgc_collector.hpp");
  static final Q _gpgc_debug_cpp                 = new Q(AZUL_GC_GPGC+"gpgc_debug.cpp");
  static final Q _gpgc_debug_hpp                 = new Q(AZUL_GC_GPGC+"gpgc_debug.hpp");
  static final Q _gpgc_gcManagerMark_cpp         = new Q(AZUL_GC_GPGC+"gpgc_gcManagerMark.cpp");
  static final Q _gpgc_gcManagerMark_hpp         = new Q(AZUL_GC_GPGC+"gpgc_gcManagerMark.hpp");
  static final Q _gpgc_gcManagerNew_cpp          = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNew.cpp");
  static final Q _gpgc_gcManagerNew_hpp          = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNew.hpp");
  static final Q _gpgc_gcManagerNewFinal_cpp     = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewFinal.cpp");
  static final Q _gpgc_gcManagerNewFinal_hpp     = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewFinal.hpp");
  static final Q _gpgc_gcManagerNewFinal_i_hpp   = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewFinal.inline.hpp");
  static final Q _gpgc_gcManagerNewReloc_cpp     = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewReloc.cpp");
  static final Q _gpgc_gcManagerNewReloc_hpp     = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewReloc.hpp");
  static final Q _gpgc_gcManagerNewStrong_cpp    = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewStrong.cpp");
  static final Q _gpgc_gcManagerNewStrong_hpp    = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewStrong.hpp");
  static final Q _gpgc_gcManagerNewStrong_i_hpp  = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewStrong.inline.hpp");
  static final Q _gpgc_gcManagerNewStrong_i2_hpp = new Q(AZUL_GC_GPGC+"gpgc_gcManagerNewStrong.inline2.hpp");
  static final Q _gpgc_gcManagerOld_cpp          = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOld.cpp");
  static final Q _gpgc_gcManagerOld_hpp          = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOld.hpp");
  static final Q _gpgc_gcManagerOldFinal_cpp     = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOldFinal.cpp");
  static final Q _gpgc_gcManagerOldFinal_hpp     = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOldFinal.hpp");
  static final Q _gpgc_gcManagerOldFinal_i_hpp   = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOldFinal.inline.hpp");
  static final Q _gpgc_gcManagerOldReloc_cpp     = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOldReloc.cpp");
  static final Q _gpgc_gcManagerOldReloc_hpp     = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOldReloc.hpp");
  static final Q _gpgc_gcManagerOldStrong_cpp    = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOldStrong.cpp");
  static final Q _gpgc_gcManagerOldStrong_hpp    = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOldStrong.hpp");
  static final Q _gpgc_gcManagerOldStrong_i_hpp  = new Q(AZUL_GC_GPGC+"gpgc_gcManagerOldStrong.inline.hpp");
  static final Q _gpgc_gcManager_hpp             = new Q(AZUL_GC_GPGC+"gpgc_gcManager.hpp");
  static final Q _gpgc_generation_cpp            = new Q(AZUL_GC_GPGC+"gpgc_generation.cpp");
  static final Q _gpgc_generation_hpp            = new Q(AZUL_GC_GPGC+"gpgc_generation.hpp");
  static final Q _gpgc_heap_cpp                  = new Q(AZUL_GC_GPGC+"gpgc_heap.cpp");
  static final Q _gpgc_heap_hpp                  = new Q(AZUL_GC_GPGC+"gpgc_heap.hpp");
  static final Q _gpgc_heuristic_cpp             = new Q(AZUL_GC_GPGC+"gpgc_heuristic.cpp");
  static final Q _gpgc_heuristic_hpp             = new Q(AZUL_GC_GPGC+"gpgc_heuristic.hpp");
  static final Q _gpgc_interlock_cpp             = new Q(AZUL_GC_GPGC+"gpgc_interlock.cpp");
  static final Q _gpgc_interlock_hpp             = new Q(AZUL_GC_GPGC+"gpgc_interlock.hpp");
  static final Q _gpgc_layout_cpp                = new Q(AZUL_GC_GPGC+"gpgc_layout.cpp");
  static final Q _gpgc_layout_hpp                = new Q(AZUL_GC_GPGC+"gpgc_layout.hpp");
  static final Q _gpgc_layout_i_hpp              = new Q(AZUL_GC_GPGC+"gpgc_layout.inline.hpp");
  static final Q _gpgc_lvb_cpp                   = new Q(AZUL_GC_GPGC+"gpgc_lvb.cpp");
  static final Q _gpgc_lvb_hpp                   = new Q(AZUL_GC_GPGC+"gpgc_lvb.hpp");
  static final Q _gpgc_markAlgorithms_hpp        = new Q(AZUL_GC_GPGC+"gpgc_markAlgorithms.hpp");
  static final Q _gpgc_marker_cpp                = new Q(AZUL_GC_GPGC+"gpgc_marker.cpp");
  static final Q _gpgc_marker_hpp                = new Q(AZUL_GC_GPGC+"gpgc_marker.hpp");
  static final Q _gpgc_markingQueue_cpp          = new Q(AZUL_GC_GPGC+"gpgc_markingQueue.cpp");
  static final Q _gpgc_markingQueue_hpp          = new Q(AZUL_GC_GPGC+"gpgc_markingQueue.hpp");
  static final Q _gpgc_markIterator_cpp          = new Q(AZUL_GC_GPGC+"gpgc_markIterator.cpp");
  static final Q _gpgc_markIterator_hpp          = new Q(AZUL_GC_GPGC+"gpgc_markIterator.hpp");
  static final Q _gpgc_marks_cpp                 = new Q(AZUL_GC_GPGC+"gpgc_marks.cpp");
  static final Q _gpgc_marks_hpp                 = new Q(AZUL_GC_GPGC+"gpgc_marks.hpp");
  static final Q _gpgc_metadata_cpp              = new Q(AZUL_GC_GPGC+"gpgc_metadata.cpp");
  static final Q _gpgc_metadata_hpp              = new Q(AZUL_GC_GPGC+"gpgc_metadata.hpp");
  static final Q _gpgc_multiPageSpace_cpp        = new Q(AZUL_GC_GPGC+"gpgc_multiPageSpace.cpp");
  static final Q _gpgc_multiPageSpace_hpp        = new Q(AZUL_GC_GPGC+"gpgc_multiPageSpace.hpp");
  static final Q _gpgc_newCollector_hpp          = new Q(AZUL_GC_GPGC+"gpgc_newCollector.hpp");
  static final Q _gpgc_newCollector_inline_hpp   = new Q(AZUL_GC_GPGC+"gpgc_newCollector.inline.hpp");
  static final Q _gpgc_newCollector_main_cpp     = new Q(AZUL_GC_GPGC+"gpgc_newCollector.main.cpp");
  static final Q _gpgc_newCollector_mark_cpp     = new Q(AZUL_GC_GPGC+"gpgc_newCollector.mark.cpp");
  static final Q _gpgc_newCollector_misc_cpp     = new Q(AZUL_GC_GPGC+"gpgc_newCollector.misc.cpp");
  static final Q _gpgc_newCollector_reloc_cpp    = new Q(AZUL_GC_GPGC+"gpgc_newCollector.reloc.cpp");
  static final Q _gpgc_newCollector_traps_cpp    = new Q(AZUL_GC_GPGC+"gpgc_newCollector.traps.cpp");
  static final Q _gpgc_newCollector_verify_cpp   = new Q(AZUL_GC_GPGC+"gpgc_newCollector.verify.cpp");
  static final Q _gpgc_nmt_cpp                   = new Q(AZUL_GC_GPGC+"gpgc_nmt.cpp");
  static final Q _gpgc_nmt_hpp                   = new Q(AZUL_GC_GPGC+"gpgc_nmt.hpp");
  static final Q _gpgc_oldCollector_hpp          = new Q(AZUL_GC_GPGC+"gpgc_oldCollector.hpp");
  static final Q _gpgc_oldCollector_inline_hpp   = new Q(AZUL_GC_GPGC+"gpgc_oldCollector.inline.hpp");
  static final Q _gpgc_oldCollector_main_cpp     = new Q(AZUL_GC_GPGC+"gpgc_oldCollector.main.cpp");
  static final Q _gpgc_oldCollector_mark_cpp     = new Q(AZUL_GC_GPGC+"gpgc_oldCollector.mark.cpp");
  static final Q _gpgc_oldCollector_misc_cpp     = new Q(AZUL_GC_GPGC+"gpgc_oldCollector.misc.cpp");
  static final Q _gpgc_oldCollector_reloc_cpp    = new Q(AZUL_GC_GPGC+"gpgc_oldCollector.reloc.cpp");
  static final Q _gpgc_oldCollector_traps_cpp    = new Q(AZUL_GC_GPGC+"gpgc_oldCollector.traps.cpp");
  static final Q _gpgc_oldCollector_verify_cpp   = new Q(AZUL_GC_GPGC+"gpgc_oldCollector.verify.cpp");
  static final Q _gpgc_onePageSpace_cpp          = new Q(AZUL_GC_GPGC+"gpgc_onePageSpace.cpp");
  static final Q _gpgc_onePageSpace_hpp          = new Q(AZUL_GC_GPGC+"gpgc_onePageSpace.hpp");
  static final Q _gpgc_operation_cpp             = new Q(AZUL_GC_GPGC+"gpgc_operation.cpp");
  static final Q _gpgc_operation_hpp             = new Q(AZUL_GC_GPGC+"gpgc_operation.hpp");
  static final Q _gpgc_pageAudit_cpp             = new Q(AZUL_GC_GPGC+"gpgc_pageAudit.cpp");
  static final Q _gpgc_pageAudit_hpp             = new Q(AZUL_GC_GPGC+"gpgc_pageAudit.hpp");
  static final Q _gpgc_pageBudget_cpp            = new Q(AZUL_GC_GPGC+"gpgc_pageBudget.cpp");
  static final Q _gpgc_pageBudget_hpp            = new Q(AZUL_GC_GPGC+"gpgc_pageBudget.hpp");
  static final Q _gpgc_pageInfo_cpp              = new Q(AZUL_GC_GPGC+"gpgc_pageInfo.cpp");
  static final Q _gpgc_pageInfo_hpp              = new Q(AZUL_GC_GPGC+"gpgc_pageInfo.hpp");
  static final Q _gpgc_pageInfo_inline_hpp       = new Q(AZUL_GC_GPGC+"gpgc_pageInfo.inline.hpp");
  static final Q _gpgc_population_cpp            = new Q(AZUL_GC_GPGC+"gpgc_population.cpp");
  static final Q _gpgc_population_hpp            = new Q(AZUL_GC_GPGC+"gpgc_population.hpp");
  static final Q _gpgc_population_inline_hpp     = new Q(AZUL_GC_GPGC+"gpgc_population.inline.hpp");
  static final Q _gpgc_readTrapArray_cpp         = new Q(AZUL_GC_GPGC+"gpgc_readTrapArray.cpp");
  static final Q _gpgc_readTrapArray_hpp         = new Q(AZUL_GC_GPGC+"gpgc_readTrapArray.hpp");
  static final Q _gpgc_readTrapArray_inline_hpp  = new Q(AZUL_GC_GPGC+"gpgc_readTrapArray.inline.hpp");
  static final Q _gpgc_javaLangRefHandler_cpp    = new Q(AZUL_GC_GPGC+"gpgc_javaLangRefHandler.cpp");
  static final Q _gpgc_javaLangRefHandler_hpp    = new Q(AZUL_GC_GPGC+"gpgc_javaLangRefHandler.hpp");
  static final Q _gpgc_relocation_cpp            = new Q(AZUL_GC_GPGC+"gpgc_relocation.cpp");
  static final Q _gpgc_relocation_hpp            = new Q(AZUL_GC_GPGC+"gpgc_relocation.hpp");
  static final Q _gpgc_relocation_inline_hpp     = new Q(AZUL_GC_GPGC+"gpgc_relocation.inline.hpp");
  static final Q _gpgc_relocationSpike_hpp       = new Q(AZUL_GC_GPGC+"gpgc_relocationSpike.hpp");
  static final Q _gpgc_rendezvous_cpp            = new Q(AZUL_GC_GPGC+"gpgc_rendezvous.cpp");
  static final Q _gpgc_rendezvous_hpp            = new Q(AZUL_GC_GPGC+"gpgc_rendezvous.hpp");
  static final Q _gpgc_safepoint_cpp             = new Q(AZUL_GC_GPGC+"gpgc_safepoint.cpp");
  static final Q _gpgc_safepoint_hpp             = new Q(AZUL_GC_GPGC+"gpgc_safepoint.hpp");
  static final Q _gpgc_slt_cpp                   = new Q(AZUL_GC_GPGC+"gpgc_slt.cpp");
  static final Q _gpgc_slt_hpp                   = new Q(AZUL_GC_GPGC+"gpgc_slt.hpp");
  static final Q _gpgc_space_cpp                 = new Q(AZUL_GC_GPGC+"gpgc_space.cpp");
  static final Q _gpgc_space_hpp                 = new Q(AZUL_GC_GPGC+"gpgc_space.hpp");
  static final Q _gpgc_sparseMappedSpace_cpp     = new Q(AZUL_GC_GPGC+"gpgc_sparseMappedSpace.cpp");
  static final Q _gpgc_sparseMappedSpace_hpp     = new Q(AZUL_GC_GPGC+"gpgc_sparseMappedSpace.hpp");
  static final Q _gpgc_stats_cpp                 = new Q(AZUL_GC_GPGC+"gpgc_stats.cpp");
  static final Q _gpgc_stats_hpp                 = new Q(AZUL_GC_GPGC+"gpgc_stats.hpp");
  static final Q _gpgc_tasks_cpp                 = new Q(AZUL_GC_GPGC+"gpgc_tasks.cpp");
  static final Q _gpgc_tasks_hpp                 = new Q(AZUL_GC_GPGC+"gpgc_tasks.hpp");
  static final Q _gpgc_threadCleaner_cpp         = new Q(AZUL_GC_GPGC+"gpgc_threadCleaner.cpp");
  static final Q _gpgc_threadCleaner_hpp         = new Q(AZUL_GC_GPGC+"gpgc_threadCleaner.hpp");
  static final Q _gpgc_thread_cpp                = new Q(AZUL_GC_GPGC+"gpgc_thread.cpp");
  static final Q _gpgc_thread_hpp                = new Q(AZUL_GC_GPGC+"gpgc_thread.hpp");
  static final Q _gpgc_threadRefLists_cpp        = new Q(AZUL_GC_GPGC+"gpgc_threadRefLists.cpp");
  static final Q _gpgc_threadRefLists_hpp        = new Q(AZUL_GC_GPGC+"gpgc_threadRefLists.hpp");
  static final Q _gpgc_tlb_cpp                   = new Q(AZUL_GC_GPGC+"gpgc_tlb.cpp");
  static final Q _gpgc_tlb_hpp                   = new Q(AZUL_GC_GPGC+"gpgc_tlb.hpp");
  static final Q _gpgc_traps_hpp                 = new Q(AZUL_GC_GPGC+"gpgc_traps.hpp");
  static final Q _gpgc_verifyClosure_cpp         = new Q(AZUL_GC_GPGC+"gpgc_verifyClosure.cpp");
  static final Q _gpgc_verifyClosure_hpp         = new Q(AZUL_GC_GPGC+"gpgc_verifyClosure.hpp");
  static final Q _gpgc_verify_tasks_cpp         = new Q(AZUL_GC_GPGC+"gpgc_verify_tasks.cpp");
  static final Q _gpgc_verify_tasks_hpp         = new Q(AZUL_GC_GPGC+"gpgc_verify_tasks.hpp");

  static final String AZUL_OOPS                  = "src/azshare/vm/oops/";
  static final Q _methodCodeKlass_cpp            = new Q(AZUL_OOPS+"methodCodeKlass.cpp");
  static final Q _methodCodeKlass_hpp            = new Q(AZUL_OOPS+"methodCodeKlass.hpp");
  static final Q _methodCodeOop_cpp              = new Q(AZUL_OOPS+"methodCodeOop.cpp");
  static final Q _methodCodeOop_hpp              = new Q(AZUL_OOPS+"methodCodeOop.hpp");

  static final String AZUL_ARTA                  = "src/azshare/vm/arta/";
  static final Q _allocatedObjects_cpp           = new Q(AZUL_ARTA+"allocatedObjects.cpp");
  static final Q _allocatedObjects_hpp           = new Q(AZUL_ARTA+"allocatedObjects.hpp");
  static final Q _liveObjects_cpp                = new Q(AZUL_ARTA+"liveObjects.cpp");
  static final Q _liveObjects_hpp                = new Q(AZUL_ARTA+"liveObjects.hpp");
  static final Q _responseStream_hpp             = new Q(AZUL_ARTA+"responseStream.hpp");
  static final Q _artaObjects_cpp                = new Q(AZUL_ARTA+"artaObjects.cpp");
  static final Q _artaObjects_hpp                = new Q(AZUL_ARTA+"artaObjects.hpp");
  static final Q _artaQuery_cpp                  = new Q(AZUL_ARTA+"artaQuery.cpp");
  static final Q _artaQuery_hpp                  = new Q(AZUL_ARTA+"artaQuery.hpp");
  static final Q _artaThreadState_cpp            = new Q(AZUL_ARTA+"artaThreadState.cpp");
  static final Q _artaThreadState_hpp            = new Q(AZUL_ARTA+"artaThreadState.hpp");

  static final String AZUL_COMPILER              = "src/azshare/vm/compiler/";
  static final Q _codeProfile_cpp                = new Q(AZUL_COMPILER+"codeProfile.cpp");
  static final Q _codeProfile_hpp                = new Q(AZUL_COMPILER+"codeProfile.hpp");
  static final Q _compileBroker_hpp              = new Q(AZUL_COMPILER+"compileBroker.hpp");
  static final Q _compileBroker_cpp              = new Q(AZUL_COMPILER+"compileBroker.cpp");
  static final Q _freezeAndMelt_cpp              = new Q(AZUL_COMPILER+"freezeAndMelt.cpp");
  static final Q _freezeAndMelt_hpp              = new Q(AZUL_COMPILER+"freezeAndMelt.hpp");

  static final String AZUL_RUNTIME               = "src/azshare/vm/runtime/";
  static final Q _codeCacheOopTable_cpp          = new Q(AZUL_RUNTIME+"codeCacheOopTable.cpp");
  static final Q _deoptimization_cpp             = new Q(AZUL_RUNTIME+"deoptimization.cpp");
  static final Q _deoptimization_hpp             = new Q(AZUL_RUNTIME+"deoptimization.hpp");
  static final Q _flatHashSet_hpp                = new Q(AZUL_RUNTIME+"flatHashSet.hpp");
  static final Q _heapIterator_cpp               = new Q(AZUL_RUNTIME+"heapIterator.cpp");
  static final Q _heapIterator_hpp               = new Q(AZUL_RUNTIME+"heapIterator.hpp");
  static final Q _instructionTraceRecording_hpp  = new Q(AZUL_RUNTIME+"instructionTraceRecording.hpp");
  static final Q _invertedVirtualspace_cpp       = new Q(AZUL_RUNTIME+"invertedVirtualspace.cpp");
  static final Q _invertedVirtualspace_hpp       = new Q(AZUL_RUNTIME+"invertedVirtualspace.hpp");
  static final Q _klassIds_cpp                   = new Q(AZUL_RUNTIME+"klassIds.cpp");
  static final Q _klassIds_hpp                   = new Q(AZUL_RUNTIME+"klassIds.hpp");
  static final Q _klassTable_cpp                 = new Q(AZUL_RUNTIME+"klassTable.cpp");
  static final Q _log_cpp                        = new Q(AZUL_RUNTIME+"log.cpp");
  static final Q _log_hpp                        = new Q(AZUL_RUNTIME+"log.hpp");
  static final Q _modules_hpp                    = new Q(AZUL_RUNTIME+"modules.hpp");
  static final Q _mutexLocker_cpp                = new Q(AZUL_RUNTIME+"mutexLocker.cpp");
  static final Q _mutexLocker_hpp                = new Q(AZUL_RUNTIME+"mutexLocker.hpp");
  static final Q _mutex_cpp                      = new Q(AZUL_RUNTIME+"mutex.cpp");
  static final Q _mutex_hpp                      = new Q(AZUL_RUNTIME+"mutex.hpp");
  static final Q _mutex_inline_hpp               = new Q(AZUL_RUNTIME+"mutex.inline.hpp");
  static final Q _noinline_cpp                   = new Q(AZUL_RUNTIME+"noinline.cpp");
  static final Q _noinline_hpp                   = new Q(AZUL_RUNTIME+"noinline.hpp");
  static final Q _objectMonitor_hpp              = new Q(AZUL_RUNTIME+"objectMonitor.hpp");
  static final Q _objectMonitor_cpp              = new Q(AZUL_RUNTIME+"objectMonitor.cpp");
  static final Q _oopTable_cpp                   = new Q(AZUL_RUNTIME+"oopTable.cpp");
  static final Q _oopTable_hpp                   = new Q(AZUL_RUNTIME+"oopTable.hpp");
  static final Q _remoteJNI_cpp                  = new Q(AZUL_RUNTIME+"remoteJNI.cpp");
  static final Q _remoteJNI_hpp                  = new Q(AZUL_RUNTIME+"remoteJNI.hpp");
  static final Q _rlimits_cpp                    = new Q(AZUL_RUNTIME+"rlimits.cpp");
  static final Q _rlimits_hpp                    = new Q(AZUL_RUNTIME+"rlimits.hpp");
  static final Q _safepointTimes_hpp             = new Q(AZUL_RUNTIME+"safepointTimes.hpp");
  static final Q _sbaThreadInfo_cpp              = new Q(AZUL_RUNTIME+"sbaThreadInfo.cpp");
  static final Q _sbaThreadInfo_hpp              = new Q(AZUL_RUNTIME+"sbaThreadInfo.hpp");
  static final Q _sharedUserData_cpp             = new Q(AZUL_RUNTIME+"sharedUserData.cpp");
  static final Q _sharedUserData_hpp             = new Q(AZUL_RUNTIME+"sharedUserData.hpp");
  static final Q _smaHeuristic_cpp               = new Q(AZUL_RUNTIME+"smaHeuristic.cpp");
  static final Q _smaHeuristic_hpp               = new Q(AZUL_RUNTIME+"smaHeuristic.hpp");
  static final Q _stackBasedAllocation_cpp       = new Q(AZUL_RUNTIME+"stackBasedAllocation.cpp");
  static final Q _stackBasedAllocation_hpp       = new Q(AZUL_RUNTIME+"stackBasedAllocation.hpp");
  static final Q _statistics_cpp                 = new Q(AZUL_RUNTIME+"statistics.cpp");
  static final Q _statistics_hpp                 = new Q(AZUL_RUNTIME+"statistics.hpp");
  static final Q _synchronizer_cpp               = new Q(AZUL_RUNTIME+"synchronizer.cpp");
  static final Q _synchronizer_hpp               = new Q(AZUL_RUNTIME+"synchronizer.hpp");
  static final Q _threadCounters_hpp             = new Q(AZUL_RUNTIME+"threadCounters.hpp");
  static final Q _tickProfiler_cpp               = new Q(AZUL_RUNTIME+"tickProfiler.cpp");
  static final Q _tickProfiler_hpp               = new Q(AZUL_RUNTIME+"tickProfiler.hpp");
  static final Q _vframe_cpp                     = new Q(AZUL_RUNTIME+"vframe.cpp");
  static final Q _vframe_hpp                     = new Q(AZUL_RUNTIME+"vframe.hpp");
  static final Q _vmTags_cpp                     = new Q(AZUL_RUNTIME+"vmTags.cpp");
  static final Q _vmTags_hpp                     = new Q(AZUL_RUNTIME+"vmTags.hpp");
  static final Q _wlmuxer_hpp                    = new Q(AZUL_RUNTIME+"wlmuxer.hpp");
  static final Q _xmlBuffer_cpp                  = new Q(AZUL_RUNTIME+"xmlBuffer.cpp");
  static final Q _xmlBuffer_hpp                  = new Q(AZUL_RUNTIME+"xmlBuffer.hpp");

  static final String C1                         = "src/share/vm/c1/";
  static final Q _c1_CFGPrinter_cpp              = new Q(C1+"c1_CFGPrinter.cpp");
  static final Q _c1_CFGPrinter_hpp              = new Q(C1+"c1_CFGPrinter.hpp");
  static final Q _c1_CodeStubs_hpp               = new Q(C1+"c1_CodeStubs.hpp");
  static final Q _c1_Compilation_cpp             = new Q(C1+"c1_Compilation.cpp");
  static final Q _c1_Compilation_hpp             = new Q(C1+"c1_Compilation.hpp");
  static final Q _c1_Compiler_cpp                = new Q(C1+"c1_Compiler.cpp");
  static final Q _c1_Compiler_hpp                = new Q(C1+"c1_Compiler.hpp");
  static final Q _c1_Canonicalizer_cpp           = new Q(C1+"c1_Canonicalizer.cpp");
  static final Q _c1_Canonicalizer_hpp           = new Q(C1+"c1_Canonicalizer.hpp");
  static final Q _c1_Defs_cpp                    = new Q(C1+"c1_Defs.cpp");
  static final Q _c1_Defs_hpp                    = new Q(C1+"c1_Defs.hpp");
  static final Q _c1_FrameMap_cpp                = new Q(C1+"c1_FrameMap.cpp");
  static final Q _c1_FrameMap_hpp                = new Q(C1+"c1_FrameMap.hpp");
  static final Q _c1_globals_cpp                 = new Q(C1+"c1_globals.cpp");
  static final Q _c1_globals_hpp                 = new Q(C1+"c1_globals.hpp");
  static final Q _c1_GraphBuilder_cpp            = new Q(C1+"c1_GraphBuilder.cpp");
  static final Q _c1_GraphBuilder_hpp            = new Q(C1+"c1_GraphBuilder.hpp");
  static final Q _c1_LinearScan_cpp              = new Q(C1+"c1_LinearScan.cpp");
  static final Q _c1_LinearScan_hpp              = new Q(C1+"c1_LinearScan.hpp");
  static final Q _c1_LIR_cpp                     = new Q(C1+"c1_LIR.cpp");
  static final Q _c1_LIR_hpp                     = new Q(C1+"c1_LIR.hpp");
  static final Q _c1_LIRAssembler_cpp            = new Q(C1+"c1_LIRAssembler.cpp");
  static final Q _c1_LIRAssembler_hpp            = new Q(C1+"c1_LIRAssembler.hpp");
  static final Q _c1_LIRGenerator_cpp            = new Q(C1+"c1_LIRGenerator.cpp");
  static final Q _c1_LIRGenerator_hpp            = new Q(C1+"c1_LIRGenerator.hpp");
  static final Q _c1_Optimizer_cpp               = new Q(C1+"c1_Optimizer.cpp");
  static final Q _c1_Optimizer_hpp               = new Q(C1+"c1_Optimizer.hpp");
  static final Q _c1_Runtime1_cpp                = new Q(C1+"c1_Runtime1.cpp");
  static final Q _c1_Runtime1_hpp                = new Q(C1+"c1_Runtime1.hpp");
  static final Q _c1_Instruction_cpp             = new Q(C1+"c1_Instruction.cpp");
  static final Q _c1_Instruction_hpp             = new Q(C1+"c1_Instruction.hpp");
  static final Q _c1_InstructionPrinter_cpp      = new Q(C1+"c1_InstructionPrinter.cpp");
  static final Q _c1_InstructionPrinter_hpp      = new Q(C1+"c1_InstructionPrinter.hpp");
  static final Q _c1_IR_cpp                      = new Q(C1+"c1_IR.cpp");
  static final Q _c1_IR_hpp                      = new Q(C1+"c1_IR.hpp");
  static final Q _c1_MacroAssembler_hpp          = new Q(C1+"c1_MacroAssembler.hpp");
  static final Q _c1_ThreadLocals_cpp            = new Q(C1+"c1_ThreadLocals.cpp");
  static final Q _c1_ThreadLocals_hpp            = new Q(C1+"c1_ThreadLocals.hpp");
  static final Q _c1_ValueMap_cpp                = new Q(C1+"c1_ValueMap.cpp");
  static final Q _c1_ValueMap_hpp                = new Q(C1+"c1_ValueMap.hpp");
  static final Q _c1_ValueSet_cpp                = new Q(C1+"c1_ValueSet.cpp");
  static final Q _c1_ValueSet_hpp                = new Q(C1+"c1_ValueSet.hpp");
  static final Q _c1_ValueStack_cpp              = new Q(C1+"c1_ValueStack.cpp");
  static final Q _c1_ValueStack_hpp              = new Q(C1+"c1_ValueStack.hpp");
  static final Q _c1_ValueType_cpp               = new Q(C1+"c1_ValueType.cpp");
  static final Q _c1_ValueType_hpp               = new Q(C1+"c1_ValueType.hpp");

  static final String CI                         = "src/share/vm/ci/";
  static final Q _bcEscapeAnalyzer_cpp           = new Q(CI+"bcEscapeAnalyzer.cpp");
  static final Q _bcEscapeAnalyzer_hpp           = new Q(CI+"bcEscapeAnalyzer.hpp");
  static final Q _ciArray_cpp                    = new Q(CI+"ciArray.cpp");
  static final Q _ciArray_hpp                    = new Q(CI+"ciArray.hpp");
  static final Q _ciArrayKlass_cpp               = new Q(CI+"ciArrayKlass.cpp");
  static final Q _ciArrayKlass_hpp               = new Q(CI+"ciArrayKlass.hpp");
  static final Q _ciArrayKlassKlass_hpp          = new Q(CI+"ciArrayKlassKlass.hpp");
  static final Q _ciCallProfile_hpp              = new Q(CI+"ciCallProfile.hpp");
  static final Q _ciClassList_hpp                = new Q(CI+"ciClassList.hpp");
  static final Q _ciConstant_cpp                 = new Q(CI+"ciConstant.cpp");
  static final Q _ciConstant_hpp                 = new Q(CI+"ciConstant.hpp");
  static final Q _ciConstantPoolCache_cpp        = new Q(CI+"ciConstantPoolCache.cpp");
  static final Q _ciConstantPoolCache_hpp        = new Q(CI+"ciConstantPoolCache.hpp");
  static final Q _ciEnv_cpp                      = new Q(CI+"ciEnv.cpp");
  static final Q _ciEnv_hpp                      = new Q(CI+"ciEnv.hpp");
  static final Q _ciExceptionHandler_cpp         = new Q(CI+"ciExceptionHandler.cpp");
  static final Q _ciExceptionHandler_hpp         = new Q(CI+"ciExceptionHandler.hpp");
  static final Q _ciField_cpp                    = new Q(CI+"ciField.cpp");
  static final Q _ciField_hpp                    = new Q(CI+"ciField.hpp");
  static final Q _ciFlags_cpp                    = new Q(CI+"ciFlags.cpp");
  static final Q _ciFlags_hpp                    = new Q(CI+"ciFlags.hpp");
  static final Q _ciInstance_cpp                 = new Q(CI+"ciInstance.cpp");
  static final Q _ciInstance_hpp                 = new Q(CI+"ciInstance.hpp");
  static final Q _ciInstanceKlass_cpp            = new Q(CI+"ciInstanceKlass.cpp");
  static final Q _ciInstanceKlass_hpp            = new Q(CI+"ciInstanceKlass.hpp");
  static final Q _ciInstanceKlassKlass_cpp       = new Q(CI+"ciInstanceKlassKlass.cpp");
  static final Q _ciInstanceKlassKlass_hpp       = new Q(CI+"ciInstanceKlassKlass.hpp");
  static final Q _ciKlass_cpp                    = new Q(CI+"ciKlass.cpp");
  static final Q _ciKlass_hpp                    = new Q(CI+"ciKlass.hpp");
  static final Q _ciKlassKlass_cpp               = new Q(CI+"ciKlassKlass.cpp");
  static final Q _ciKlassKlass_hpp               = new Q(CI+"ciKlassKlass.hpp");
  static final Q _ciMethod_cpp                   = new Q(CI+"ciMethod.cpp");
  static final Q _ciMethod_hpp                   = new Q(CI+"ciMethod.hpp");
  static final Q _ciMethodBlocks_cpp             = new Q(CI+"ciMethodBlocks.cpp");
  static final Q _ciMethodBlocks_hpp             = new Q(CI+"ciMethodBlocks.hpp");
  static final Q _ciMethodKlass_cpp              = new Q(CI+"ciMethodKlass.cpp");
  static final Q _ciMethodKlass_hpp              = new Q(CI+"ciMethodKlass.hpp");
  static final Q _ciNullObject_cpp               = new Q(CI+"ciNullObject.cpp");
  static final Q _ciNullObject_hpp               = new Q(CI+"ciNullObject.hpp");
  static final Q _ciObjArray_cpp                 = new Q(CI+"ciObjArray.cpp");
  static final Q _ciObjArray_hpp                 = new Q(CI+"ciObjArray.hpp");
  static final Q _ciObjArrayKlass_cpp            = new Q(CI+"ciObjArrayKlass.cpp");
  static final Q _ciObjArrayKlass_hpp            = new Q(CI+"ciObjArrayKlass.hpp");
  static final Q _ciObjArrayKlassKlass_cpp       = new Q(CI+"ciObjArrayKlassKlass.cpp");
  static final Q _ciObjArrayKlassKlass_hpp       = new Q(CI+"ciObjArrayKlassKlass.hpp");
  static final Q _ciObject_cpp                   = new Q(CI+"ciObject.cpp");
  static final Q _ciObject_hpp                   = new Q(CI+"ciObject.hpp");
  static final Q _ciObjectFactory_cpp            = new Q(CI+"ciObjectFactory.cpp");
  static final Q _ciObjectFactory_hpp            = new Q(CI+"ciObjectFactory.hpp");
  static final Q _ciSignature_cpp                = new Q(CI+"ciSignature.cpp");
  static final Q _ciSignature_hpp                = new Q(CI+"ciSignature.hpp");
  static final Q _ciStreams_cpp                  = new Q(CI+"ciStreams.cpp");
  static final Q _ciStreams_hpp                  = new Q(CI+"ciStreams.hpp");
  static final Q _ciSymbol_cpp                   = new Q(CI+"ciSymbol.cpp");
  static final Q _ciSymbol_hpp                   = new Q(CI+"ciSymbol.hpp");
  static final Q _ciSymbolKlass_cpp              = new Q(CI+"ciSymbolKlass.cpp");
  static final Q _ciSymbolKlass_hpp              = new Q(CI+"ciSymbolKlass.hpp");
  static final Q _ciType_cpp                     = new Q(CI+"ciType.cpp");
  static final Q _ciType_hpp                     = new Q(CI+"ciType.hpp");
  static final Q _ciTypeArrayKlass_cpp           = new Q(CI+"ciTypeArrayKlass.cpp");
  static final Q _ciTypeArrayKlass_hpp           = new Q(CI+"ciTypeArrayKlass.hpp");
  static final Q _ciTypeArrayKlassKlass_cpp      = new Q(CI+"ciTypeArrayKlassKlass.cpp");
  static final Q _ciTypeArrayKlassKlass_hpp      = new Q(CI+"ciTypeArrayKlassKlass.hpp");
  static final Q _ciTypeArray_cpp                = new Q(CI+"ciTypeArray.cpp");
  static final Q _ciTypeArray_hpp                = new Q(CI+"ciTypeArray.hpp");
  static final Q _ciTypeFlow_cpp                 = new Q(CI+"ciTypeFlow.cpp");
  static final Q _ciTypeFlow_hpp                 = new Q(CI+"ciTypeFlow.hpp");
  static final Q _ciUtilities_cpp                = new Q(CI+"ciUtilities.cpp");
  static final Q _ciUtilities_hpp                = new Q(CI+"ciUtilities.hpp");

  static final String CLASSFILE                  = "src/share/vm/classfile/";
  static final Q _classFileError_cpp             = new Q(CLASSFILE+"classFileError.cpp");
  static final Q _classFileParser_cpp            = new Q(CLASSFILE+"classFileParser.cpp");
  static final Q _classFileParser_hpp            = new Q(CLASSFILE+"classFileParser.hpp");
  static final Q _classFileStream_cpp            = new Q(CLASSFILE+"classFileStream.cpp");
  static final Q _classFileStream_hpp            = new Q(CLASSFILE+"classFileStream.hpp");
  static final Q _classLoader_cpp                = new Q(CLASSFILE+"classLoader.cpp");
  static final Q _classLoader_hpp                = new Q(CLASSFILE+"classLoader.hpp");
  static final Q _dictionary_cpp                 = new Q(CLASSFILE+"dictionary.cpp");
  static final Q _dictionary_hpp                 = new Q(CLASSFILE+"dictionary.hpp");
  static final Q _javaAssertions_cpp             = new Q(CLASSFILE+"javaAssertions.cpp");
  static final Q _javaAssertions_hpp             = new Q(CLASSFILE+"javaAssertions.hpp");
  static final Q _javaClasses_cpp                = new Q(CLASSFILE+"javaClasses.cpp");
  static final Q _javaClasses_hpp                = new Q(CLASSFILE+"javaClasses.hpp");
  static final Q _loaderConstraints_cpp          = new Q(CLASSFILE+"loaderConstraints.cpp");
  static final Q _loaderConstraints_hpp          = new Q(CLASSFILE+"loaderConstraints.hpp");
  static final Q _placeholders_cpp               = new Q(CLASSFILE+"placeholders.cpp");
  static final Q _placeholders_hpp               = new Q(CLASSFILE+"placeholders.hpp");
  static final Q _resolutionErrors_cpp           = new Q(CLASSFILE+"resolutionErrors.cpp");
  static final Q _resolutionErrors_hpp           = new Q(CLASSFILE+"resolutionErrors.hpp");
  static final Q _symbolTable_cpp                = new Q(CLASSFILE+"symbolTable.cpp");
  static final Q _symbolTable_hpp                = new Q(CLASSFILE+"symbolTable.hpp");
  static final Q _stackMapFrame_cpp              = new Q(CLASSFILE+"stackMapFrame.cpp");
  static final Q _stackMapFrame_hpp              = new Q(CLASSFILE+"stackMapFrame.hpp");
  static final Q _stackMapTable_cpp              = new Q(CLASSFILE+"stackMapTable.cpp");
  static final Q _stackMapTable_hpp              = new Q(CLASSFILE+"stackMapTable.hpp");
  static final Q _systemDictionary_cpp           = new Q(CLASSFILE+"systemDictionary.cpp");
  static final Q _systemDictionary_hpp           = new Q(CLASSFILE+"systemDictionary.hpp");
  static final Q _verificationType_cpp           = new Q(CLASSFILE+"verificationType.cpp");
  static final Q _verificationType_hpp           = new Q(CLASSFILE+"verificationType.hpp");
  static final Q _verifier_cpp                   = new Q(CLASSFILE+"verifier.cpp");
  static final Q _verifier_hpp                   = new Q(CLASSFILE+"verifier.hpp");
  static final Q _vmSymbols_cpp                  = new Q(CLASSFILE+"vmSymbols.cpp");
  static final Q _vmSymbols_hpp                  = new Q(CLASSFILE+"vmSymbols.hpp");

  static final String CODE                       = "src/share/vm/code/";
  static final Q _compressedStream_cpp           = new Q(CODE+"compressedStream.cpp");
  static final Q _compressedStream_hpp           = new Q(CODE+"compressedStream.hpp");
  static final Q _dependencies_cpp               = new Q(CODE+"dependencies.cpp");
  static final Q _dependencies_hpp               = new Q(CODE+"dependencies.hpp");

  static final String COMPILER                   = "src/share/vm/compiler/";
  static final Q _abstractCompiler_hpp           = new Q(COMPILER+"abstractCompiler.hpp");
  static final Q _abstractCompiler_cpp           = new Q(COMPILER+"abstractCompiler.cpp");
  static final Q _compilerOracle_cpp             = new Q(COMPILER+"compilerOracle.cpp");
  static final Q _compilerOracle_hpp             = new Q(COMPILER+"compilerOracle.hpp");
  static final Q _methodLiveness_hpp             = new Q(COMPILER+"methodLiveness.hpp");
  static final Q _methodLiveness_cpp             = new Q(COMPILER+"methodLiveness.cpp");

  static final String GC_INTERFACE               = "src/share/vm/gc_interface/";
  static final Q _collectedHeap_cpp              = new Q(GC_INTERFACE+"collectedHeap.cpp");
  static final Q _collectedHeap_hpp              = new Q(GC_INTERFACE+"collectedHeap.hpp");
  static final Q _collectedHeap_inline_hpp       = new Q(GC_INTERFACE+"collectedHeap.inline.hpp");
  static final Q _gcCause_hpp                    = new Q(GC_INTERFACE+"gcCause.hpp");

  static final String GC_SHARED                  = "src/share/vm/gc_implementation/shared/";
  static final Q _adaptiveSizePolicy_cpp         = new Q(GC_SHARED+"adaptiveSizePolicy.cpp");
  static final Q _adaptiveSizePolicy_hpp         = new Q(GC_SHARED+"adaptiveSizePolicy.hpp");
  static final Q _ageTable_hpp                   = new Q(GC_SHARED+"ageTable.hpp");
  static final Q _cSpaceCounters_hpp             = new Q(GC_SHARED+"cSpaceCounters.hpp");
  static final Q _collectorCounters_cpp          = new Q(GC_SHARED+"collectorCounters.cpp");
  static final Q _collectorCounters_hpp          = new Q(GC_SHARED+"collectorCounters.hpp");
  static final Q _gcAdaptivePolicyCounters_cpp   = new Q(GC_SHARED+"gcAdaptivePolicyCounters.cpp");
  static final Q _gcAdaptivePolicyCounters_hpp   = new Q(GC_SHARED+"gcAdaptivePolicyCounters.hpp");
  static final Q _gcPolicyCounters_cpp           = new Q(GC_SHARED+"gcPolicyCounters.cpp");
  static final Q _gcPolicyCounters_hpp           = new Q(GC_SHARED+"gcPolicyCounters.hpp");
  static final Q _gcStats_cpp                    = new Q(GC_SHARED+"gcStats.cpp");
  static final Q _gcStats_hpp                    = new Q(GC_SHARED+"gcStats.hpp");
  static final Q _gcUtil_cpp                     = new Q(GC_SHARED+"gcUtil.cpp");
  static final Q _gcUtil_hpp                     = new Q(GC_SHARED+"gcUtil.hpp");
  static final Q _generationCounters_hpp         = new Q(GC_SHARED+"generationCounters.hpp");
  static final Q _immutableSpace_hpp             = new Q(GC_SHARED+"immutableSpace.hpp");
  static final Q _isGCActiveMark_hpp             = new Q(GC_SHARED+"isGCActiveMark.hpp");
  static final Q _markSweep_cpp                  = new Q(GC_SHARED+"markSweep.cpp");
  static final Q _markSweep_hpp                  = new Q(GC_SHARED+"markSweep.hpp");
  static final Q _markSweep_inline_hpp           = new Q(GC_SHARED+"markSweep.inline.hpp");
  static final Q _mutableSpace_cpp               = new Q(GC_SHARED+"mutableSpace.cpp");
  static final Q _mutableSpace_hpp               = new Q(GC_SHARED+"mutableSpace.hpp");
  static final Q _mutableNUMASpace_hpp           = new Q(GC_SHARED+"mutableNUMASpace.hpp");
  static final Q _spaceCounters_cpp              = new Q(GC_SHARED+"spaceCounters.cpp");
  static final Q _spaceCounters_hpp              = new Q(GC_SHARED+"spaceCounters.hpp");
  static final Q _surrogateLockerThread_cpp      = new Q(GC_SHARED+"surrogateLockerThread.cpp");
  static final Q _surrogateLockerThread_hpp      = new Q(GC_SHARED+"surrogateLockerThread.hpp");
  static final Q _vmGCOperations_cpp             = new Q(GC_SHARED+"vmGCOperations.cpp");
  static final Q _vmGCOperations_hpp             = new Q(GC_SHARED+"vmGCOperations.hpp");

  static final String GC_PARALLELSCAVENGE        = "src/share/vm/gc_implementation/parallelScavenge/";
  static final Q _adjoiningGenerations_cpp       = new Q(GC_PARALLELSCAVENGE+"adjoiningGenerations.cpp");
  static final Q _adjoiningGenerations_hpp       = new Q(GC_PARALLELSCAVENGE+"adjoiningGenerations.hpp");
  static final Q _adjoiningVirtualSpaces_cpp     = new Q(GC_PARALLELSCAVENGE+"adjoiningVirtualSpaces.cpp");
  static final Q _adjoiningVirtualSpaces_hpp     = new Q(GC_PARALLELSCAVENGE+"adjoiningVirtualSpaces.hpp");
  static final Q _asPSOldGen_hpp                 = new Q(GC_PARALLELSCAVENGE+"asPSOldGen.hpp");
  static final Q _asPSYoungGen_hpp               = new Q(GC_PARALLELSCAVENGE+"asPSYoungGen.hpp");
  static final Q _cardTableExtension_cpp         = new Q(GC_PARALLELSCAVENGE+"cardTableExtension.cpp");
  static final Q _cardTableExtension_hpp         = new Q(GC_PARALLELSCAVENGE+"cardTableExtension.hpp");
  static final Q _gcTaskManager_cpp              = new Q(GC_PARALLELSCAVENGE+"gcTaskManager.cpp");
  static final Q _gcTaskManager_hpp              = new Q(GC_PARALLELSCAVENGE+"gcTaskManager.hpp");
  static final Q _gcTaskThread_cpp               = new Q(GC_PARALLELSCAVENGE+"gcTaskThread.cpp");
  static final Q _gcTaskThread_hpp               = new Q(GC_PARALLELSCAVENGE+"gcTaskThread.hpp");
  static final Q _generationSizer_hpp            = new Q(GC_PARALLELSCAVENGE+"generationSizer.hpp");
  static final Q _objectStartArray_cpp           = new Q(GC_PARALLELSCAVENGE+"objectStartArray.cpp");
  static final Q _objectStartArray_hpp           = new Q(GC_PARALLELSCAVENGE+"objectStartArray.hpp");
  static final Q _parMarkBitMap_hpp              = new Q(GC_PARALLELSCAVENGE+"parMarkBitMap.hpp");
  static final Q _parallelScavengeHeap_cpp       = new Q(GC_PARALLELSCAVENGE+"parallelScavengeHeap.cpp");
  static final Q _parallelScavengeHeap_hpp       = new Q(GC_PARALLELSCAVENGE+"parallelScavengeHeap.hpp");
  static final Q _parallelScavengeHeap_inline_hpp= new Q(GC_PARALLELSCAVENGE+"parallelScavengeHeap.inline.hpp");
  static final Q _pcTasks_hpp                    = new Q(GC_PARALLELSCAVENGE+"pcTasks.hpp");
  static final Q _prefetchQueue_hpp              = new Q(GC_PARALLELSCAVENGE+"prefetchQueue.hpp");
  static final Q _psAdaptiveSizePolicy_cpp       = new Q(GC_PARALLELSCAVENGE+"psAdaptiveSizePolicy.cpp");
  static final Q _psAdaptiveSizePolicy_hpp       = new Q(GC_PARALLELSCAVENGE+"psAdaptiveSizePolicy.hpp");
  static final Q _psCompactionManager_hpp        = new Q(GC_PARALLELSCAVENGE+"psCompactionManager.hpp");
  static final Q _psGCAdaptivePolicyCounters_cpp = new Q(GC_PARALLELSCAVENGE+"psGCAdaptivePolicyCounters.cpp");
  static final Q _psGCAdaptivePolicyCounters_hpp = new Q(GC_PARALLELSCAVENGE+"psGCAdaptivePolicyCounters.hpp");
  static final Q _psGenerationCounters_cpp       = new Q(GC_PARALLELSCAVENGE+"psGenerationCounters.cpp");
  static final Q _psGenerationCounters_hpp       = new Q(GC_PARALLELSCAVENGE+"psGenerationCounters.hpp");
  static final Q _psMarkSweepDecorator_cpp       = new Q(GC_PARALLELSCAVENGE+"psMarkSweepDecorator.cpp");
  static final Q _psMarkSweepDecorator_hpp       = new Q(GC_PARALLELSCAVENGE+"psMarkSweepDecorator.hpp");
  static final Q _psMarkSweep_cpp                = new Q(GC_PARALLELSCAVENGE+"psMarkSweep.cpp");
  static final Q _psMarkSweep_hpp                = new Q(GC_PARALLELSCAVENGE+"psMarkSweep.hpp");
  static final Q _psOldGen_cpp                   = new Q(GC_PARALLELSCAVENGE+"psOldGen.cpp");
  static final Q _psOldGen_hpp                   = new Q(GC_PARALLELSCAVENGE+"psOldGen.hpp");
  static final Q _psParallelCompact_cpp          = new Q(GC_PARALLELSCAVENGE+"psParallelCompact.cpp");
  static final Q _psParallelCompact_hpp          = new Q(GC_PARALLELSCAVENGE+"psParallelCompact.hpp");
  static final Q _psPermGen_cpp                  = new Q(GC_PARALLELSCAVENGE+"psPermGen.cpp");
  static final Q _psPermGen_hpp                  = new Q(GC_PARALLELSCAVENGE+"psPermGen.hpp");
  static final Q _psPromotionLAB_cpp             = new Q(GC_PARALLELSCAVENGE+"psPromotionLAB.cpp");
  static final Q _psPromotionLAB_hpp             = new Q(GC_PARALLELSCAVENGE+"psPromotionLAB.hpp");
  static final Q _psPromotionManager_cpp         = new Q(GC_PARALLELSCAVENGE+"psPromotionManager.cpp");
  static final Q _psPromotionManager_hpp         = new Q(GC_PARALLELSCAVENGE+"psPromotionManager.hpp");
  static final Q _psPromotionManager_inline_hpp  = new Q(GC_PARALLELSCAVENGE+"psPromotionManager.inline.hpp");
  static final Q _psScavenge_cpp                 = new Q(GC_PARALLELSCAVENGE+"psScavenge.cpp");
  static final Q _psScavenge_hpp                 = new Q(GC_PARALLELSCAVENGE+"psScavenge.hpp");
  static final Q _psScavenge_inline_hpp          = new Q(GC_PARALLELSCAVENGE+"psScavenge.inline.hpp");
  static final Q _psTasks_cpp                    = new Q(GC_PARALLELSCAVENGE+"psTasks.cpp");
  static final Q _psTasks_hpp                    = new Q(GC_PARALLELSCAVENGE+"psTasks.hpp");
  static final Q _psVirtualspace_cpp             = new Q(GC_PARALLELSCAVENGE+"psVirtualspace.cpp");
  static final Q _psVirtualspace_hpp             = new Q(GC_PARALLELSCAVENGE+"psVirtualspace.hpp");
  static final Q _psYoungGen_cpp                 = new Q(GC_PARALLELSCAVENGE+"psYoungGen.cpp");
  static final Q _psYoungGen_hpp                 = new Q(GC_PARALLELSCAVENGE+"psYoungGen.hpp");
  static final Q _vmPSOperations_cpp             = new Q(GC_PARALLELSCAVENGE+"vmPSOperations.cpp");
  static final Q _vmPSOperations_hpp             = new Q(GC_PARALLELSCAVENGE+"vmPSOperations.hpp");

  static final String INTERPRETER                = "src/share/vm/interpreter/";
  static final Q _bytecodeHistogram_cpp          = new Q(INTERPRETER+"bytecodeHistogram.cpp");
  static final Q _bytecodeHistogram_hpp          = new Q(INTERPRETER+"bytecodeHistogram.hpp");
  static final Q _bytecodeStream_cpp             = new Q(INTERPRETER+"bytecodeStream.cpp");
  static final Q _bytecodeStream_hpp             = new Q(INTERPRETER+"bytecodeStream.hpp");
  static final Q _bytecodeTracer_cpp             = new Q(INTERPRETER+"bytecodeTracer.cpp");
  static final Q _bytecodeTracer_hpp             = new Q(INTERPRETER+"bytecodeTracer.hpp");
  static final Q _bytecode_cpp                   = new Q(INTERPRETER+"bytecode.cpp");
  static final Q _bytecode_hpp                   = new Q(INTERPRETER+"bytecode.hpp");
  static final Q _bytecodes_cpp                  = new Q(INTERPRETER+"bytecodes.cpp");
  static final Q _bytecodes_hpp                  = new Q(INTERPRETER+"bytecodes.hpp");
  static final Q _interpreter_cpp                = new Q(INTERPRETER+"interpreter.cpp");
  static final Q _interpreter_hpp                = new Q(INTERPRETER+"interpreter.hpp");
  static final Q _interpreterRuntime_cpp         = new Q(INTERPRETER+"interpreterRuntime.cpp");
  static final Q _interpreterRuntime_hpp         = new Q(INTERPRETER+"interpreterRuntime.hpp");
  static final Q _invocationCounter_cpp          = new Q(INTERPRETER+"invocationCounter.cpp");
  static final Q _invocationCounter_hpp          = new Q(INTERPRETER+"invocationCounter.hpp");
  static final Q _linkResolver_cpp               = new Q(INTERPRETER+"linkResolver.cpp");
  static final Q _linkResolver_hpp               = new Q(INTERPRETER+"linkResolver.hpp");
  static final Q _rewriter_cpp                   = new Q(INTERPRETER+"rewriter.cpp");
  static final Q _rewriter_hpp                   = new Q(INTERPRETER+"rewriter.hpp");
  static final Q _templateTable_cpp              = new Q(INTERPRETER+"templateTable.cpp");
  static final Q _templateTable_hpp              = new Q(INTERPRETER+"templateTable.hpp");

  static final String LIBADT                     = "src/share/vm/libadt/";
  static final Q _dict_cpp                       = new Q(LIBADT+"dict.cpp");
  static final Q _dict_hpp                       = new Q(LIBADT+"dict.hpp");
  static final Q _port_cpp                       = new Q(LIBADT+"port.cpp");
  static final Q _port_hpp                       = new Q(LIBADT+"port.hpp");
  static final Q _set_cpp                        = new Q(LIBADT+"set.cpp");
  static final Q _set_hpp                        = new Q(LIBADT+"set.hpp");
  static final Q _vectset_cpp                    = new Q(LIBADT+"vectset.cpp");
  static final Q _vectset_hpp                    = new Q(LIBADT+"vectset.hpp");

  static final String MEMORY                     = "src/share/vm/memory/";
  static final Q _allocation_cpp                 = new Q(MEMORY+"allocation.cpp");
  static final Q _allocation_hpp                 = new Q(MEMORY+"allocation.hpp");
  static final Q _allocation_inline_hpp          = new Q(MEMORY+"allocation.inline.hpp");
  static final Q _barrierSet_hpp                 = new Q(MEMORY+"barrierSet.hpp");
  static final Q _barrierSet_inline_hpp          = new Q(MEMORY+"barrierSet.inline.hpp");
  static final Q _blockOffsetTable_hpp           = new Q(MEMORY+"blockOffsetTable.hpp");
  static final Q _cardTableModRefBS_cpp          = new Q(MEMORY+"cardTableModRefBS.cpp");
  static final Q _cardTableModRefBS_hpp          = new Q(MEMORY+"cardTableModRefBS.hpp");
  static final Q _cardTableRS_hpp                = new Q(MEMORY+"cardTableRS.hpp");
  static final Q _collectorPolicy_cpp            = new Q(MEMORY+"collectorPolicy.cpp");
  static final Q _collectorPolicy_hpp            = new Q(MEMORY+"collectorPolicy.hpp");
  static final Q _compactingPermGenGen_cpp       = new Q(MEMORY+"compactingPermGenGen.cpp");
  static final Q _compactingPermGenGen_hpp       = new Q(MEMORY+"compactingPermGenGen.hpp");
  static final Q _defNewGeneration_hpp           = new Q(MEMORY+"defNewGeneration.hpp");
  static final Q _filemap_hpp                    = new Q(MEMORY+"filemap.hpp");
  static final Q _gcLocker_cpp                   = new Q(MEMORY+"gcLocker.cpp");
  static final Q _gcLocker_hpp                   = new Q(MEMORY+"gcLocker.hpp");
  static final Q _gcLocker_inline_hpp            = new Q(MEMORY+"gcLocker.inline.hpp");
  static final Q _genCollectedHeap_hpp           = new Q(MEMORY+"genCollectedHeap.hpp");
  static final Q _genOopClosures_cpp             = new Q(MEMORY+"genOopClosures.cpp");
  static final Q _genOopClosures_hpp             = new Q(MEMORY+"genOopClosures.hpp");
  static final Q _genOopClosures_inline_hpp      = new Q(MEMORY+"genOopClosures.inline.hpp");
  static final Q _genRemSet_cpp                  = new Q(MEMORY+"genRemSet.cpp");
  static final Q _genRemSet_hpp                  = new Q(MEMORY+"genRemSet.hpp");
  static final Q _generationSpec_hpp             = new Q(MEMORY+"generationSpec.hpp");
  static final Q _generation_hpp                 = new Q(MEMORY+"generation.hpp");
  static final Q _heapInspection_hpp             = new Q(MEMORY+"heapInspection.hpp");
  static final Q _iterator_hpp                   = new Q(MEMORY+"iterator.hpp");
  static final Q _memRegion_cpp                  = new Q(MEMORY+"memRegion.cpp");
  static final Q _memRegion_hpp                  = new Q(MEMORY+"memRegion.hpp");
  static final Q _modRefBarrierSet_hpp           = new Q(MEMORY+"modRefBarrierSet.hpp");
  static final Q _oopFactory_cpp                 = new Q(MEMORY+"oopFactory.cpp");
  static final Q _oopFactory_hpp                 = new Q(MEMORY+"oopFactory.hpp");
  static final Q _permGen_hpp                    = new Q(MEMORY+"permGen.hpp");
  static final Q _referencePolicy_cpp            = new Q(MEMORY+"referencePolicy.cpp");
  static final Q _referencePolicy_hpp            = new Q(MEMORY+"referencePolicy.hpp");
  static final Q _referenceProcessor_cpp         = new Q(MEMORY+"referenceProcessor.cpp");
  static final Q _referenceProcessor_hpp         = new Q(MEMORY+"referenceProcessor.hpp");
  static final Q _resourceArea_cpp               = new Q(MEMORY+"resourceArea.cpp");
  static final Q _resource_hpp                   = new Q(MEMORY+"resourceArea.hpp");
  static final Q _sharedHeap_hpp                 = new Q(MEMORY+"sharedHeap.hpp");
  static final Q _space_cpp                      = new Q(MEMORY+"space.cpp");
  static final Q _space_hpp                      = new Q(MEMORY+"space.hpp");
  static final Q _space_inline_hpp               = new Q(MEMORY+"space.inline.hpp");
  static final Q _spec_oop_clos_hpp              = new Q(MEMORY+"specialized_oop_closures.hpp");
  static final Q _threadLocalAlloc_cpp           = new Q(MEMORY+"threadLocalAllocBuffer.cpp");
  static final Q _threadLocalAlloc_hpp           = new Q(MEMORY+"threadLocalAllocBuffer.hpp");
  static final Q _threadLocalAlloc_inline_hpp    = new Q(MEMORY+"threadLocalAllocBuffer.inline.hpp");
  static final Q _universe_cpp                   = new Q(MEMORY+"universe.cpp");
  static final Q _universe_hpp                   = new Q(MEMORY+"universe.hpp");
  static final Q _universe_inline_hpp            = new Q(MEMORY+"universe.inline.hpp");
  static final Q _watermark_hpp                  = new Q(MEMORY+"watermark.hpp");

  static final String OOPS                       = "src/share/vm/oops/";
  static final Q _arrayKlassKlass_cpp            = new Q(OOPS+"arrayKlassKlass.cpp");
  static final Q _arrayKlassKlass_hpp            = new Q(OOPS+"arrayKlassKlass.hpp");
  static final Q _arrayKlass_cpp                 = new Q(OOPS+"arrayKlass.cpp");
  static final Q _arrayKlass_hpp                 = new Q(OOPS+"arrayKlass.hpp");
  static final Q _arrayOop_hpp                   = new Q(OOPS+"arrayOop.hpp");
  static final Q _constMethodKlass_cpp           = new Q(OOPS+"constMethodKlass.cpp");
  static final Q _constMethodKlass_hpp           = new Q(OOPS+"constMethodKlass.hpp");
  static final Q _constMethodOop_cpp             = new Q(OOPS+"constMethodOop.cpp");
  static final Q _constMethodOop_hpp             = new Q(OOPS+"constMethodOop.hpp");
  static final Q _constantPoolKlass_cpp          = new Q(OOPS+"constantPoolKlass.cpp");
  static final Q _constantPoolKlass_hpp          = new Q(OOPS+"constantPoolKlass.hpp");
  static final Q _constantPoolOop_cpp            = new Q(OOPS+"constantPoolOop.cpp");
  static final Q _constantPoolOop_hpp            = new Q(OOPS+"constantPoolOop.hpp");
  static final Q _cpCacheKlass_cpp               = new Q(OOPS+"cpCacheKlass.cpp");
  static final Q _cpCacheKlass_hpp               = new Q(OOPS+"cpCacheKlass.hpp");
  static final Q _cpCacheOop_cpp                 = new Q(OOPS+"cpCacheOop.cpp");
  static final Q _cpCacheOop_hpp                 = new Q(OOPS+"cpCacheOop.hpp");
  static final Q _generateOopMap_cpp             = new Q(OOPS+"generateOopMap.cpp");
  static final Q _generateOopMap_hpp             = new Q(OOPS+"generateOopMap.hpp");
  static final Q _inst_klass_cpp                 = new Q(OOPS+"instanceKlass.cpp");
  static final Q _inst_klass_hpp                 = new Q(OOPS+"instanceKlass.hpp");
  static final Q _inst_oop_hpp                   = new Q(OOPS+"instanceOop.hpp");
  static final Q _instanceKlassKlass_cpp         = new Q(OOPS+"instanceKlassKlass.cpp");
  static final Q _instanceKlassKlass_hpp         = new Q(OOPS+"instanceKlassKlass.hpp");
  static final Q _instanceRefKlass_cpp           = new Q(OOPS+"instanceRefKlass.cpp");
  static final Q _instanceRefKlass_hpp           = new Q(OOPS+"instanceRefKlass.hpp");
  static final Q _klassKlass_cpp                 = new Q(OOPS+"klassKlass.cpp");
  static final Q _klassKlass_hpp                 = new Q(OOPS+"klassKlass.hpp");
  static final Q _klassOop_hpp                   = new Q(OOPS+"klassOop.hpp");
  static final Q _klassVtable_cpp                = new Q(OOPS+"klassVtable.cpp");
  static final Q _klassVtable_hpp                = new Q(OOPS+"klassVtable.hpp");
  static final Q _klass_cpp                      = new Q(OOPS+"klass.cpp");
  static final Q _klass_hpp                      = new Q(OOPS+"klass.hpp");
  static final Q _klassPS_hpp                    = new Q(OOPS+"klassPS.hpp");
  static final Q _methodKlass_cpp                = new Q(OOPS+"methodKlass.cpp");
  static final Q _methodKlass_hpp                = new Q(OOPS+"methodKlass.hpp");
  static final Q _methodOop_cpp                  = new Q(OOPS+"methodOop.cpp");
  static final Q _methodOop_hpp                  = new Q(OOPS+"methodOop.hpp");
  static final Q _objArrayKlassKlass_cpp         = new Q(OOPS+"objArrayKlassKlass.cpp");
  static final Q _objArrayKlassKlass_hpp         = new Q(OOPS+"objArrayKlassKlass.hpp");
  static final Q _objArrayKlass_cpp              = new Q(OOPS+"objArrayKlass.cpp");
  static final Q _objArrayKlass_hpp              = new Q(OOPS+"objArrayKlass.hpp");
  static final Q _objArrayOop_hpp                = new Q(OOPS+"objArrayOop.hpp");
  static final Q _oopClosures_py                 = new Q(OOPS+"oopClosures.py");
  static final Q _oop_cpp                        = new Q(OOPS+"oop.cpp");
  static final Q _oop_hpp                        = new Q(OOPS+"oop.hpp");
  static final Q _oop_inline2_hpp                = new Q(OOPS+"oop.inline2.hpp");
  static final Q _oop_inline_hpp                 = new Q(OOPS+"oop.inline.hpp");
  static final Q _oop_gpgc_inline_hpp            = new Q(OOPS+"oop.gpgc.inline.hpp");
  static final Q _oop_psgc_inline_hpp            = new Q(OOPS+"oop.psgc.inline.hpp");
  static final Q _oopsHierarchy_cpp              = new Q(OOPS+"oopsHierarchy.cpp");
  static final Q _oopsHierarchy_hpp              = new Q(OOPS+"oopsHierarchy.hpp");
  static final Q _symbolKlass_cpp                = new Q(OOPS+"symbolKlass.cpp");
  static final Q _symbolKlass_hpp                = new Q(OOPS+"symbolKlass.hpp");
  static final Q _symbolOop_cpp                  = new Q(OOPS+"symbolOop.cpp");
  static final Q _symbolOop_hpp                  = new Q(OOPS+"symbolOop.hpp");
  static final Q _typeArrayKlassKlass_cpp        = new Q(OOPS+"typeArrayKlassKlass.cpp");
  static final Q _typeArrayKlassKlass_hpp        = new Q(OOPS+"typeArrayKlassKlass.hpp");
  static final Q _typeArrayKlass_cpp             = new Q(OOPS+"typeArrayKlass.cpp");
  static final Q _typeArrayKlass_hpp             = new Q(OOPS+"typeArrayKlass.hpp");
  static final Q _typeArrayOop_hpp               = new Q(OOPS+"typeArrayOop.hpp");

  static final String OPTO                       = "src/share/vm/opto/";
  static final Q _addnode_cpp                    = new Q(OPTO+"addnode.cpp");
  static final Q _addnode_hpp                    = new Q(OPTO+"addnode.hpp");
  static final Q _block_cpp                      = new Q(OPTO+"block.cpp");
  static final Q _block_hpp                      = new Q(OPTO+"block.hpp");
  static final Q _buildOopMap_cpp                = new Q(OPTO+"buildOopMap.cpp");
  static final Q _bytecodeInfo_cpp               = new Q(OPTO+"bytecodeInfo.cpp");
  static final Q _c2_globals_cpp                 = new Q(OPTO+"c2_globals.cpp");
  static final Q _c2_globals_hpp                 = new Q(OPTO+"c2_globals.hpp");
  static final Q _c2compiler_cpp                 = new Q(OPTO+"c2compiler.cpp");
  static final Q _c2compiler_hpp                 = new Q(OPTO+"c2compiler.hpp");
  static final Q _callGenerator_cpp              = new Q(OPTO+"callGenerator.cpp");
  static final Q _callGenerator_hpp              = new Q(OPTO+"callGenerator.hpp");
  static final Q _callnode_cpp                   = new Q(OPTO+"callnode.cpp");
  static final Q _callnode_hpp                   = new Q(OPTO+"callnode.hpp");
  static final Q _cfgnode_cpp                    = new Q(OPTO+"cfgnode.cpp");
  static final Q _cfgnode_hpp                    = new Q(OPTO+"cfgnode.hpp");
  static final Q _chaitin_cpp                    = new Q(OPTO+"chaitin.cpp");
  static final Q _chaitin_hpp                    = new Q(OPTO+"chaitin.hpp");
  static final Q _classes_cpp                    = new Q(OPTO+"classes.cpp");
  static final Q _classes_hpp                    = new Q(OPTO+"classes.hpp");
  static final Q _coalesce_cpp                   = new Q(OPTO+"coalesce.cpp");
  static final Q _coalesce_hpp                   = new Q(OPTO+"coalesce.hpp");
  static final Q _compile_cpp                    = new Q(OPTO+"compile.cpp");
  static final Q _compile_hpp                    = new Q(OPTO+"compile.hpp");
  static final Q _connode_cpp                    = new Q(OPTO+"connode.cpp");
  static final Q _connode_hpp                    = new Q(OPTO+"connode.hpp");
  static final Q _divnode_cpp                    = new Q(OPTO+"divnode.cpp");
  static final Q _divnode_hpp                    = new Q(OPTO+"divnode.hpp");
  static final Q _doCall_cpp                     = new Q(OPTO+"doCall.cpp");
  static final Q _domgraph_cpp                   = new Q(OPTO+"domgraph.cpp");
  static final Q _escape_cpp                     = new Q(OPTO+"escape.cpp");
  static final Q _escape_hpp                     = new Q(OPTO+"escape.hpp");
  static final Q _gcm_cpp                        = new Q(OPTO+"gcm.cpp");
  static final Q _graphKit_cpp                   = new Q(OPTO+"graphKit.cpp");
  static final Q _graphKit_hpp                   = new Q(OPTO+"graphKit.hpp");
  static final Q _idealKit_cpp                   = new Q(OPTO+"idealKit.cpp");
  static final Q _idealKit_hpp                   = new Q(OPTO+"idealKit.hpp");
  static final Q _ifg_cpp                        = new Q(OPTO+"ifg.cpp");
  static final Q _ifnode_cpp                     = new Q(OPTO+"ifnode.cpp");
  static final Q _indexSet_cpp                   = new Q(OPTO+"indexSet.cpp");
  static final Q _indexSet_hpp                   = new Q(OPTO+"indexSet.hpp");
  static final Q _lcm_cpp                        = new Q(OPTO+"lcm.cpp");
  static final Q _library_call_cpp               = new Q(OPTO+"library_call.cpp");
  static final Q _live_cpp                       = new Q(OPTO+"live.cpp");
  static final Q _live_hpp                       = new Q(OPTO+"live.hpp");
  static final Q _locknode_cpp                   = new Q(OPTO+"locknode.cpp");
  static final Q _locknode_hpp                   = new Q(OPTO+"locknode.hpp");
  static final Q _loopLock_cpp                   = new Q(OPTO+"loopLock.cpp");
  static final Q _loopTransform_cpp              = new Q(OPTO+"loopTransform.cpp");
  static final Q _loopUnswitch_cpp               = new Q(OPTO+"loopUnswitch.cpp");
  static final Q _loopnode_cpp                   = new Q(OPTO+"loopnode.cpp");
  static final Q _loopnode_hpp                   = new Q(OPTO+"loopnode.hpp");
  static final Q _loopopts_cpp                   = new Q(OPTO+"loopopts.cpp");
  static final Q _machnode_cpp                   = new Q(OPTO+"machnode.cpp");
  static final Q _machnode_hpp                   = new Q(OPTO+"machnode.hpp");
  static final Q _macro_cpp                      = new Q(OPTO+"macro.cpp");
  static final Q _macro_hpp                      = new Q(OPTO+"macro.hpp");
  static final Q _matcher_cpp                    = new Q(OPTO+"matcher.cpp");
  static final Q _matcher_hpp                    = new Q(OPTO+"matcher.hpp");
  static final Q _memnode_cpp                    = new Q(OPTO+"memnode.cpp");
  static final Q _memnode_hpp                    = new Q(OPTO+"memnode.hpp");
  static final Q _mulnode_cpp                    = new Q(OPTO+"mulnode.cpp");
  static final Q _mulnode_hpp                    = new Q(OPTO+"mulnode.hpp");
  static final Q _multnode_cpp                   = new Q(OPTO+"multnode.cpp");
  static final Q _multnode_hpp                   = new Q(OPTO+"multnode.hpp");
  static final Q _node_cpp                       = new Q(OPTO+"node.cpp");
  static final Q _node_hpp                       = new Q(OPTO+"node.hpp");
  static final Q _opcodes_cpp                    = new Q(OPTO+"opcodes.cpp");
  static final Q _opcodes_hpp                    = new Q(OPTO+"opcodes.hpp");
  static final Q _output_cpp                     = new Q(OPTO+"output.cpp");
  static final Q _output_hpp                     = new Q(OPTO+"output.hpp");
  static final Q _parse1_cpp                     = new Q(OPTO+"parse1.cpp");
  static final Q _parse2_cpp                     = new Q(OPTO+"parse2.cpp");
  static final Q _parse3_cpp                     = new Q(OPTO+"parse3.cpp");
  static final Q _parseHelper_cpp                = new Q(OPTO+"parseHelper.cpp");
  static final Q _parse_hpp                      = new Q(OPTO+"parse.hpp");
  static final Q _phaseX_cpp                     = new Q(OPTO+"phaseX.cpp");
  static final Q _phaseX_hpp                     = new Q(OPTO+"phaseX.hpp");
  static final Q _phase_cpp                      = new Q(OPTO+"phase.cpp");
  static final Q _phase_hpp                      = new Q(OPTO+"phase.hpp");
  static final Q _postaloc_cpp                   = new Q(OPTO+"postaloc.cpp");
  static final Q _reg_split_cpp                  = new Q(OPTO+"reg_split.cpp");
  static final Q _regalloc_cpp                   = new Q(OPTO+"regalloc.cpp");
  static final Q _regalloc_hpp                   = new Q(OPTO+"regalloc.hpp");
  static final Q _regmask_cpp                    = new Q(OPTO+"regmask.cpp");
  static final Q _regmask_hpp                    = new Q(OPTO+"regmask.hpp");
  static final Q _rootnode_cpp                   = new Q(OPTO+"rootnode.cpp");
  static final Q _rootnode_hpp                   = new Q(OPTO+"rootnode.hpp");
  static final Q _runtime_cpp                    = new Q(OPTO+"runtime.cpp");
  static final Q _runtime_hpp                    = new Q(OPTO+"runtime.hpp");
  static final Q _split_if_cpp                   = new Q(OPTO+"split_if.cpp");
  static final Q _subnode_cpp                    = new Q(OPTO+"subnode.cpp");
  static final Q _subnode_hpp                    = new Q(OPTO+"subnode.hpp");
  static final Q _superword_cpp                  = new Q(OPTO+"superword.cpp");
  static final Q _superword_hpp                  = new Q(OPTO+"superword.hpp");
  static final Q _type_cpp                       = new Q(OPTO+"type.cpp");
  static final Q _type_hpp                       = new Q(OPTO+"type.hpp");
  static final Q _vectornode_cpp                 = new Q(OPTO+"vectornode.cpp");
  static final Q _vectornode_hpp                 = new Q(OPTO+"vectornode.hpp");

  static final String PRIMS                      = "src/share/vm/prims/";
  static final Q _jni_cpp                        = new Q(PRIMS+"jni.cpp");
  static final Q _jniCheck_cpp                   = new Q(PRIMS+"jniCheck.cpp");
  static final Q _jniCheck_hpp                   = new Q(PRIMS+"jniCheck.hpp");
  static final Q _jniFastGetField_cpp            = new Q(PRIMS+"jniFastGetField.cpp");
  static final Q _jniFastGetField_hpp            = new Q(PRIMS+"jniFastGetField.hpp");
  static final Q _jni_h                          = new Q(PRIMS+"jni.h");
  static final Q _jni_hpp                        = new Q(PRIMS+"jni.hpp");
  static final Q _jni_md_h                       = new Q(PRIMS+"jni_md.h");
  static final Q _jvm_cpp                        = new Q(PRIMS+"jvm.cpp");
  static final Q _jvm_h                          = new Q(PRIMS+"jvm.h");
  static final Q _jvm_misc_hpp                   = new Q(PRIMS+"jvm_misc.hpp");
  static final Q _jvmtiAgentThread_hpp           = new Q(PRIMS+"jvmtiAgentThread.hpp");
  static final Q _jvmtiClassFileReconstituter_cpp = new Q(PRIMS+"jvmtiClassFileReconstituter.cpp");
  static final Q _jvmtiClassFileReconstituter_hpp = new Q(PRIMS+"jvmtiClassFileReconstituter.hpp");
  static final Q _jvmtiCodeBlobEvents_cpp        = new Q(PRIMS+"jvmtiCodeBlobEvents.cpp");
  static final Q _jvmtiCodeBlobEvents_hpp        = new Q(PRIMS+"jvmtiCodeBlobEvents.hpp");
  static final Q _jvmtiEnter_hpp                 = new Q(PRIMS+"jvmtiEnter.hpp");
  static final Q _jvmtiEnvBase_cpp               = new Q(PRIMS+"jvmtiEnvBase.cpp");
  static final Q _jvmtiEnvBase_hpp               = new Q(PRIMS+"jvmtiEnvBase.hpp");
  static final Q _jvmtiEnv_cpp                   = new Q(PRIMS+"jvmtiEnv.cpp");
  static final Q _jvmtiEnvThreadState_cpp        = new Q(PRIMS+"jvmtiEnvThreadState.cpp");
  static final Q _jvmtiEnvThreadState_hpp        = new Q(PRIMS+"jvmtiEnvThreadState.hpp");
  static final Q _jvmtiEnvThreadState_inline_hpp = new Q(PRIMS+"jvmtiEnvThreadState.inline.hpp");
  static final Q _jvmtiEventController_cpp       = new Q(PRIMS+"jvmtiEventController.cpp");
  static final Q _jvmtiEventController_hpp       = new Q(PRIMS+"jvmtiEventController.hpp");
  static final Q _jvmtiEventController_inline_hpp= new Q(PRIMS+"jvmtiEventController.inline.hpp");
  static final Q _jvmtiExport_cpp                = new Q(PRIMS+"jvmtiExport.cpp");
  static final Q _jvmtiExport_hpp                = new Q(PRIMS+"jvmtiExport.hpp");
  static final Q _jvmtiExtensions_cpp            = new Q(PRIMS+"jvmtiExtensions.cpp");
  static final Q _jvmtiExtensions_hpp            = new Q(PRIMS+"jvmtiExtensions.hpp");
  static final Q _jvmtiGetLoadedClasses_cpp      = new Q(PRIMS+"jvmtiGetLoadedClasses.cpp");
  static final Q _jvmtiGetLoadedClasses_hpp      = new Q(PRIMS+"jvmtiGetLoadedClasses.hpp");
  static final Q _jvmtiImpl_cpp                  = new Q(PRIMS+"jvmtiImpl.cpp");
  static final Q _jvmtiImpl_hpp                  = new Q(PRIMS+"jvmtiImpl.hpp");
  static final Q _jvmtiManageCapabilities_cpp    = new Q(PRIMS+"jvmtiManageCapabilities.cpp");
  static final Q _jvmtiManageCapabilities_hpp    = new Q(PRIMS+"jvmtiManageCapabilities.hpp");
  static final Q _jvmtiRedefineClasses_cpp       = new Q(PRIMS+"jvmtiRedefineClasses.cpp");
  static final Q _jvmtiRedefineClasses_hpp       = new Q(PRIMS+"jvmtiRedefineClasses.hpp");
  static final Q _jvmtiTagMap_cpp                = new Q(PRIMS+"jvmtiTagMap.cpp");
  static final Q _jvmtiTagMap_hpp                = new Q(PRIMS+"jvmtiTagMap.hpp");
  static final Q _jvmtiThreadState_cpp           = new Q(PRIMS+"jvmtiThreadState.cpp");
  static final Q _jvmtiThreadState_hpp           = new Q(PRIMS+"jvmtiThreadState.hpp");
  static final Q _jvmtiThreadState_inline_hpp    = new Q(PRIMS+"jvmtiThreadState.inline.hpp");
  static final Q _methodComparator_cpp           = new Q(PRIMS+"methodComparator.cpp");
  static final Q _methodComparator_hpp           = new Q(PRIMS+"methodComparator.hpp");
  static final Q _nativeLookup_cpp               = new Q(PRIMS+"nativeLookup.cpp");
  static final Q _nativeLookup_hpp               = new Q(PRIMS+"nativeLookup.hpp");
  static final Q _perf_cpp                       = new Q(PRIMS+"perf.cpp");
  static final Q _privilegedStack_cpp            = new Q(PRIMS+"privilegedStack.cpp");
  static final Q _privilegedStack_hpp            = new Q(PRIMS+"privilegedStack.hpp");
  static final Q _unsafe_cpp                     = new Q(PRIMS+"unsafe.cpp");

  static final Q _jvmtiEnter_xsl                 = new Q(PRIMS+"jvmtiEnter.xsl");
  static final Q _jvmtiEnv_xsl                   = new Q(PRIMS+"jvmtiEnv.xsl");
  static final Q _jvmtiH_xsl                     = new Q(PRIMS+"jvmtiH.xsl");
  static final Q _jvmtiHpp_xsl                   = new Q(PRIMS+"jvmtiHpp.xsl");
  static final Q _jvmtiLib_xsl                   = new Q(PRIMS+"jvmtiLib.xsl");
  static final Q _jvmti_xml                      = new Q(PRIMS+"jvmti.xml");

  static final String RUNTIME                    = "src/share/vm/runtime/";
  static final Q _aprofiler_hpp                  = new Q(RUNTIME+"aprofiler.hpp");
  static final Q _arguments_cpp                  = new Q(RUNTIME+"arguments.cpp");
  static final Q _arguments_hpp                  = new Q(RUNTIME+"arguments.hpp");
  static final Q _atomic_cpp                     = new Q(RUNTIME+"atomic.cpp");
  static final Q _atomic_hpp                     = new Q(RUNTIME+"atomic.hpp");
  static final Q _compilationPolicy_cpp          = new Q(RUNTIME+"compilationPolicy.cpp");
  static final Q _compilationPolicy_hpp          = new Q(RUNTIME+"compilationPolicy.hpp");
  static final Q _fieldDescriptor_cpp            = new Q(RUNTIME+"fieldDescriptor.cpp");
  static final Q _fieldDescriptor_hpp            = new Q(RUNTIME+"fieldDescriptor.hpp");
  static final Q _fieldType_cpp                  = new Q(RUNTIME+"fieldType.cpp");
  static final Q _fieldType_hpp                  = new Q(RUNTIME+"fieldType.hpp");
  static final Q _frame_cpp                      = new Q(RUNTIME+"frame.cpp");
  static final Q _frame_hpp                      = new Q(RUNTIME+"frame.hpp");
  static final Q _frame_inline_hpp               = new Q(RUNTIME+"frame.inline.hpp");
  static final Q _globals_cpp                    = new Q(RUNTIME+"globals.cpp");
  static final Q _globals_extension_hpp          = new Q(RUNTIME+"globals_extension.hpp");
  static final Q _globals_hpp                    = new Q(RUNTIME+"globals.hpp");
  static final Q _handles_cpp                    = new Q(RUNTIME+"handles.cpp");
  static final Q _handles_hpp                    = new Q(RUNTIME+"handles.hpp");
  static final Q _handles_inline_hpp             = new Q(RUNTIME+"handles.inline.hpp");
  static final Q _hpi_cpp                        = new Q(RUNTIME+"hpi.cpp");
  static final Q _hpi_hpp                        = new Q(RUNTIME+"hpi.hpp");
  static final Q _hpi_imported_h                 = new Q(RUNTIME+"hpi_imported.h");
  static final Q _icache_hpp                     = new Q(RUNTIME+"icache.hpp");
  static final Q _init_cpp                       = new Q(RUNTIME+"init.cpp");
  static final Q _init_hpp                       = new Q(RUNTIME+"init.hpp");
  static final Q _interfaceSupport_cpp           = new Q(RUNTIME+"interfaceSupport.cpp");
  static final Q _interfaceSupport_hpp           = new Q(RUNTIME+"interfaceSupport.hpp");
  static final Q _javaCalls_cpp                  = new Q(RUNTIME+"javaCalls.cpp");
  static final Q _javaCalls_hpp                  = new Q(RUNTIME+"javaCalls.hpp");
  static final Q _javaFrameAnchor_hpp            = new Q(RUNTIME+"javaFrameAnchor.hpp");
  static final Q _java_cpp                       = new Q(RUNTIME+"java.cpp");
  static final Q _java_hpp                       = new Q(RUNTIME+"java.hpp");
  static final Q _jfieldID_hpp                   = new Q(RUNTIME+"jfieldIDWorkaround.hpp");
  static final Q _jniHandles_cpp                 = new Q(RUNTIME+"jniHandles.cpp");
  static final Q _jniHandles_hpp                 = new Q(RUNTIME+"jniHandles.hpp");
  static final Q _jniPeriodicChecker_hpp         = new Q(RUNTIME+"jniPeriodicChecker.hpp");
  static final Q _memprofiler_hpp                = new Q(RUNTIME+"memprofiler.hpp");
  static final Q _orderAccess_cpp                = new Q(RUNTIME+"orderAccess.cpp");
  static final Q _orderAccess_hpp                = new Q(RUNTIME+"orderAccess.hpp");
  static final Q _osThread_cpp                   = new Q(RUNTIME+"osThread.cpp");
  static final Q _osThread_hpp                   = new Q(RUNTIME+"osThread.hpp");
  static final Q _os_cpp                         = new Q(RUNTIME+"os.cpp");
  static final Q _os_hpp                         = new Q(RUNTIME+"os.hpp");
  static final Q _perfData_hpp                   = new Q(RUNTIME+"perfData.hpp");
  static final Q _perfData_cpp                   = new Q(RUNTIME+"perfData.cpp");
  static final Q _perfMemory_cpp                 = new Q(RUNTIME+"perfMemory.cpp");
  static final Q _perfMemory_hpp                 = new Q(RUNTIME+"perfMemory.hpp");
  static final Q _prefetch_hpp                   = new Q(RUNTIME+"prefetch.hpp");
  static final Q _reflection_cpp                 = new Q(RUNTIME+"reflection.cpp");
  static final Q _reflection_hpp                 = new Q(RUNTIME+"reflection.hpp");
  static final Q _reflectionCompat_hpp           = new Q(RUNTIME+"reflectionCompat.hpp");
  static final Q _reflectionUtils_cpp            = new Q(RUNTIME+"reflectionUtils.cpp");
  static final Q _reflectionUtils_hpp            = new Q(RUNTIME+"reflectionUtils.hpp");
  static final Q _relocator_cpp                  = new Q(RUNTIME+"relocator.cpp");
  static final Q _relocator_hpp                  = new Q(RUNTIME+"relocator.hpp");
  static final Q _rframe_cpp                     = new Q(RUNTIME+"rframe.cpp");
  static final Q _rframe_hpp                     = new Q(RUNTIME+"rframe.hpp");
  static final Q _safepoint_cpp                  = new Q(RUNTIME+"safepoint.cpp");
  static final Q _safepoint_hpp                  = new Q(RUNTIME+"safepoint.hpp");
  static final Q _sharedRuntime_cpp              = new Q(RUNTIME+"sharedRuntime.cpp");
  static final Q _sharedRuntime_hpp              = new Q(RUNTIME+"sharedRuntime.hpp");
  static final Q _sharedRuntimeTrig_cpp          = new Q(RUNTIME+"sharedRuntimeTrig.cpp");
  static final Q _sharedRuntimeTrans_cpp         = new Q(RUNTIME+"sharedRuntimeTrans.cpp");
  static final Q _signature_cpp                  = new Q(RUNTIME+"signature.cpp");
  static final Q _signature_hpp                  = new Q(RUNTIME+"signature.hpp");
  static final Q _statSampler_cpp                = new Q(RUNTIME+"statSampler.cpp");
  static final Q _statSampler_hpp                = new Q(RUNTIME+"statSampler.hpp");
  static final Q _stubCodeGenerator_cpp          = new Q(RUNTIME+"stubCodeGenerator.cpp");
  static final Q _stubCodeGenerator_hpp          = new Q(RUNTIME+"stubCodeGenerator.hpp");
  static final Q _stubRoutines_cpp               = new Q(RUNTIME+"stubRoutines.cpp");
  static final Q _stubRoutines_hpp               = new Q(RUNTIME+"stubRoutines.hpp");
  static final Q _task_cpp                       = new Q(RUNTIME+"task.cpp");
  static final Q _task_hpp                       = new Q(RUNTIME+"task.hpp");
  static final Q _thread_cpp                     = new Q(RUNTIME+"thread.cpp");
  static final Q _thread_hpp                     = new Q(RUNTIME+"thread.hpp");
  static final Q _timer_cpp                      = new Q(RUNTIME+"timer.cpp");
  static final Q _timer_hpp                      = new Q(RUNTIME+"timer.hpp");
  static final Q _virtualspace_cpp               = new Q(RUNTIME+"virtualspace.cpp");
  static final Q _virtualspace_hpp               = new Q(RUNTIME+"virtualspace.hpp");
  static final Q _vmThread_cpp                   = new Q(RUNTIME+"vmThread.cpp");
  static final Q _vmThread_hpp                   = new Q(RUNTIME+"vmThread.hpp");
  static final Q _vm_operations_cpp              = new Q(RUNTIME+"vm_operations.cpp");
  static final Q _vm_operations_hpp              = new Q(RUNTIME+"vm_operations.hpp");
  static final Q _vm_version_cpp                 = new Q(RUNTIME+"vm_version.cpp");
  static final Q _vm_version_hpp                 = new Q(RUNTIME+"vm_version.hpp");

  static final String SERVICES                   = "src/share/vm/services/";
  static final Q _attachListener_cpp             = new Q(SERVICES+"attachListener.cpp");
  static final Q _attachListener_hpp             = new Q(SERVICES+"attachListener.hpp");
  static final Q _classLoadingService_cpp        = new Q(SERVICES+"classLoadingService.cpp");
  static final Q _classLoadingService_hpp        = new Q(SERVICES+"classLoadingService.hpp");
  static final Q _heapDumper_cpp                 = new Q(SERVICES+"heapDumper.cpp");
  static final Q _heapDumper_hpp                 = new Q(SERVICES+"heapDumper.hpp");
  static final Q _jmm_h                          = new Q(SERVICES+"jmm.h");
  static final Q _lowMemoryDetector_cpp          = new Q(SERVICES+"lowMemoryDetector.cpp");
  static final Q _lowMemoryDetector_hpp          = new Q(SERVICES+"lowMemoryDetector.hpp");
  static final Q _management_cpp                 = new Q(SERVICES+"management.cpp");
  static final Q _management_hpp                 = new Q(SERVICES+"management.hpp");
  static final Q _memoryManager_cpp              = new Q(SERVICES+"memoryManager.cpp");
  static final Q _memoryManager_hpp              = new Q(SERVICES+"memoryManager.hpp");
  static final Q _memoryPool_cpp                 = new Q(SERVICES+"memoryPool.cpp");
  static final Q _memoryPool_hpp                 = new Q(SERVICES+"memoryPool.hpp");
  static final Q _memoryService_cpp              = new Q(SERVICES+"memoryService.cpp");
  static final Q _memoryService_hpp              = new Q(SERVICES+"memoryService.hpp");
  static final Q _memoryUsage_hpp                = new Q(SERVICES+"memoryUsage.hpp");
  static final Q _psMemoryPool_cpp               = new Q(SERVICES+"psMemoryPool.cpp");
  static final Q _psMemoryPool_hpp               = new Q(SERVICES+"psMemoryPool.hpp");
  static final Q _runtimeService_cpp             = new Q(SERVICES+"runtimeService.cpp");
  static final Q _runtimeService_hpp             = new Q(SERVICES+"runtimeService.hpp");
  static final Q _serviceUtil_hpp                = new Q(SERVICES+"serviceUtil.hpp");
  static final Q _threadService_cpp              = new Q(SERVICES+"threadService.cpp");
  static final Q _threadService_hpp              = new Q(SERVICES+"threadService.hpp");

  static final String UTILITIES                  = "src/share/vm/utilities/";
  static final Q _accessFlags_cpp                = new Q(UTILITIES+"accessFlags.cpp");
  static final Q _accessFlags_hpp                = new Q(UTILITIES+"accessFlags.hpp");
  static final Q _array_cpp                      = new Q(UTILITIES+"array.cpp");
  static final Q _array_hpp                      = new Q(UTILITIES+"array.hpp");
  static final Q _bitMap_cpp                     = new Q(UTILITIES+"bitMap.cpp");
  static final Q _bitMap_hpp                     = new Q(UTILITIES+"bitMap.hpp");
  static final Q _bitOps_hpp                     = new Q(UTILITIES+"bitOps.hpp");
  static final Q _bitMap_inline_hpp              = new Q(UTILITIES+"bitMap.inline.hpp");
  static final Q _constantTag_hpp                = new Q(UTILITIES+"constantTag.hpp");
  static final Q _constantTag_cpp                = new Q(UTILITIES+"constantTag.cpp");
  static final Q _copy_cpp                       = new Q(UTILITIES+"copy.cpp");
  static final Q _copy_hpp                       = new Q(UTILITIES+"copy.hpp");
  static final Q _copy_inline_hpp                = new Q(UTILITIES+"copy.inline.hpp");
  static final Q _debug_cpp                      = new Q(UTILITIES+"debug.cpp");
  static final Q _debug_hpp                      = new Q(UTILITIES+"debug.hpp");
  static final Q _defaultStream_hpp              = new Q(UTILITIES+"defaultStream.hpp");
  static final Q _exceptions_cpp                 = new Q(UTILITIES+"exceptions.cpp");
  static final Q _exceptions_hpp                 = new Q(UTILITIES+"exceptions.hpp");
  static final Q _global_defs_azgcc_hpp          = new Q(UTILITIES+"globalDefinitions_azgcc.hpp");
  static final Q _global_defs_cpp                = new Q(UTILITIES+"globalDefinitions.cpp");
  static final Q _global_defs_hpp                = new Q(UTILITIES+"globalDefinitions.hpp");
  static final Q _growableArray_cpp              = new Q(UTILITIES+"growableArray.cpp");
  static final Q _growableArray_hpp              = new Q(UTILITIES+"growableArray.hpp");
  static final Q _hashtable_cpp                  = new Q(UTILITIES+"hashtable.cpp");
  static final Q _hashtable_hpp                  = new Q(UTILITIES+"hashtable.hpp");
  static final Q _hashtable_inline_hpp           = new Q(UTILITIES+"hashtable.inline.hpp");
  static final Q _histogram_hpp                  = new Q(UTILITIES+"histogram.hpp");
  static final Q _macros_hpp                     = new Q(UTILITIES+"macros.hpp"); 
  static final Q _ostream_cpp                    = new Q(UTILITIES+"ostream.cpp");
  static final Q _ostream_hpp                    = new Q(UTILITIES+"ostream.hpp");
  static final Q _preserveEx_cpp                 = new Q(UTILITIES+"preserveException.cpp");
  static final Q _preserveEx_hpp                 = new Q(UTILITIES+"preserveException.hpp");
  static final Q _sizes_hpp                      = new Q(UTILITIES+"sizes.hpp");
  static final Q _taskqueue_cpp                  = new Q(UTILITIES+"taskqueue.cpp");
  static final Q _taskqueue_hpp                  = new Q(UTILITIES+"taskqueue.hpp");
  static final Q _uft8_cpp                       = new Q(UTILITIES+"utf8.cpp");
  static final Q _uft8_hpp                       = new Q(UTILITIES+"utf8.hpp");
  static final Q _vmError_cpp                    = new Q(UTILITIES+"vmError.cpp");
  static final Q _vmError_hpp                    = new Q(UTILITIES+"vmError.hpp");
  static final Q _workgroup_hpp                  = new Q(UTILITIES+"workgroup.hpp");
  static final Q _xmlstream_cpp                  = new Q(UTILITIES+"xmlstream.cpp");
  static final Q _xmlstream_hpp                  = new Q(UTILITIES+"xmlstream.hpp");


  // --- ADLC
  static final String ADLC                       = "src/share/vm/adlc/";
  static final Q _adlc_hpp                       = new Q(ADLC+"adlc.hpp");
  static final Q _adlcVMDeps_hpp                 = new Q(ADLC+"adlcVMDeps.hpp");
  static final Q _adlparse_cpp                   = new Q(ADLC+"adlparse.cpp");
  static final Q _adlparse_hpp                   = new Q(ADLC+"adlparse.hpp");
  static final Q _archDesc_cpp                   = new Q(ADLC+"archDesc.cpp");
  static final Q _archDesc_hpp                   = new Q(ADLC+"archDesc.hpp");
  static final Q _arena_cpp                      = new Q(ADLC+"arena.cpp");
  static final Q _arena_hpp                      = new Q(ADLC+"arena.hpp");
  static final Q _dfa_cpp                        = new Q(ADLC+"dfa.cpp");
  static final Q _dict2_cpp                      = new Q(ADLC+"dict2.cpp");
  static final Q _dict2_hpp                      = new Q(ADLC+"dict2.hpp");
  static final Q _filebuff_cpp                   = new Q(ADLC+"filebuff.cpp");
  static final Q _filebuff_hpp                   = new Q(ADLC+"filebuff.hpp");
  static final Q _forms_cpp                      = new Q(ADLC+"forms.cpp");
  static final Q _forms_hpp                      = new Q(ADLC+"forms.hpp");
  static final Q _formsopt_cpp                   = new Q(ADLC+"formsopt.cpp");
  static final Q _formsopt_hpp                   = new Q(ADLC+"formsopt.hpp");
  static final Q _formssel_cpp                   = new Q(ADLC+"formssel.cpp");
  static final Q _formssel_hpp                   = new Q(ADLC+"formssel.hpp");
  static final Q _adlcmain_cpp                   = new Q(ADLC+"adlcmain.cpp");
  static final Q _output_c_cpp                   = new Q(ADLC+"output_c.cpp");
  static final Q _output_h_cpp                   = new Q(ADLC+"output_h.cpp");
  static final Q ADLC_CPPS[] = {
    _adlcmain_cpp,
    _adlparse_cpp,
    _archDesc_cpp,
    _arena_cpp,
    _dfa_cpp,
    _dict2_cpp,
    _filebuff_cpp,
    _forms_cpp,
    _formsopt_cpp,
    _formssel_cpp,
    _opcodes_cpp,
    _output_c_cpp,
    _output_h_cpp
  };

  // adlc objects and includes are generated here...
  static final String OBJDIR_ADLC_JIT6_TARGET
                      = (TARGET_OSN.equals("azproxied")) ? OBJDIR_AZPROXIED_X86_JIT6 
                        : OBJDIR_AZLINUX_X86_JIT6;
 
  // --- JVMTI (blech!!!)
  static final String JVMTI_DIR = PRIMS + "generated_jvmtifiles/" + TARGET_OSN + "/";

  static final Q _jvmtiGen_j = new Q (PRIMS+"jvmtiGen.java");
  static final Q _jvmtiGen_c = new QS(JVMTI_DIR+"jvmtiGen.class", JAVAC + " -d "+JVMTI_DIR+" %src",_jvmtiGen_j);

  static final Q _jvmtiEnvFill_j = new Q (PRIMS+"jvmtiEnvFill.java");
  static final Q _jvmtiEnvFill_c = new QS(JVMTI_DIR+"jvmtiEnvFill.class", JAVAC + " -d "+JVMTI_DIR+" %src",_jvmtiEnvFill_j);

  static final Q _jvmti_h    = new QS(JVMTI_DIR+"jvmti.h",
                                      JAVA + " -cp " + JVMTI_DIR + " jvmtiGen -IN %src0 -XSL %src1 -OUT %dst",
                                      ' ',_jvmti_xml,_jvmtiH_xsl,_jvmtiLib_xsl,_jvmtiGen_c);

  static final Q _jvmtiEnv_hpp=new QS(JVMTI_DIR+"jvmtiEnv.hpp",
                                      JAVA + " -cp " + JVMTI_DIR + " jvmtiGen -IN %src0 -XSL %src1 -OUT %dst",
                                      ' ',_jvmti_xml,_jvmtiHpp_xsl,_jvmtiLib_xsl,_jvmtiGen_c);

  static final Q _jvmtiEnterTrace_cpp=new QS(JVMTI_DIR+"jvmtiEnterTrace.cpp",
                                      JAVA + " -cp " + JVMTI_DIR + " jvmtiGen -IN %src0 -XSL %src1 -OUT %dst -PARAM interface jvmti -PARAM trace Trace",
                                      ' ',_jvmti_xml,_jvmtiEnter_xsl,_jvmtiLib_xsl,_jvmtiGen_c);
  static final Q _jvmtiEnter_cpp=new QS(JVMTI_DIR+"jvmtiEnter.cpp",
                                      JAVA + " -cp " + JVMTI_DIR + " jvmtiGen -IN %src0 -XSL %src1 -OUT %dst -PARAM interface jvmti",
                                      ' ',_jvmti_xml,_jvmtiEnter_xsl,_jvmtiLib_xsl,_jvmtiGen_c);

  // --- OopClosures
  static final Q _oopClosures_cpp = new QS(OOPS+"oopClosures.cpp", _oopClosures_py._target+" %dst", _oopClosures_py);
  

  // --- Includes
  static final String INCLUDES = 
    " -I " + OBJDIR_ADLC_JIT6_TARGET +"adlc" +
    " -I " + ADLC +
    " -I " + AZUL_CODE + 
    " -I " + AZUL_COMPILER + 
    " -I " + AZUL_GC_GPGC + 
    " -I " + AZUL_GC_SHARED + 
    " -I " + AZUL_OOPS + 
    " -I " + AZUL_ARTA + 
    " -I " + AZUL_RUNTIME + 
    " -I " + C1 + 
    " -I " + CI + 
    " -I " + CLASSFILE + 
    " -I " + CODE + 
    " -I " + COMPILER + 
    " -I " + GC_INTERFACE + 
    " -I " + GC_SHARED + 
    " -I " + GC_PARALLELSCAVENGE + 
    " -I " + INTERPRETER + 
    " -I " + JVMTI_DIR + 
    " -I " + LIBADT + 
    " -I " + MEMORY + 
    " -I " + OOPS + 
    " -I " + OPTO + 
    " -I " + PRIMS +
    " -I " + RUNTIME +
    " -I " + SERVICES +
    " -I " + UTILITIES;


  // --- DEFINES for the different build flavors
  static final String JDK_RELEASE_VERSION     = _jdkversion;
  // JDK_VERSION needs to be an integer.
  static final String JDK_VERSION             = JDK_RELEASE_VERSION.replaceAll("[._]", "");
  static final String BUILD_NUMBER            = _buildnumber;
  static final String BUILD_VERSION           = _releaseversion + "-b" + _buildnumber;
  // by the user while building hotspot
  static final String BUILD_USER              = System.getProperty("user.name", "unknown");
  static final String AVM_RELEASE_VERSION     = JDK_RELEASE_VERSION + "-AVM_" + BUILD_VERSION;
  static final String HOTSPOT_RELEASE_VERSION = JDK_RELEASE_VERSION + "-b" + BUILD_NUMBER;

  static final String VERS_FLAGS = " -DHOTSPOT_BUILD_USER=" + BUILD_USER +
                                   " -DJDK_VERSION=" + JDK_VERSION +
                                   " -DAVM_RELEASE_VERSION=" + AVM_RELEASE_VERSION +
                                   " -DHOTSPOT_RELEASE_VERSION=" + HOTSPOT_RELEASE_VERSION + " ";

  static final String GLIBC_64_BIT_FLAGS       = " -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE ";
  static final String AZNIX_FLAGS              = " -DAZNIX_API_VERSION="+_aznix_api_version+ " ";

  static final String DEBUG_FLAGS              = " -DDEBUG -DASSERT ";
  static final String FASTDEBUG_FLAGS          = " -DFASTDEBUG -DASSERT "+GCC_WARN_OPTIMIZED;
  static final String OPTIMIZED_FLAGS          = " -DOPTIMIZED"+GCC_WARN_OPTIMIZED;
  static final String PRODUCT_FLAGS            = " -DPRODUCT "+GCC_WARN_OPTIMIZED;

  static final String AZPROXIED_X86_FLAGS_CORE = " -DAZ_PROFILER -DAZUL -DAZ_X86 -DAZ_PROXIED -DCORE -DX86_64 -DVM_LITTLE_ENDIAN "+VERS_FLAGS+GLIBC_64_BIT_FLAGS+AZNIX_FLAGS;
  static final String AZLINUX_X86_FLAGS_CORE   = "               -DAZUL -DAZ_X86              -DCORE -DX86_64 -DVM_LITTLE_ENDIAN -DAZPROF_NO_OPENSSL "+VERS_FLAGS+GLIBC_64_BIT_FLAGS+AZNIX_FLAGS;
  static final String AZPROXIED_X86_FLAGS_JIT  = " -DAZ_PROFILER -DAZUL -DAZ_X86 -DAZ_PROXIED        -DX86_64 -DVM_LITTLE_ENDIAN "+VERS_FLAGS+GLIBC_64_BIT_FLAGS+AZNIX_FLAGS;
  static final String AZLINUX_X86_FLAGS_JIT    = "               -DAZUL -DAZ_X86                     -DX86_64 -DVM_LITTLE_ENDIAN -DAZPROF_NO_OPENSSL "+VERS_FLAGS+GLIBC_64_BIT_FLAGS+AZNIX_FLAGS;


  static final Q _proxy_jdk_version_c = new Q(SANDBOX+"/cross/src/proxy/proxy_jdk_version.c");

  // A list of all HotSpot CPP files
  static final Q HOTSPOT_CPPS[] = 
  { 
    _accessFlags_cpp,
    _adaptiveSizePolicy_cpp,
    _adjoiningGenerations_cpp,
    _adjoiningVirtualSpaces_cpp,
    _allocatedObjects_cpp,
    _allocation_cpp,
    _arguments_cpp,
    _arrayKlassKlass_cpp,
    _arrayKlass_cpp,
    _array_cpp,
    _atomic_cpp,
    _attachListener_cpp,
    _auditTrail_cpp,
    _bitMap_cpp,
    _bytecode_cpp,
    _bytecodes_cpp,
    _bytecodeStream_cpp,
    _bytecodeHistogram_cpp,
    _bytecodeTracer_cpp,
    _c1_globals_cpp,
    _c2_globals_cpp,
    _cardTableExtension_cpp,
    _cardTableModRefBS_cpp,
    _classFileError_cpp,
    _classFileParser_cpp,
    _classFileStream_cpp,
    _classLoader_cpp,
    _classLoadingService_cpp,
    _codeBlob_cpp,
    _codeCacheOopTable_cpp,
    _codeCache_cpp,
    _collectedHeap_cpp,
    _collectorCounters_cpp,
    _collectorPolicy_cpp,
    _commonAsm_cpp,
    _compactingPermGenGen_cpp,
    _compressedStream_cpp,
    _constMethodKlass_cpp,
    _constMethodOop_cpp,
    _constantPoolKlass_cpp,
    _constantPoolOop_cpp,
    _constantTag_cpp,
    _copy_cpp,
    _cpCacheKlass_cpp,
    _cpCacheOop_cpp,
    _cycleCounts_cpp,
    _debug_cpp,
    _dependencies_cpp,
    _dict_cpp,
    _dictionary_cpp,
    _exceptions_cpp,
    _fieldDescriptor_cpp,
    _fieldType_cpp,
    _frame_cpp,
    _freezeAndMelt_cpp,
    _gcAdaptivePolicyCounters_cpp,
    _gcLocker_cpp,
    _gcPolicyCounters_cpp,
    _gcStats_cpp,
    _gcTaskManager_cpp,
    _gcTaskThread_cpp,
    _gcUtil_cpp,
    _genOopClosures_cpp,
    _genRemSet_cpp,
    _generateOopMap_cpp,
    _global_defs_cpp,
    _globals_cpp,
    _gpgc_cardTable_cpp,
    _gpgc_closures_cpp,
    _gpgc_collector_cpp,
    _gpgc_debug_cpp,
    _gpgc_gcManagerMark_cpp,
    _gpgc_gcManagerNewFinal_cpp,
    _gpgc_gcManagerNewReloc_cpp,
    _gpgc_gcManagerNewStrong_cpp,
    _gpgc_gcManagerNew_cpp,
    _gpgc_gcManagerOldFinal_cpp,
    _gpgc_gcManagerOldReloc_cpp,
    _gpgc_gcManagerOldStrong_cpp,
    _gpgc_gcManagerOld_cpp,
    _gpgc_generation_cpp,
    _gpgc_heap_cpp,
    _gpgc_heuristic_cpp,
    _gpgc_interlock_cpp,
    _gpgc_layout_cpp,
    _gpgc_lvb_cpp,
    _gpgc_marker_cpp,
    _gpgc_markingQueue_cpp,
    _gpgc_markIterator_cpp,
    _gpgc_marks_cpp,
    _gpgc_metadata_cpp,
    _gpgc_multiPageSpace_cpp,
    _gpgc_newCollector_main_cpp,
    _gpgc_newCollector_mark_cpp,
    _gpgc_newCollector_misc_cpp,
    _gpgc_newCollector_reloc_cpp,
    _gpgc_newCollector_traps_cpp,
    _gpgc_newCollector_verify_cpp,
    _gpgc_nmt_cpp,
    _gpgc_oldCollector_main_cpp,
    _gpgc_oldCollector_mark_cpp,
    _gpgc_oldCollector_misc_cpp,
    _gpgc_oldCollector_reloc_cpp,
    _gpgc_oldCollector_traps_cpp,
    _gpgc_oldCollector_verify_cpp,
    _gpgc_onePageSpace_cpp,
    _gpgc_operation_cpp,
    _gpgc_pageAudit_cpp,
    _gpgc_pageBudget_cpp,
    _gpgc_pageInfo_cpp,
    _gpgc_population_cpp,
    _gpgc_readTrapArray_cpp,
    _gpgc_javaLangRefHandler_cpp,
    _gpgc_relocation_cpp,
    _gpgc_rendezvous_cpp,
    _gpgc_safepoint_cpp,
    _gpgc_slt_cpp,
    _gpgc_space_cpp,
    _gpgc_sparseMappedSpace_cpp,
    _gpgc_stats_cpp,
    _gpgc_tasks_cpp,
    _gpgc_threadCleaner_cpp,
    _gpgc_threadRefLists_cpp,
    _gpgc_thread_cpp,
    _gpgc_tlb_cpp,
    _gpgc_verifyClosure_cpp,
    _gpgc_verify_tasks_cpp,
    _growableArray_cpp,
    _handles_cpp,
    _hashtable_cpp,
    _heapDumper_cpp,
    _heapIterator_cpp,
    _heapRefBuffer_cpp,
    _hpi_cpp,
    _init_cpp,
    _inst_klass_cpp,
    _instanceKlassKlass_cpp,
    _instanceRefKlass_cpp,
    _interfaceSupport_cpp,
    _interpreterRuntime_cpp,
    _interpreter_cpp,
    _invertedVirtualspace_cpp,
    _invocationCounter_cpp,
    _javaAssertions_cpp,
    _javaCalls_cpp,
    _javaClasses_cpp,
    _java_cpp,
    _jniCheck_cpp,
    _jniHandles_cpp,
    _jni_cpp,
    _jniFastGetField_cpp,
    _jvm_cpp,
    _jvmtiClassFileReconstituter_cpp,
    _jvmtiCodeBlobEvents_cpp,
    _jvmtiEnterTrace_cpp, // machine generated file
    _jvmtiEnter_cpp,      // machine generated file
    _jvmtiEnv_cpp,
    _jvmtiEnvBase_cpp,
    _jvmtiEnvThreadState_cpp,
    _jvmtiEventController_cpp,
    _jvmtiExport_cpp,
    _jvmtiExtensions_cpp,
    _jvmtiGetLoadedClasses_cpp,
    _jvmtiImpl_cpp,
    _jvmtiManageCapabilities_cpp,
    _jvmtiRedefineClasses_cpp,
    _jvmtiTagMap_cpp,
    _jvmtiThreadState_cpp,
    _klassIds_cpp,
    _klassKlass_cpp,
    _klassTable_cpp,
    _klassVtable_cpp,
    _klass_cpp,
    _linkResolver_cpp,
    _liveObjects_cpp,
    _loaderConstraints_cpp,
    _log_cpp,
    _lowMemoryDetector_cpp,
    _lvb_cpp,
    _management_cpp,
    _markSweep_cpp,
    _markWord_cpp,
    _memRegion_cpp,
    _memoryManager_cpp,
    _memoryPool_cpp,
    _memoryService_cpp,
    _methodCodeKlass_cpp,
    _methodCodeOop_cpp,
    _methodComparator_cpp,
    _methodKlass_cpp,
    _methodOop_cpp,
    _mutableSpace_cpp,
    _mutexLocker_cpp,
    _mutex_cpp,
    _nativeLookup_cpp,
    _noinline_cpp,
    _objArrayKlassKlass_cpp,
    _objArrayKlass_cpp,
    _objectMonitor_cpp,
    _objectStartArray_cpp,
    _oopClosures_cpp,
    _oopFactory_cpp,
    _oopTable_cpp,
    _oop_cpp,
    _oopsHierarchy_cpp,
    _orderAccess_cpp,
    _osThread_cpp,
    _os_cpp,
    _ostream_cpp,
    _parallelScavengeHeap_cpp,
    _pcMap_cpp,
    _perf_cpp,
    _perfData_cpp,
    _perfMemory_cpp,
    _pgcTaskManager_cpp,
    _pgcTaskThread_cpp,
    _placeholders_cpp,
    _preserveEx_cpp,
    _privilegedStack_cpp,
    _psAdaptiveSizePolicy_cpp,
    _psGCAdaptivePolicyCounters_cpp,
    _psGenerationCounters_cpp,
    _psMarkSweepDecorator_cpp,
    _psMarkSweep_cpp,
    _psMemoryPool_cpp,
    _psOldGen_cpp,
    _psParallelCompact_cpp,
    _psPermGen_cpp,
    _psPromotionLAB_cpp,
    _psPromotionManager_cpp,
    _psScavenge_cpp,
    _psTasks_cpp,
    _psVirtualspace_cpp,
    _psYoungGen_cpp,
    _relocator_cpp,
    _referencePolicy_cpp,
    _referenceProcessor_cpp,
    _reflectionUtils_cpp,
    _reflection_cpp,
    _remoteJNI_cpp,
    _resolutionErrors_cpp,
    _resourceArea_cpp,
    _rewriter_cpp,
    _rlimits_cpp,
    _artaObjects_cpp,
    _artaQuery_cpp,
    _artaThreadState_cpp,
    _runtimeService_cpp,
    _safepoint_cpp,
    _sbaThreadInfo_cpp,
    _sharedRuntimeTrans_cpp,
    _sharedRuntimeTrig_cpp,
    _sharedRuntime_cpp,
    _sharedUserData_cpp,
    _signature_cpp,
    _smaHeuristic_cpp,
    _spaceCounters_cpp,
    _space_cpp,
    _stackBasedAllocation_cpp,
    _stackMapFrame_cpp,
    _stackMapTable_cpp,
    _statSampler_cpp,
    _statistics_cpp,
    _stubCodeGenerator_cpp,
    _stubRoutines_cpp,
    _surrogateLockerThread_cpp,
    _symbolKlass_cpp,
    _symbolOop_cpp,
    _symbolTable_cpp,
    _synchronizer_cpp,
    _systemDictionary_cpp,
    _task_cpp,
    _taskqueue_cpp,
    _templateTable_cpp,
    _threadLocalAlloc_cpp,
    _threadService_cpp,
    _thread_cpp,
    _tickProfiler_cpp,
    _timer_cpp,
    _typeArrayKlassKlass_cpp,
    _typeArrayKlass_cpp,
    _uft8_cpp,
    _universe_cpp,
    _unsafe_cpp,
    _verificationType_cpp,
    _verifier_cpp,
    _vframe_cpp,
    _virtualspace_cpp,
    _vmError_cpp,
    _vmGCOperations_cpp,
    _vmPSOperations_cpp,
    _vmSymbols_cpp,
    _vmTags_cpp,
    _vmThread_cpp,
    _vm_operations_cpp,
    _vm_version_cpp,
    _xmlBuffer_cpp,
    _xmlstream_cpp
  };

  static final Q HOTSPOT_JIT_CPPS[] = {
    _abstractCompiler_cpp,
    _addnode_cpp, 
    _bcEscapeAnalyzer_cpp,
    _block_cpp, 
    _buildOopMap_cpp, 
    _bytecodeInfo_cpp, 
    _c1_CFGPrinter_cpp,
    _c1_Canonicalizer_cpp,
    _c1_Compilation_cpp,
    _c1_Compiler_cpp,
    _c1_Defs_cpp,
    _c1_FrameMap_cpp,
    _c1_GraphBuilder_cpp,
    _c1_IR_cpp,
    _c1_InstructionPrinter_cpp,
    _c1_Instruction_cpp,
    _c1_LIRAssembler_cpp,
    _c1_LIRGenerator_cpp,
    _c1_LIR_cpp,
    _c1_LinearScan_cpp,
    _c1_Optimizer_cpp,
    _c1_Runtime1_cpp,
    _c1_ThreadLocals_cpp,
    _c1_ValueMap_cpp,
    _c1_ValueSet_cpp,
    _c1_ValueStack_cpp,
    _c1_ValueType_cpp,
    _c2compiler_cpp,
    _callGenerator_cpp, 
    _callnode_cpp, 
    _cfgnode_cpp,
    _chaitin_cpp, 
    _chaitin_linux_cpp,
    _ciArrayKlass_cpp,
    _ciArray_cpp,
    _ciConstantPoolCache_cpp,
    _ciConstant_cpp,
    _ciEnv_cpp,
    _ciExceptionHandler_cpp,
    _ciField_cpp,
    _ciFlags_cpp,
    _ciInstanceKlassKlass_cpp,
    _ciInstanceKlass_cpp,
    _ciInstance_cpp,
    _ciKlassKlass_cpp,
    _ciKlass_cpp,
    _ciMethod_cpp,
    _ciMethodBlocks_cpp,
    _ciMethodKlass_cpp,
    _ciNullObject_cpp,
    _ciObjArray_cpp,
    _ciObjArrayKlassKlass_cpp,
    _ciObjArrayKlass_cpp,
    _ciObjectFactory_cpp,
    _ciObject_cpp,
    _ciSignature_cpp,
    _ciStreams_cpp,
    _ciSymbolKlass_cpp,
    _ciSymbol_cpp,
    _ciTypeArrayKlassKlass_cpp,
    _ciTypeArrayKlass_cpp,
    _ciTypeArray_cpp,
    _ciTypeFlow_cpp,
    _ciType_cpp,
    _ciUtilities_cpp,
    _classes_cpp, 
    _coalesce_cpp, 
    _codeProfile_cpp,
    _compilationPolicy_cpp,
    _compileBroker_cpp,
    _compile_cpp,
    _compiledIC_cpp,
    _compilerOracle_cpp,
    _connode_cpp, 
    _deoptimization_cpp,
    _divnode_cpp, 
    _doCall_cpp, 
    _domgraph_cpp, 
    _escape_cpp, 
    _gcm_cpp,  
    _graphKit_cpp, 
    _idealKit_cpp, 
    _ifg_cpp,  
    _ifnode_cpp, 
    _indexSet_cpp, 
    _lcm_cpp,  
    _library_call_cpp, 
    _live_cpp, 
    _locknode_cpp, 
    _loopLock_cpp, 
    _loopTransform_cpp, 
    _loopUnswitch_cpp, 
    _loopnode_cpp, 
    _loopopts_cpp, 
    _machnode_cpp, 
    _macro_cpp, 
    _matcher_cpp,
    _memnode_cpp,
    _methodLiveness_cpp,
    _mulnode_cpp, 
    _multnode_cpp, 
    _node_cpp,
    _output_cpp, 
    _parse1_cpp, 
    _parse2_cpp, 
    _parse3_cpp, 
    _parseHelper_cpp, 
    _phaseX_cpp,
    _phase_cpp,
    _port_cpp,
    _postaloc_cpp, 
    _reg_split_cpp, 
    _regalloc_cpp, 
    _regmask_cpp,
    _rframe_cpp,
    _rootnode_cpp, 
    _runtime_cpp, 
    _set_cpp,
    _split_if_cpp, 
    _subnode_cpp, 
    _superword_cpp, 
    _type_cpp,
    _vectornode_cpp, 
    _vectset_cpp
  };

  static final Q HOTSPOT_X86_CPPS[] = {
    _assembler_x86_cpp,
    _bytecodes_x86_cpp,
    _debug_x86_cpp,
    _disassembler_x86_cpp,
    _frame_x86_cpp,
    _icache_x86_cpp,
    _interpreter_x86_cpp,
    _interpreterRT_x86_cpp,
    _interp_masm_x86_cpp,
    _nativeInst_x86_cpp,
    _objectRef_x86_cpp,
    _register_x86_cpp,
    _sharedRuntime_x86_cpp,
    _stackRef_x86_cpp,
    _stubGenerator_x86_cpp,
    _stubRoutines_x86_cpp,
    _templateTable_x86_cpp,
    _tickProfiler_x86_cpp,
    _vm_version_x86_cpp,
    _vtableStubs_x86_cpp
  };

  static final Q HOTSPOT_LINUX_CPPS[] = {
    _attachListener_linux_cpp,
    _assembler_linux_x86_cpp,
    _gpgc_linux_cpp,
    _hpi_linux_cpp,
    _jvm_linux_cpp,
    _mutex_linux_cpp,
    _os_linux_cpp,
    _osThread_linux_cpp,
    _vmError_linux_cpp,

    _gctrap_linux_x86_cpp,
    _os_linux_x86_cpp,
    _thread_linux_x86_cpp
  };

  static final Q HOTSPOT_AZLINUX_CPPS[] = {
  };
  static final Q HOTSPOT_AZPROXIED_CPPS[] = {
  };

  // --- Build all the P's from all the CPP's, per build directory

  static final String INCLUDES_AZLINUX_X86_CORE   = INCLUDES + 
    " -I " + CPU_X86 + " -I " + OS_LINUX + INCLUDES_AZLINUX + " -I " + OS_CPU_LINUX_X86;
  static { QP.build_P_deps(OBJDIR_AZLINUX_X86_CORE6,GCC_X86+" -M -o %dst %src0 "+INCLUDES_AZLINUX_X86_CORE+AZLINUX_X86_FLAGS_CORE+DEBUG_FLAGS); }

  static final String INCLUDES_AZPROXIED_X86_CORE = INCLUDES + 
    " -I " + CPU_X86 + " -I " + OS_LINUX + INCLUDES_AZPROXIED + " -I " + OS_CPU_LINUX_X86;
  static { QP.build_P_deps(OBJDIR_AZPROXIED_X86_CORE6,GCC_X86+" -M -o %dst %src0 "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+DEBUG_FLAGS); }

  static final String INCLUDES_AZLINUX_X86_JIT    = INCLUDES + 
    " -I " + CPU_X86 + " -I " + OS_LINUX + INCLUDES_AZLINUX + " -I " + OS_CPU_LINUX_X86;
  static { QP.build_P_deps(OBJDIR_AZLINUX_X86_JIT6,GCC_X86+" -M -o %dst %src0 "+INCLUDES_AZLINUX_X86_JIT+AZLINUX_X86_FLAGS_JIT+DEBUG_FLAGS); }

  static final String INCLUDES_AZPROXIED_X86_JIT  = INCLUDES + 
    " -I " + CPU_X86 + " -I " + OS_LINUX + INCLUDES_AZPROXIED + " -I " + OS_CPU_LINUX_X86;
  static { QP.build_P_deps(OBJDIR_AZPROXIED_X86_JIT6,GCC_X86+" -M -o %dst %src0 "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+DEBUG_FLAGS); }

  // Compile & Link the ADLC
  static final Q ADLC_DBG_OBJS [] = QC.gen_all(ADLC_CPPS, OBJDIR_ADLC_JIT6_TARGET,"adlc_g/",GCC_X86+" -c -g  -o %dst -I "+ADLC+" -I "+OPTO+ AZLINUX_X86_FLAGS_JIT+  DEBUG_FLAGS);
  static final Q ADLC_PRD_OBJS [] = QC.gen_all(ADLC_CPPS, OBJDIR_ADLC_JIT6_TARGET,"adlc/"  ,GCC_X86+" -c -O2 -o %dst -I "+ADLC+" -I "+OPTO+ AZLINUX_X86_FLAGS_JIT+PRODUCT_FLAGS);

  static final Q _opcodes_o = Q.FILES.get(OBJDIR_ADLC_JIT6_TARGET+"adlc/opcodes.o");

  static final Q ADLC_DBG = new QL( OBJDIR_ADLC_JIT6_TARGET+"adlc_g/adlc_g",GCC_X86+" -g -o %dst %src",ADLC_DBG_OBJS );
  static final Q ADLC_PRD = new QL( OBJDIR_ADLC_JIT6_TARGET+"adlc/adlc"    ,GCC_X86+" -g -o %dst %src",ADLC_PRD_OBJS );

  static final Q X86_AD = new Qcat( OBJDIR_ADLC_JIT6_TARGET+"adlc/linux_x86.ad", _x86_ad,_linux_x86_64_ad);

  // Some sample file produced by the ADLC.  Running the ADLC produces a slew
  // of files, and we need to run it if any input changes.  But most of the
  // outputs will NOT change, so we'll do a further "did this file change"
  // step.  But we need some single produced file as the target of the ADLC
  // (out of the 10 it makes) so we pick "$cpu.hpp".  We need to run the ADLC
  // if either the input AD files changed OR the ADLC itself changed.
  static final Q ADLC_GX = new Qadlc(OBJDIR_ADLC_JIT6_TARGET+"adlc","/adGlobals_linux_x86.hpp", "./adlc linux_x86.ad -cad_x86_tmp.cpp -had_x86_tmp.hpp -aad_dfa_tmp.cpp", ADLC_PRD, X86_AD);
  static final String ADF = OBJDIR_ADLC_JIT6_TARGET+"adlc/ad_x86";
  static final Q X86_TMP_HPP         = new Q(ADF+"_tmp.hpp");
  static final Q X86_TMP_CPP         = new Q(ADF+"_tmp.cpp"         );
  static final Q X86_TMP_CLONE_CPP   = new Q(ADF+"_tmp_clone.cpp"   );
  static final Q X86_TMP_DFA_CPP     = new Q(OBJDIR_ADLC_JIT6_TARGET+"adlc/ad_dfa_tmp.cpp");
  static final Q X86_TMP_EXPAND_CPP  = new Q(ADF+"_tmp_expand.cpp"  );
  static final Q X86_TMP_FORMAT_CPP  = new Q(ADF+"_tmp_format.cpp"  );
  static final Q X86_TMP_GEN_CPP     = new Q(ADF+"_tmp_gen.cpp"     );
  static final Q X86_TMP_MISC_CPP    = new Q(ADF+"_tmp_misc.cpp"    );
  static final Q X86_TMP_PEEPHOLE_CPP= new Q(ADF+"_tmp_peephole.cpp");
  static final Q X86_TMP_PIPELINE_CPP= new Q(ADF+"_tmp_pipeline.cpp");

  // Now optionally copy each of the ADLC produced tmp files into a happy
  // final file... if it changed.  The goal is to update the time-stamps on
  // the gen'd files ONLY if things changed.  Typically, many files will not
  // change for e.g. small encoding changes in the AD file.  So if x86_tmp.cpp
  // is the same as x86.cpp, then leave x86.cpp alone else copy x86_tmp.cpp
  // into x86.cpp bumping the time stamp at the same time.
  static final Q X86_HPP         = new Qcmp_cp(OBJDIR_ADLC_JIT6_TARGET+"adlc/ad_pd.hpp",X86_TMP_HPP,ADLC_GX);
  static final Q X86_CPP         = new Qcmp_cp(ADF+".cpp"         , X86_TMP_CPP         ,ADLC_GX);
  static final Q X86_CLONE_CPP   = new Qcmp_cp(ADF+"_clone.cpp"   , X86_TMP_CLONE_CPP   ,ADLC_GX);
  static final Q X86_DFA_CPP     = new Qcmp_cp(ADF+"_dfa.cpp"     , X86_TMP_DFA_CPP     ,ADLC_GX);
  static final Q X86_EXPAND_CPP  = new Qcmp_cp(ADF+"_expand.cpp"  , X86_TMP_EXPAND_CPP  ,ADLC_GX);
  static final Q X86_FORMAT_CPP  = new Qcmp_cp(ADF+"_format.cpp"  , X86_TMP_FORMAT_CPP  ,ADLC_GX);
  static final Q X86_GEN_CPP     = new Qcmp_cp(ADF+"_gen.cpp"     , X86_TMP_GEN_CPP     ,ADLC_GX);
  static final Q X86_MISC_CPP    = new Qcmp_cp(ADF+"_misc.cpp"    , X86_TMP_MISC_CPP    ,ADLC_GX);
  static final Q X86_PEEPHOLE_CPP= new Qcmp_cp(ADF+"_peephole.cpp", X86_TMP_PEEPHOLE_CPP,ADLC_GX);
  static final Q X86_PIPELINE_CPP= new Qcmp_cp(ADF+"_pipeline.cpp", X86_TMP_PIPELINE_CPP,ADLC_GX);
  static final Q X86_GLOBALS_HPP = new Qcmp_cp(OBJDIR_ADLC_JIT6_TARGET+"adlc/adGlobals_os_pd.hpp",ADLC_GX,ADLC_GX);

  static void make_ad_deps(String adPP, Q adcpp ) {
    Q  AZLINUX  = new QP( OBJDIR_AZLINUX_X86_JIT6+"incls/"+adPP+".PP",GCC_X86+" -M -o %dst %src0 "+ INCLUDES_AZLINUX_X86_JIT+ AZLINUX_X86_FLAGS_JIT+DEBUG_FLAGS,' ',adcpp);
    Q AZPROXIED = new QP(OBJDIR_AZPROXIED_X86_JIT6+"incls/"+adPP+".PP",GCC_X86+" -M -o %dst %src0 "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+DEBUG_FLAGS,' ',adcpp);
  }
  static { 
    make_ad_deps("ad_x86"         ,X86_CPP         );
    make_ad_deps("ad_x86_clone"   ,X86_CLONE_CPP   );
    make_ad_deps("ad_x86_dfa"     ,X86_DFA_CPP     );
    make_ad_deps("ad_x86_expand"  ,X86_EXPAND_CPP  );
    make_ad_deps("ad_x86_format"  ,X86_FORMAT_CPP  );
    make_ad_deps("ad_x86_gen"     ,X86_GEN_CPP     );
    make_ad_deps("ad_x86_misc"    ,X86_MISC_CPP    );
    make_ad_deps("ad_x86_peephole",X86_PEEPHOLE_CPP);
    make_ad_deps("ad_x86_pipeline",X86_PIPELINE_CPP);
  }

  static final Q HOTSPOT_X86_JIT_CPPS[] = {
    X86_CPP,
    X86_CLONE_CPP,
    X86_DFA_CPP,
    X86_EXPAND_CPP,
    X86_FORMAT_CPP,
    X86_GEN_CPP,
    X86_MISC_CPP,
    X86_PEEPHOLE_CPP,
    X86_PIPELINE_CPP,
    _c1_CodeStubs_x86_cpp,
    _c1_FrameMap_x86_cpp,
    _c1_LinearScan_x86_cpp,
    _c1_LIRAssembler_x86_cpp,
    _c1_LIRGenerator_x86_cpp,
    _c1_MacroAssembler_x86_cpp,
    _c1_Runtime1_x86_cpp,
  };


  static Q[] append( Q[] ary, Q ... more ) {
    Q[] qs = Arrays.copyOf(ary, ary.length+more.length);
    System.arraycopy(more,0,qs,ary.length,more.length); 
    return qs;
  }

  // ------------------
  // --- Building the AVM from libjvm.a
  static final Q XLinker_whole    = new QLinkerOption("-Xlinker --whole-archive");
  static final Q XLinker_no_whole = new QLinkerOption("-Xlinker --no-whole-archive");

  static final Q _libawt_dbg           = new Q(SANDBOX_J2SE_LIB_DIR + "libawt_g.a"   );
  static final Q _libawt_opt           = new Q(SANDBOX_J2SE_LIB_DIR + "libawt.a"   );
  static final Q _libcmm_dbg           = new Q(SANDBOX_J2SE_LIB_DIR + "libcmm_g.a"   );
  static final Q _libcmm_opt           = new Q(SANDBOX_J2SE_LIB_DIR + "libcmm.a"   );
  static final Q _libdcpr_dbg          = new Q(SANDBOX_J2SE_LIB_DIR + "libdcpr_g.a"  );
  static final Q _libdcpr_opt          = new Q(SANDBOX_J2SE_LIB_DIR + "libdcpr.a"  );
  static final Q _libdt_socket_dbg     = new Q(SANDBOX_J2SE_LIB_DIR + "libdt_socket_g.a");
  static final Q _libdt_socket_opt     = new Q(SANDBOX_J2SE_LIB_DIR + "libdt_socket.a");
  static final Q _libfdlibm_dbg        = new Q(SANDBOX_J2SE_LIB_DIR + "libfdlibm_g.a");
  static final Q _libfdlibm_opt        = new Q(SANDBOX_J2SE_LIB_DIR + "libfdlibm.a");
  static final Q _libfontmanager_dbg   = new Q(SANDBOX_J2SE_LIB_DIR + "libfontmanager_g.a");
  static final Q _libfontmanager_opt   = new Q(SANDBOX_J2SE_LIB_DIR + "libfontmanager.a");
  static final Q _libhprof_dbg         = new Q(SANDBOX_J2SE_LIB_DIR + "libhprof_g.a" );
  static final Q _libhprof_opt         = new Q(SANDBOX_J2SE_LIB_DIR + "libhprof.a" );
  static final Q _libinstrument_dbg    = new Q(SANDBOX_J2SE_LIB_DIR + "libinstrument_g.a"  );
  static final Q _libinstrument_opt    = new Q(SANDBOX_J2SE_LIB_DIR + "libinstrument.a"  );
  static final Q _libioser12_dbg       = new Q(SANDBOX_J2SE_LIB_DIR + "libioser12_g.a"    );
  static final Q _libioser12_opt       = new Q(SANDBOX_J2SE_LIB_DIR + "libioser12.a"    );
  static final Q _libjaas_dbg          = new Q(SANDBOX_J2SE_LIB_DIR + "libjaas_" + ((TARGET_OSN.equals("azlinux")) ? "unix" : TARGET_OSN) + "_g.a" );
  static final Q _libjaas_opt          = new Q(SANDBOX_J2SE_LIB_DIR + "libjaas_" + ((TARGET_OSN.equals("azlinux")) ? "unix" : TARGET_OSN) + ".a" );
  static final Q _libjava_crw_demo_dbg = new Q(SANDBOX_J2SE_LIB_DIR + "libjava_crw_demo_g.a");
  static final Q _libjava_crw_demo_opt = new Q(SANDBOX_J2SE_LIB_DIR + "libjava_crw_demo.a");
  static final Q _libjava_dbg          = new Q(SANDBOX_J2SE_LIB_DIR + "libjava_g.a");
  static final Q _libjava_opt          = new Q(SANDBOX_J2SE_LIB_DIR + "libjava.a");
  static final Q _libjdwp_dbg          = new Q(SANDBOX_J2SE_LIB_DIR + "libjdwp_g.a"  );
  static final Q _libjdwp_opt          = new Q(SANDBOX_J2SE_LIB_DIR + "libjdwp.a"  );
  static final Q _libjli_dbg           = new Q(SANDBOX_J2SE_LIB_DIR + ((TARGET_OSN.equals("azlinux")) ? "jli/" : "") + "libjli_g.a"  );
  static final Q _libjli_opt           = new Q(SANDBOX_J2SE_LIB_DIR + ((TARGET_OSN.equals("azlinux")) ? "jli/" : "") + "libjli.a"  );
  static final Q _libjpeg_dbg          = new Q(SANDBOX_J2SE_LIB_DIR + "libjpeg_g.a"  );
  static final Q _libjpeg_opt          = new Q(SANDBOX_J2SE_LIB_DIR + "libjpeg.a"  );
  static final Q _libjsound_dbg        = new Q(SANDBOX_J2SE_LIB_DIR + "libjsound_g.a");
  static final Q _libjsound_opt        = new Q(SANDBOX_J2SE_LIB_DIR + "libjsound.a");
  static final Q _libmanagement_dbg    = new Q(SANDBOX_J2SE_LIB_DIR + "libmanagement_g.a"  );
  static final Q _libmanagement_opt    = new Q(SANDBOX_J2SE_LIB_DIR + "libmanagement.a"  );
  static final Q _libmlib_image_dbg    = new Q(SANDBOX_J2SE_LIB_DIR + "libmlib_image_g.a"  );
  static final Q _libmlib_image_opt    = new Q(SANDBOX_J2SE_LIB_DIR + "libmlib_image.a"  );
  static final Q _libnet_dbg           = new Q(SANDBOX_J2SE_LIB_DIR + "libnet_g.a"   );
  static final Q _libnet_opt           = new Q(SANDBOX_J2SE_LIB_DIR + "libnet.a"   );
  static final Q _libnio_dbg           = new Q(SANDBOX_J2SE_LIB_DIR + "libnio_g.a"   );
  static final Q _libnio_opt           = new Q(SANDBOX_J2SE_LIB_DIR + "libnio.a"   );
  static final Q _libnpt_dbg           = new Q(SANDBOX_J2SE_LIB_DIR + "libnpt_g.a" );
  static final Q _libnpt_opt           = new Q(SANDBOX_J2SE_LIB_DIR + "libnpt.a" );
  static final Q _librmi_dbg           = new Q(SANDBOX_J2SE_LIB_DIR + "librmi_g.a"   );
  static final Q _librmi_opt           = new Q(SANDBOX_J2SE_LIB_DIR + "librmi.a"   );
  static final Q _libsecurity_dbg      = new Q(SANDBOX_J2SE_LIB_DIR + "libsecurity_g.a");
  static final Q _libsecurity_opt      = new Q(SANDBOX_J2SE_LIB_DIR + "libsecurity.a");
  static final Q _libunpack_dbg        = new Q(SANDBOX_J2SE_LIB_DIR + "libunpack_g.a"  );
  static final Q _libunpack_opt        = new Q(SANDBOX_J2SE_LIB_DIR + "libunpack.a"  );
  static final Q _libverify_dbg        = new Q(SANDBOX_J2SE_LIB_DIR + "libverify_g.a");
  static final Q _libverify_opt        = new Q(SANDBOX_J2SE_LIB_DIR + "libverify.a");
  static final Q _libzip_dbg           = new Q(SANDBOX_J2SE_LIB_DIR + "libzip_g.a"   );
  static final Q _libzip_opt           = new Q(SANDBOX_J2SE_LIB_DIR + "libzip.a"   );
  static final Q _libmawt_dbg          = new Q(SANDBOX_J2SE_LIB_DIR + ((TARGET_OSN.equals("azproxied")) ? "headless" : "motif21") + "/libmawt_g.a");
  static final Q _libmawt_opt          = new Q(SANDBOX_J2SE_LIB_DIR + ((TARGET_OSN.equals("azproxied")) ? "headless" : "motif21") + "/libmawt.a");
  static final Q _libavm_dbg           = new Q(SANDBOX_LIB_DIR + "1.6/libavm_g.a");
  static final Q _libavm_opt           = new Q(SANDBOX_LIB_DIR + "1.6/libavm.a");
  static final Q _libazti_dbg          = new Q(SANDBOX_LIB_DIR + "1.6/libazti_g.a" );
  static final Q _libazti_opt          = new Q(SANDBOX_LIB_DIR + "1.6/libazti.a" );
  static final Q _libazpr_dbg          = new Q(SANDBOX_LIB_DIR + "libazpr_g.a"  );
  static final Q _libazpr_opt          = new Q(SANDBOX_LIB_DIR + "libazpr.a"  );
  static final Q _libazprof_dbg        = new Q(SANDBOX_LIB_DIR + "libazprof_g.a");
  static final Q _libazprof_opt        = new Q(SANDBOX_LIB_DIR + "libazprof.a");
  static final Q _libcrypto_dbg        = new Q(SANDBOX_LIB_DIR + "libcrypto_g.a");
  static final Q _libcrypto_opt        = new Q(SANDBOX_LIB_DIR + "libcrypto.a");
  static final Q _libproxy_dbg         = new Q(SANDBOX_LIB_DIR + "libproxy_g.a" );
  static final Q _libproxy_opt         = new Q(SANDBOX_LIB_DIR + "libproxy.a" );
  static final Q _libregex_dbg         = new Q(SANDBOX_LIB_DIR + "libregex_g.a" );
  static final Q _libregex_opt         = new Q(SANDBOX_LIB_DIR + "libregex.a" );
  static final Q _librpc_dbg           = new Q(SANDBOX_LIB_DIR + "librpc_g.a"   );
  static final Q _librpc_opt           = new Q(SANDBOX_LIB_DIR + "librpc.a"   );
  static final Q _libssl_dbg           = new Q(SANDBOX_LIB_DIR + "libssl_g.a"   );
  static final Q _libssl_opt           = new Q(SANDBOX_LIB_DIR + "libssl.a"   );
  static final Q _libtrace_dbg         = new Q(SANDBOX_LIB_DIR + "libtrace_g.a"   );
  static final Q _libtrace_opt         = new Q(SANDBOX_LIB_DIR + "libtrace.a"   );  
  static final Q _libtransport_dbg     = new Q(SANDBOX_LIB_DIR + "libtransport_g.a");
  static final Q _libtransport_opt     = new Q(SANDBOX_LIB_DIR + "libtransport.a");

  static final Q _libXm                = new Q(SANDBOX_X11_LIB_DIR + "libXm.a");
  static final Q _libXt                = new Q(SANDBOX_X11_LIB_DIR + "libXt.a");
  static final Q _libXext              = new Q(SANDBOX_X11_LIB_DIR + "libXext.a");
  static final Q _libXtst              = new Q(SANDBOX_X11_LIB_DIR + "libXtst.a");
  static final Q _libXmu               = new Q(SANDBOX_X11_LIB_DIR + "libXmu.a");
  static final Q _libX11               = new Q(SANDBOX_X11_LIB_DIR + "libX11.a");
  static final Q _libSM                = new Q(SANDBOX_X11_LIB_DIR + "libSM.a");
  static final Q _libICE               = new Q(SANDBOX_X11_LIB_DIR + "libICE.a");
  static final Q _libregex_2           = new Q(SANDBOX_X11_LIB_DIR + "libregex.a");
  static final Q _libfontconfig        = new Q(SANDBOX_X11_LIB_DIR + "libfontconfig.a");
  static final Q _libexpat             = new Q(SANDBOX_X11_LIB_DIR + "libexpat.a");
  static final Q _libfreetype          = new Q(SANDBOX_X11_LIB_DIR + "libfreetype.a");

  static final Q _libfdc_dbg           = new Q(SANDBOX_LIB_DIR + "libfdc_g.a");
  static final Q _libfdc_opt           = new Q(SANDBOX_LIB_DIR + "libfdc.a");
  static final Q _libos_dbg            = new Q(SANDBOX_LIB_DIR + "libos_g.a" );
  static final Q _libos_opt            = new Q(SANDBOX_LIB_DIR + "libos.a" );
  static final Q _libc                 = new Q(SANDBOX_LIB_DIR + "libc.a"  );

  // ---------------------
  // --- Linux X86

  static final Q _stubs_linux_cpp = new Q("src/os/linux/launcher/stubilicious.cpp");
  // AZLINUX
  // All .o files from all .cpp files
  static final Q HOTSPOT_DBG_AZLINUX_X86_CORE_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZLINUX_X86_CORE6,    "debug/",GCC_X86+" -c -g     -o %dst "+INCLUDES_AZLINUX_X86_CORE+AZLINUX_X86_FLAGS_CORE+DEBUG_FLAGS, _jvmti_h, _jvmtiEnv_hpp);
  static final Q HOTSPOT_FDG_AZLINUX_X86_CORE_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZLINUX_X86_CORE6,"fastdebug/",GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZLINUX_X86_CORE+AZLINUX_X86_FLAGS_CORE+FASTDEBUG_FLAGS, _jvmti_h, _jvmtiEnv_hpp);
  static final Q HOTSPOT_OPT_AZLINUX_X86_CORE_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZLINUX_X86_CORE6,"optimized/",GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZLINUX_X86_CORE+AZLINUX_X86_FLAGS_CORE+OPTIMIZED_FLAGS, _jvmti_h, _jvmtiEnv_hpp);
  static final Q HOTSPOT_PRD_AZLINUX_X86_CORE_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZLINUX_X86_CORE6,"product/"  ,GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZLINUX_X86_CORE+AZLINUX_X86_FLAGS_CORE+PRODUCT_FLAGS, _jvmti_h, _jvmtiEnv_hpp);

  static final Q HOTSPOT_DBG_AZLINUX_X86_JIT_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_JIT_CPPS,HOTSPOT_X86_JIT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZLINUX_X86_JIT6,"debug/",GCC_X86+" -c -g -o %dst "+INCLUDES_AZLINUX_X86_JIT+AZLINUX_X86_FLAGS_JIT+DEBUG_FLAGS, _jvmti_h, _jvmtiEnv_hpp, X86_GLOBALS_HPP, X86_HPP);
  static final Q HOTSPOT_FDG_AZLINUX_X86_JIT_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_JIT_CPPS,HOTSPOT_X86_JIT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZLINUX_X86_JIT6,"fastdebug/",GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZLINUX_X86_JIT+AZLINUX_X86_FLAGS_JIT+FASTDEBUG_FLAGS, _jvmti_h, _jvmtiEnv_hpp, X86_GLOBALS_HPP, X86_HPP);
  static final Q HOTSPOT_OPT_AZLINUX_X86_JIT_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_JIT_CPPS,HOTSPOT_X86_JIT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZLINUX_X86_JIT6,"optimized/",GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZLINUX_X86_JIT+AZLINUX_X86_FLAGS_JIT+OPTIMIZED_FLAGS, _jvmti_h, _jvmtiEnv_hpp, X86_GLOBALS_HPP, X86_HPP);
  static final Q HOTSPOT_PRD_AZLINUX_X86_JIT_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_JIT_CPPS,HOTSPOT_X86_JIT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZLINUX_X86_JIT6,"product/"  ,GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZLINUX_X86_JIT+AZLINUX_X86_FLAGS_JIT+PRODUCT_FLAGS, _jvmti_h, _jvmtiEnv_hpp, X86_GLOBALS_HPP, X86_HPP);
  
  static final Q _stubs_azlinux_x86_core_o = new QS(OBJDIR_AZLINUX_X86_CORE6+"debug/stubilicious.o",GCC_X86+" -c -g -o %dst "+_stubs_linux_cpp._target+" "+INCLUDES_AZLINUX_X86_CORE+AZLINUX_X86_FLAGS_CORE+DEBUG_FLAGS,_stubs_linux_cpp);
  static final Q _stubs_azlinux_x86_jit_o  = new QS(OBJDIR_AZLINUX_X86_JIT6+"debug/stubilicious.o",GCC_X86+" -c -g -o %dst "+_stubs_linux_cpp._target+" "+INCLUDES_AZLINUX_X86_JIT+AZLINUX_X86_FLAGS_JIT+DEBUG_FLAGS,_stubs_linux_cpp);

  static final Q HOTSPOT_DBG_AZLINUX_X86_CORE_OBJS2[] = append(HOTSPOT_DBG_AZLINUX_X86_CORE_OBJS,_stubs_azlinux_x86_core_o);
  static final Q HOTSPOT_FDG_AZLINUX_X86_CORE_OBJS2[] = append(HOTSPOT_FDG_AZLINUX_X86_CORE_OBJS,_stubs_azlinux_x86_core_o);
  static final Q HOTSPOT_OPT_AZLINUX_X86_CORE_OBJS2[] = append(HOTSPOT_OPT_AZLINUX_X86_CORE_OBJS,_stubs_azlinux_x86_core_o);
  static final Q HOTSPOT_PRD_AZLINUX_X86_CORE_OBJS2[] = append(HOTSPOT_PRD_AZLINUX_X86_CORE_OBJS,_stubs_azlinux_x86_core_o);
  static final Q HOTSPOT_DBG_AZLINUX_X86_JIT_OBJS2[]  = append(HOTSPOT_DBG_AZLINUX_X86_JIT_OBJS ,_stubs_azlinux_x86_jit_o, _opcodes_o);
  static final Q HOTSPOT_FDG_AZLINUX_X86_JIT_OBJS2[]  = append(HOTSPOT_FDG_AZLINUX_X86_JIT_OBJS ,_stubs_azlinux_x86_jit_o, _opcodes_o);
  static final Q HOTSPOT_OPT_AZLINUX_X86_JIT_OBJS2[]  = append(HOTSPOT_OPT_AZLINUX_X86_JIT_OBJS ,_stubs_azlinux_x86_jit_o, _opcodes_o );
  static final Q HOTSPOT_PRD_AZLINUX_X86_JIT_OBJS2[]  = append(HOTSPOT_PRD_AZLINUX_X86_JIT_OBJS ,_stubs_azlinux_x86_jit_o, _opcodes_o );

  // Build libjvm_g.a from all .o files
  static final Q _libjvm_dbg_azlinux_x86_core = new QA(OBJDIR_AZLINUX_X86_CORE6+    "debug/libjvm_g.a", AR_X86 + " -r %dst %src",' ', HOTSPOT_DBG_AZLINUX_X86_CORE_OBJS2);
  static final Q _libjvm_fdg_azlinux_x86_core = new QA(OBJDIR_AZLINUX_X86_CORE6+"fastdebug/libjvm_g.a", AR_X86 + " -r %dst %src",' ', HOTSPOT_FDG_AZLINUX_X86_CORE_OBJS2);
  static final Q _libjvm_opt_azlinux_x86_core = new QA(OBJDIR_AZLINUX_X86_CORE6+"optimized/libjvm.a",   AR_X86 + " -r %dst %src",' ', HOTSPOT_OPT_AZLINUX_X86_CORE_OBJS2);
  static final Q _libjvm_prd_azlinux_x86_core = new QA(OBJDIR_AZLINUX_X86_CORE6+  "product/libjvm.a",   AR_X86 + " -r %dst %src",' ', HOTSPOT_PRD_AZLINUX_X86_CORE_OBJS2);
  static final Q _libjvm_dbg_azlinux_x86_jit  = new QA(OBJDIR_AZLINUX_X86_JIT6 +    "debug/libjvm_g.a", AR_X86 + " -r %dst %src",' ', HOTSPOT_DBG_AZLINUX_X86_JIT_OBJS2);
  static final Q _libjvm_fdg_azlinux_x86_jit  = new QA(OBJDIR_AZLINUX_X86_JIT6 +"fastdebug/libjvm_g.a", AR_X86 + " -r %dst %src",' ', HOTSPOT_FDG_AZLINUX_X86_JIT_OBJS2);
  static final Q _libjvm_opt_azlinux_x86_jit  = new QA(OBJDIR_AZLINUX_X86_JIT6 +"optimized/libjvm.a",   AR_X86 + " -r %dst %src",' ', HOTSPOT_OPT_AZLINUX_X86_JIT_OBJS2);
  static final Q _libjvm_prd_azlinux_x86_jit  = new QA(OBJDIR_AZLINUX_X86_JIT6 +  "product/libjvm.a",   AR_X86 + " -r %dst %src",' ', HOTSPOT_PRD_AZLINUX_X86_JIT_OBJS2);

  // Linux port does not use mawt, all public symbols are in libawt.so
  static String AVX_LIB_DIR = SANDBOX + "/fromavx/fc12/lib";
  static String LIB_DIRS =
      "-L "  + SANDBOX_LIB_DIR +
      " -L " + SANDBOX_LIB_DIR + "/1.6" +
      " -L " + AVX_LIB_DIR;
  // FIXME - Need to add libazti libazprof and libazpr
  static String LDLIBS   = " -lpthread -lrt -ldl " + LIB_DIRS + " -z origin -Wl,-rpath -Wl,$ORIGIN/. -lazsys -laznixnonproxied -lsysmiscnonproxied ";
  static String LDLIBS_G = " -lpthread -lrt -ldl " + LIB_DIRS + " -z origin -Wl,-rpath -Wl,$ORIGIN/. -lazsys_g -laznixnonproxied_g -lsysmiscnonproxied_g ";

  // --- Building the AVM from libjvm.a
  static final String PRE_LINK_CMD = GCC_X86+" -shared -rdynamic " + " -Wl,--verbose | sed -e '/^======/,/^======/!d' -e '/^======/d;s/0\\( + SIZEOF_HEADERS\\)/0x38400000\\1/' > %dst.lds ; ";
  static final String LINK_CMD     = "sh -c \" " + PRE_LINK_CMD + GCC_X86 + " -o %dst %src -shared -rdynamic " + LDLIBS   + " -Wl,-T,%dst.lds ; \"";
  static final String LINK_CMD_G   = "sh -c \" " + PRE_LINK_CMD + GCC_X86 + " -o %dst %src -shared -rdynamic " + LDLIBS_G + " -Wl,-T,%dst.lds ; \"";
  static final Q _avm_dbg_sym_azlinux_x86_core =
    new QL(OBJDIR_AZLINUX_X86_CORE6+"debug/libjvm_g.so",
           LINK_CMD_G,
           ' ',
           XLinker_whole,
           _libjvm_dbg_azlinux_x86_core,     // 1st in the whole-archive section
           XLinker_no_whole
           );
  static final Q _avm_dbg_azlinux_x86_core = QStripSign.make(OBJDIR_AZLINUX_X86_CORE6+"debug/libjvm_g.so.stripped",_avm_dbg_sym_azlinux_x86_core);

  static final Q _avm_fdg_sym_azlinux_x86_core =
    new QL(OBJDIR_AZLINUX_X86_CORE6+"fastdebug/libjvm_g.so",
           LINK_CMD_G,
           ' ',
           XLinker_whole,
           _libjvm_fdg_azlinux_x86_core,     // 1st in the whole-archive section
           XLinker_no_whole
           );
  static final Q _avm_fdg_azlinux_x86_core = QStripSign.make(OBJDIR_AZLINUX_X86_CORE6+"fastdebug/libjvm_g.so.stripped",_avm_fdg_sym_azlinux_x86_core);

  static final Q _avm_opt_sym_azlinux_x86_core =
    new QL(OBJDIR_AZLINUX_X86_CORE6+"optimized/libjvm.so",
           LINK_CMD,
           ' ',
           XLinker_whole,
           _libjvm_opt_azlinux_x86_core,     // 1st in the whole-archive section
           XLinker_no_whole
           );
  static final Q _avm_opt_azlinux_x86_core = QStripSign.make(OBJDIR_AZLINUX_X86_CORE6+"optimized/libjvm.so.stripped",_avm_opt_sym_azlinux_x86_core);

  static final Q _avm_prd_sym_azlinux_x86_core =
    new QL(OBJDIR_AZLINUX_X86_CORE6+"product/libjvm.so",
           LINK_CMD,
           ' ',
           XLinker_whole,
           _libjvm_prd_azlinux_x86_core,     // 1st in the whole-archive section
           XLinker_no_whole
           );
  static final Q _avm_prd_azlinux_x86_core = QStripSign.make(OBJDIR_AZLINUX_X86_CORE6+"product/libjvm.so.stripped",_avm_prd_sym_azlinux_x86_core);

  static final Q _avm_dbg_sym_azlinux_x86_jit =
    new QL(OBJDIR_AZLINUX_X86_JIT6+"debug/libjvm_g.so",
           LINK_CMD_G,
           ' ',
           XLinker_whole,
           _libjvm_dbg_azlinux_x86_jit,     // 1st in the whole-archive section
           XLinker_no_whole
           );
  static final Q _avm_dbg_azlinux_x86_jit = QStripSign.make(OBJDIR_AZLINUX_X86_JIT6+"debug/libjvm_g.so.stripped",_avm_dbg_sym_azlinux_x86_jit);

  static final Q _avm_fdg_sym_azlinux_x86_jit =
    new QL(OBJDIR_AZLINUX_X86_JIT6+"fastdebug/libjvm_g.so",
           LINK_CMD_G,
           ' ',
           XLinker_whole,
           _libjvm_fdg_azlinux_x86_jit,     // 1st in the whole-archive section
           XLinker_no_whole
           );
  static final Q _avm_fdg_azlinux_x86_jit = QStripSign.make(OBJDIR_AZLINUX_X86_JIT6+"fastdebug/libjvm_g.so.stripped",_avm_fdg_sym_azlinux_x86_jit);

  static final Q _avm_opt_sym_azlinux_x86_jit =
    new QL(OBJDIR_AZLINUX_X86_JIT6+"optimized/libjvm.so",
           LINK_CMD,
           ' ',
           XLinker_whole,
           _libjvm_opt_azlinux_x86_jit,     // 1st in the whole-archive section
           XLinker_no_whole
           );
  static final Q _avm_opt_azlinux_x86_jit = QStripSign.make(OBJDIR_AZLINUX_X86_JIT6+"optimized/libjvm.so.stripped",_avm_opt_sym_azlinux_x86_jit);

  static final Q _avm_prd_sym_azlinux_x86_jit =
    new QL(OBJDIR_AZLINUX_X86_JIT6+"product/libjvm.so",
           LINK_CMD,
           ' ',
           XLinker_whole,
           _libjvm_prd_azlinux_x86_jit,     // 1st in the whole-archive section
           XLinker_no_whole
           );
  static final Q _avm_prd_azlinux_x86_jit = QStripSign.make(OBJDIR_AZLINUX_X86_JIT6+"product/libjvm.so.stripped",_avm_prd_sym_azlinux_x86_jit);

  // AZPROXIED
  // All .o files from all .cpp files
  static final Q HOTSPOT_DBG_AZPROXIED_X86_CORE_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZPROXIED_X86_CORE6,    "debug/",GCC_X86+" -c -g     -o %dst "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+DEBUG_FLAGS, _jvmti_h, _jvmtiEnv_hpp);
  static final Q HOTSPOT_FDG_AZPROXIED_X86_CORE_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZPROXIED_X86_CORE6,"fastdebug/",GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+FASTDEBUG_FLAGS, _jvmti_h, _jvmtiEnv_hpp);
  static final Q HOTSPOT_OPT_AZPROXIED_X86_CORE_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZPROXIED_X86_CORE6,"optimized/",GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+OPTIMIZED_FLAGS, _jvmti_h, _jvmtiEnv_hpp);
  static final Q HOTSPOT_PRD_AZPROXIED_X86_CORE_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZPROXIED_X86_CORE6,"product/"  ,GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+PRODUCT_FLAGS, _jvmti_h, _jvmtiEnv_hpp);

  static final Q HOTSPOT_DBG_AZPROXIED_X86_JIT_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_JIT_CPPS,HOTSPOT_X86_JIT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZPROXIED_X86_JIT6,"debug/",GCC_X86+" -c -g -o %dst "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+DEBUG_FLAGS, _jvmti_h, _jvmtiEnv_hpp, X86_GLOBALS_HPP, X86_HPP);
  static final Q HOTSPOT_FDG_AZPROXIED_X86_JIT_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_JIT_CPPS,HOTSPOT_X86_JIT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZPROXIED_X86_JIT6,"fastdebug/",GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+FASTDEBUG_FLAGS, _jvmti_h, _jvmtiEnv_hpp, X86_GLOBALS_HPP, X86_HPP);
  static final Q HOTSPOT_OPT_AZPROXIED_X86_JIT_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_JIT_CPPS,HOTSPOT_X86_JIT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZPROXIED_X86_JIT6,"optimized/",GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+OPTIMIZED_FLAGS, _jvmti_h, _jvmtiEnv_hpp, X86_GLOBALS_HPP, X86_HPP);
  static final Q HOTSPOT_PRD_AZPROXIED_X86_JIT_OBJS[] = QC.gen_all(flatten_to_Q(HOTSPOT_CPPS,HOTSPOT_JIT_CPPS,HOTSPOT_X86_JIT_CPPS,HOTSPOT_X86_CPPS,HOTSPOT_LINUX_CPPS),OBJDIR_AZPROXIED_X86_JIT6,"product/"  ,GCC_X86+" -c -g -O3 -fno-omit-frame-pointer -o %dst "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+PRODUCT_FLAGS, _jvmti_h, _jvmtiEnv_hpp, X86_GLOBALS_HPP, X86_HPP);
  
  // --- Horrible porting layer
  static final Q _stubs_azproxied_x86_core_o = new QS(OBJDIR_AZPROXIED_X86_CORE6+"debug/stubilicious.o",GCC_X86+" -c -g -o %dst "+_stubs_linux_cpp._target+" "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+DEBUG_FLAGS,_stubs_linux_cpp);
  static final Q _stubs_azproxied_x86_jit_o  = new QS(OBJDIR_AZPROXIED_X86_JIT6+"debug/stubilicious.o",GCC_X86+" -c -g -o %dst "+_stubs_linux_cpp._target+" "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+DEBUG_FLAGS,_stubs_linux_cpp);

  static final Q HOTSPOT_DBG_AZPROXIED_X86_CORE_OBJS2[] = append(HOTSPOT_DBG_AZPROXIED_X86_CORE_OBJS,_stubs_azproxied_x86_core_o);
  static final Q HOTSPOT_FDG_AZPROXIED_X86_CORE_OBJS2[] = append(HOTSPOT_FDG_AZPROXIED_X86_CORE_OBJS,_stubs_azproxied_x86_core_o);
  static final Q HOTSPOT_OPT_AZPROXIED_X86_CORE_OBJS2[] = append(HOTSPOT_OPT_AZPROXIED_X86_CORE_OBJS,_stubs_azproxied_x86_core_o);
  static final Q HOTSPOT_PRD_AZPROXIED_X86_CORE_OBJS2[] = append(HOTSPOT_PRD_AZPROXIED_X86_CORE_OBJS,_stubs_azproxied_x86_core_o);
  static final Q HOTSPOT_DBG_AZPROXIED_X86_JIT_OBJS2 [] = append(HOTSPOT_DBG_AZPROXIED_X86_JIT_OBJS ,_stubs_azproxied_x86_jit_o , _opcodes_o);
  static final Q HOTSPOT_FDG_AZPROXIED_X86_JIT_OBJS2 [] = append(HOTSPOT_FDG_AZPROXIED_X86_JIT_OBJS ,_stubs_azproxied_x86_jit_o , _opcodes_o);
  static final Q HOTSPOT_OPT_AZPROXIED_X86_JIT_OBJS2 [] = append(HOTSPOT_OPT_AZPROXIED_X86_JIT_OBJS ,_stubs_azproxied_x86_jit_o , _opcodes_o);
  static final Q HOTSPOT_PRD_AZPROXIED_X86_JIT_OBJS2 [] = append(HOTSPOT_PRD_AZPROXIED_X86_JIT_OBJS ,_stubs_azproxied_x86_jit_o , _opcodes_o);

  // Build libjvm_g.a from all .o files
  static final Q _libjvm_dbg_azproxied_x86_core = new QA(OBJDIR_AZPROXIED_X86_CORE6+    "debug/libjvm_g.a", AR_X86 + " -scrf %dst %src",' ', HOTSPOT_DBG_AZPROXIED_X86_CORE_OBJS2);
  static final Q _libjvm_fdg_azproxied_x86_core = new QA(OBJDIR_AZPROXIED_X86_CORE6+"fastdebug/libjvm_g.a", AR_X86 + " -scrf %dst %src",' ', HOTSPOT_FDG_AZPROXIED_X86_CORE_OBJS2);
  static final Q _libjvm_opt_azproxied_x86_core = new QA(OBJDIR_AZPROXIED_X86_CORE6+"optimized/libjvm.a",   AR_X86 + " -scrf %dst %src",' ', HOTSPOT_OPT_AZPROXIED_X86_CORE_OBJS2);
  static final Q _libjvm_prd_azproxied_x86_core = new QA(OBJDIR_AZPROXIED_X86_CORE6+"product/libjvm.a",     AR_X86 + " -scrf %dst %src",' ', HOTSPOT_PRD_AZPROXIED_X86_CORE_OBJS2);
  static final Q _libjvm_dbg_azproxied_x86_jit  = new QA(OBJDIR_AZPROXIED_X86_JIT6 +    "debug/libjvm_g.a", AR_X86 + " -scrf %dst %src",' ', HOTSPOT_DBG_AZPROXIED_X86_JIT_OBJS2);
  static final Q _libjvm_fdg_azproxied_x86_jit  = new QA(OBJDIR_AZPROXIED_X86_JIT6+"fastdebug/libjvm_g.a", AR_X86 + " -scrf %dst %src",' ', HOTSPOT_FDG_AZPROXIED_X86_JIT_OBJS2);
  static final Q _libjvm_opt_azproxied_x86_jit  = new QA(OBJDIR_AZPROXIED_X86_JIT6+"optimized/libjvm.a",   AR_X86 + " -scrf %dst %src",' ', HOTSPOT_OPT_AZPROXIED_X86_JIT_OBJS2);
  static final Q _libjvm_prd_azproxied_x86_jit  = new QA(OBJDIR_AZPROXIED_X86_JIT6+"product/libjvm.a",     AR_X86 + " -scrf %dst %src",' ', HOTSPOT_PRD_AZPROXIED_X86_JIT_OBJS2);
  // Some things not in the libjvm.a
  static final Q _main_azproxied_cpp  = new Q("avm/src/main.cpp");
  static final Q _main_azproxied_core_P = new QP(OBJDIR_AZPROXIED_X86_CORE6+"incls/main.PP",GCC_X86+" -M -o %dst %src0 "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+DEBUG_FLAGS,' ',_main_azproxied_cpp,_jvmti_h);
  static final Q _main_azproxied_jit_P = new QP(OBJDIR_AZPROXIED_X86_JIT6+"incls/main.PP",GCC_X86+" -M -o %dst %src0 "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+DEBUG_FLAGS,' ',_main_azproxied_cpp,_jvmti_h);
  static final Q _main_azproxied_x86_core_o = QC.gen(OBJDIR_AZPROXIED_X86_CORE6,"debug/",GCC_X86+" -c -g -o %dst "+_main_azproxied_cpp._target+" "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+DEBUG_FLAGS,"main",".PP");
  static final Q _proxy_jdk_azproxied_x86_core_o = new QS(OBJDIR_AZPROXIED_X86_CORE6+"debug/proxy_jdk_version.o",GCC_X86+" -c -g -o %dst "+_proxy_jdk_version_c._target+" "+INCLUDES_AZPROXIED_X86_CORE+AZPROXIED_X86_FLAGS_CORE+DEBUG_FLAGS,_proxy_jdk_version_c);
  static final Q _main_azproxied_x86_jit_o = QC.gen(OBJDIR_AZPROXIED_X86_JIT6,"debug/",GCC_X86+" -c -g -o %dst "+_main_azproxied_cpp._target+" "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+DEBUG_FLAGS,"main",".PP");
  static final Q _proxy_jdk_azproxied_x86_jit_o = new QS(OBJDIR_AZPROXIED_X86_JIT6+"debug/proxy_jdk_version.o",GCC_X86+" -c -g -o %dst "+_proxy_jdk_version_c._target+" "+INCLUDES_AZPROXIED_X86_JIT+AZPROXIED_X86_FLAGS_JIT+DEBUG_FLAGS,_proxy_jdk_version_c);

  static final Q[] _avm_dbg_azproxied_libs = {
    _libawt_dbg, _libcmm_dbg, _libdcpr_dbg, _libdt_socket_dbg,
    _libhprof_dbg, _libinstrument_dbg, _libioser12_dbg,
    _libjaas_dbg, _libjava_crw_demo_dbg, _libjava_dbg, _libjdwp_dbg, _libjli_dbg,
    _libjpeg_dbg, _libjsound_dbg, _libmanagement_dbg, _libmlib_image_dbg, _libnet_dbg,
    _libnio_dbg, _libnpt_dbg, _librmi_dbg, _libsecurity_dbg, _libunpack_dbg,
    _libverify_dbg, _libzip_dbg, _libmawt_dbg,
    _libazpr_dbg, _libazprof_dbg, _libazti_dbg, _libcrypto_dbg,
    _libproxy_dbg, _librpc_dbg, _libssl_dbg, _libtrace_dbg, _libtransport_dbg };

  static final Q[] _avm_opt_azproxied_libs = {
    _libawt_opt, _libcmm_opt, _libdcpr_opt, _libdt_socket_opt,
    _libhprof_opt, _libinstrument_opt, _libioser12_opt,
    _libjaas_opt, _libjava_crw_demo_opt, _libjava_opt, _libjdwp_opt, _libjli_opt,
    _libjpeg_opt, _libjsound_opt, _libmanagement_opt, _libmlib_image_opt, _libnet_opt,
    _libnio_opt, _libnpt_opt, _librmi_opt, _libsecurity_opt, _libunpack_opt,
    _libverify_opt, _libzip_opt, _libmawt_opt,
    _libazpr_opt, _libazprof_opt, _libazti_opt, _libcrypto_opt,
    _libproxy_opt, _librpc_opt, _libssl_opt, _libtrace_opt, _libtransport_opt };

  // --- Building the AVM from libjvm.a
  static final Q _avm_dbg_sym_azproxied_x86_core =
    new QL(OBJDIR_AZPROXIED_X86_CORE6+"debug/avm_debug.sym",
           GCC_X86+" -o %dst %src -rdynamic -lpthread -lrt -ldl",
           ' ', _main_azproxied_x86_core_o,_proxy_jdk_azproxied_x86_core_o,
           XLinker_whole,
           _libjvm_dbg_azproxied_x86_core,     // 1st in the whole-archive section
           _avm_dbg_azproxied_libs,

           XLinker_no_whole,
           _libfdlibm_dbg,
           _libfdc_dbg,
           _libfontmanager_dbg,
           _libos_dbg,
           _libregex_dbg
           );
  static final Q _avm_dbg_azproxied_x86_core = QStripSign.make(OBJDIR_AZPROXIED_X86_CORE6+"debug/avm_debug",_avm_dbg_sym_azproxied_x86_core);

  static final Q _avm_fdg_sym_azproxied_x86_core =
    new QL(OBJDIR_AZPROXIED_X86_CORE6+"fastdebug/avm_fastdebug.sym",
           GCC_X86+" -o %dst %src -rdynamic -lpthread -lrt -ldl",
           ' ', _main_azproxied_x86_core_o,_proxy_jdk_azproxied_x86_core_o,
           XLinker_whole,
           _libjvm_fdg_azproxied_x86_core,     // 1st in the whole-archive section
           _avm_dbg_azproxied_libs,

           XLinker_no_whole,
           _libfdlibm_dbg,
           _libfdc_dbg,
           _libfontmanager_dbg,
           _libos_dbg,
           _libregex_dbg
           );
  static final Q _avm_fdg_azproxied_x86_core = QStripSign.make(OBJDIR_AZPROXIED_X86_CORE6+"fastdebug/avm_fastdebug",_avm_fdg_sym_azproxied_x86_core);

  static final Q _avm_opt_sym_azproxied_x86_core =
    new QL(OBJDIR_AZPROXIED_X86_CORE6+"optimized/avm_optimized.sym",
           GCC_X86+" -o %dst %src -rdynamic -lpthread -lrt -ldl",
           ' ', _main_azproxied_x86_core_o,_proxy_jdk_azproxied_x86_core_o,
           XLinker_whole,
           _libjvm_opt_azproxied_x86_core,     // 1st in the whole-archive section
           _avm_opt_azproxied_libs,

           XLinker_no_whole,
           _libfdlibm_opt,
           _libfdc_opt,
           _libfontmanager_opt,
           _libos_opt,
           _libregex_opt
           );
  static final Q _avm_opt_azproxied_x86_core = QStripSign.make(OBJDIR_AZPROXIED_X86_CORE6+"optimized/avm_optimized",_avm_opt_sym_azproxied_x86_core);

  static final Q _avm_prd_sym_azproxied_x86_core =
    new QL(OBJDIR_AZPROXIED_X86_CORE6+"product/avm_product.sym",
           GCC_X86+" -o %dst %src -rdynamic -lpthread -lrt -ldl",
           ' ', _main_azproxied_x86_core_o,_proxy_jdk_azproxied_x86_core_o,
           XLinker_whole,
           _libjvm_prd_azproxied_x86_core,     // 1st in the whole-archive section
           _avm_opt_azproxied_libs,

           XLinker_no_whole,
           _libfdlibm_opt,
           _libfdc_opt,
           _libfontmanager_opt,
           _libos_opt,
           _libregex_opt
           );
  static final Q _avm_prd_azproxied_x86_core = QStripSign.make(OBJDIR_AZPROXIED_X86_CORE6+"product/avm_product",_avm_prd_sym_azproxied_x86_core);

  static final Q _avm_dbg_sym_azproxied_x86_jit =
    new QL(OBJDIR_AZPROXIED_X86_JIT6+"debug/avm_debug.sym",
           GCC_X86+" -o %dst %src -rdynamic -lpthread -lrt -ldl",
           ' ', _main_azproxied_x86_jit_o,_proxy_jdk_azproxied_x86_jit_o,
           XLinker_whole,
           _libjvm_dbg_azproxied_x86_jit,     // 1st in the whole-archive section
           _avm_dbg_azproxied_libs,

           XLinker_no_whole,
           _libfdlibm_dbg,
           _libfdc_dbg,
           _libfontmanager_dbg,
           _libos_dbg,
           _libregex_dbg
           );
  static final Q _avm_dbg_azproxied_x86_jit = QStripSign.make(OBJDIR_AZPROXIED_X86_JIT6+"debug/avm_debug",_avm_dbg_sym_azproxied_x86_jit);

  static final Q _avm_fdg_sym_azproxied_x86_jit =
    new QL(OBJDIR_AZPROXIED_X86_JIT6+"fastdebug/avm_fastdebug.sym",
           GCC_X86+" -o %dst %src -rdynamic -lpthread -lrt -ldl",
           ' ', _main_azproxied_x86_jit_o,_proxy_jdk_azproxied_x86_jit_o,
           XLinker_whole,
           _libjvm_fdg_azproxied_x86_jit,     // 1st in the whole-archive section
           _avm_dbg_azproxied_libs,

           XLinker_no_whole,
           _libfdlibm_dbg,
           _libfdc_dbg,
           _libfontmanager_dbg,
           _libos_dbg,
           _libregex_dbg
           );
  static final Q _avm_fdg_azproxied_x86_jit = QStripSign.make(OBJDIR_AZPROXIED_X86_JIT6+"fastdebug/avm_fastdebug",_avm_fdg_sym_azproxied_x86_jit);

  static final Q _avm_opt_sym_azproxied_x86_jit =
    new QL(OBJDIR_AZPROXIED_X86_JIT6+"optimized/avm_optimized.sym",
           GCC_X86+" -o %dst %src -rdynamic -lpthread -lrt -ldl",
           ' ', _main_azproxied_x86_jit_o,_proxy_jdk_azproxied_x86_jit_o,
           XLinker_whole,
           _libjvm_opt_azproxied_x86_jit,     // 1st in the whole-archive section
           _avm_opt_azproxied_libs,

           XLinker_no_whole,
           _libfdlibm_opt,
           _libfdc_opt,
           _libfontmanager_opt,
           _libos_opt,
           _libregex_opt
           );
  static final Q _avm_opt_azproxied_x86_jit = QStripSign.make(OBJDIR_AZPROXIED_X86_JIT6+"optimized/avm_optimized",_avm_opt_sym_azproxied_x86_jit);

  static final Q _avm_prd_sym_azproxied_x86_jit =
    new QL(OBJDIR_AZPROXIED_X86_JIT6+"product/avm_product.sym",
           GCC_X86+" -o %dst %src -rdynamic -lpthread -lrt -ldl",
           ' ', _main_azproxied_x86_jit_o,_proxy_jdk_azproxied_x86_jit_o,
           XLinker_whole,
           _libjvm_prd_azproxied_x86_jit,     // 1st in the whole-archive section
           _avm_opt_azproxied_libs,

           XLinker_no_whole,
           _libfdlibm_opt,
           _libfdc_opt,
           _libfontmanager_opt,
           _libos_opt,
           _libregex_opt
           );
  static final Q _avm_prd_azproxied_x86_jit = QStripSign.make(OBJDIR_AZPROXIED_X86_JIT6+"product/avm_product",_avm_prd_sym_azproxied_x86_jit);


}
