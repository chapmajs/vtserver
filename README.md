vtserver
========

Copy PDP-11 disk/tape images to physical media and back

About
-----

vtserver was written by Warren Toomey (wkt@cs.adfa.edu.au). This repository starts with Version 2.3, released March 2001.

Building
--------

Requirements:

* POSIX build environment
* sane `make` (GNU Make, BSD Make, et c.)
* sane C compiler (tests with GCC)

To build the server portion of vtserver, change to the vtserver directory and run `make`. Edit `.vtrc` for your system's serial port and the disk images you want to use. Follow `vtreadme.txt` for additional instructions.
