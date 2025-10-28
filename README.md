# ptype — a customizable ncurses based typing practice program

## Overview

ptype is a typing practice program that runs in the terminal and aims to help
the user accelerate their typing speed (or simply to exert their energy on).

For a comprehensive document on the features and how to configure ptype, see `ptype(6)` after installation.

---

![ptype quote mode](/media/ptype_quote.png "ptype quote mode")
![ptype timed mode](/media/ptype_timed.png "ptype timed mode")
![ptype colors](/media/ptype_colors.png "ptype colors")
![ptype history screen](/media/ptype_history.png "ptype history screen")
![ptype options screen](/media/ptype_options.png "ptype options screen")

## Installation

### Dependencies

- ncurses
- POSIX-system

### Building

Autoconf, Automake, a C11 compiler & the aforementioned dependencies must first be installed.

To manually build the `ptype` binary from source:
```
$ git clone https://github.com/ef655/ptype
$ cd ptype
$ aclocal && autoheader && automake --add-missing --foreign && autoconf
$ ./configure && make
# make install
```
