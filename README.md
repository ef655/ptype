# ptype — a customizable ncurses based typing practice program

## Overview

ptype is a typing practice program that runs in the terminal and aims to help
the user accelerate their typing speed (or simply to exert their energy on).

For a comprehensive document on the features and how to configure ptype, see `ptype(6)` after installation.

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
