# aurbrokenpkgcheck

---

Small and fast tool written in C to check for broken packages that need to be rebuilt on Arch Linux.

 * Redirects potentially broken package names to the standard output. This way it can be piped directly into an [aur helper](https://wiki.archlinux.org/index.php/AUR_helpers)
 * List the issues for specific libraries or binaries on the standard error output


```sh
$ aurbrokenpkgcheck
ardour5
    └── /usr/lib/ardour5/ardour-5.8.0
        └── libardourcp.so: cannot open shared object file: No such file or directory
    └── /usr/lib/ardour5/ardour-vst-scanner
        └── libpbd.so.4: cannot open shared object file: No such file or directory
    └── /usr/lib/ardour5/backends/libalsa_audiobackend.so
        └── libardour.so.3: cannot open shared object file: No such file or directory
    └── /usr/lib/ardour5/backends/libdummy_audiobackend.so
        └── libardour.so.3: cannot open shared object file: No such file or directory
    └── /usr/lib/ardour5/backends/libjack_audiobackend.so
        └── libardour.so.3: cannot open shared object file: No such file or directory
    ...
```

**Why?** Because other tools that do this task were way too slow. So instead of using the ldd script I went straight for the dynamic linker and libalpm to list the files. A call to pacman is still performed in order to get the foreign package list.

## Build

### Requirements :
 * pacman
 * libalpm
 * pkg-config
 * gcc or clang
 * make

```sh
$ make
```

Other build targets are available : 

```
make aurbrokenpkgcheck       : builds the release binary
make aurbrokenpkgcheck_debug : builds the debug binary
make clean                   : cleans the directory
make valgrind                : (needs valgrind installed) runs leak check
make static-analysis         : runs static analysis using scan-build from clang
```

## Options

```sh
$ aurbrokenpkgcheck --help
Usage: aurbrokenpkgcheck [-h|--help] [-b|--dbpath DBPATH] [-r|--root ROOT] [--colors] [--no-colors]
Options:
         -h,--help          : This help
         -b,--dbpath DBPATH : The database location to use (see man 8 pacman)
         -r,--root ROOT     : The installation root to use (see man 8 pacman)
         --colors           : Enable colored output (default)
         --no-colors        : Disable colored output
```

## Future Improvements

 * prettier tree view
 * ignore list, since since a lot of packages handle their libraries on their own so reinstalling them won't change their "broken" state. 

## License

MIT License

Copyright (c) 2017 Philipp Richter
