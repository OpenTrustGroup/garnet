# Lua 5.3.3

This is a fork of Lua 5.3.3, released on 30 May 2016. This project makes the
smallest number of modifications to support running Lua as part of the
[Fuchsia](https://fuchsia.googlesource.com/) repository.

For installation instructions, license details, and
further information about Lua, see doc/readme.html.

Please refer to the [Lua website](http://www.lua.org/about.html) for information
about the Lua project.

# Support status

The following is a list of builtin library functions that don't work due to
missing features in Fuchsia (e.g. unimplemented libc functions):

## io

  * **io.popen**: Returns not supported
  * **io.tmpfile**: Crashes with due to unsupported syscall (tmpfile)
  * **io.read**: Reading from stdin doesn't block. Reading from regular files is OK.

## os

  * **os.time**: Returns the same thing as os.clock, which is incorrect
  * **os.date**: Unsupported syscall: localtime, gmtime, localtime_r, gmtime_r (#257)
  * **os.execute**: Unsupported syscall: sigaction (#13)
  * **os.getenv**: Unsupported syscall: sigaction (#13)
  * **os.rename**: Returns "nil     Function not Implemented38"
  * **os.tmpname**: Unsupported syscall: tmpnam (#262)

## package

  * **package.cpath**: Need to configure this away from the default cpath on
    Fuchsia?
  * **package.loadlib**: Returns *"Dynamic libraries not enabled"*, which is
    probably what we want right now.
  * **package.path**: Need to map this to something meaningful.
