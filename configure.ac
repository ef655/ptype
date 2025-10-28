#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.72])
AC_INIT([ptype], [0.9], [https://github.com/ef655/ptype/issues],
        [], [https://github.com/ef655/ptype])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([src/drw.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([unistd.h],
				 [], [AC_MSG_ERROR([unistd.h is required but is missing!])])

# Other


real_prefix="$prefix"
if test "$real_prefix" = 'NONE'; then
	real_prefix='/usr/local'
elif test "$real_prefix" = '/'; then
	real_prefix=''
fi

AC_DEFINE_UNQUOTED([SYSCONFDIR], ["${real_prefix}/etc"], [System configuration directory])
AC_SUBST([SYSCONFDIR], ["${real_prefix}/etc"])

ncurses_libs=`ncurses6-config --libs` 2>/dev/null || ncurses_libs='-lncurses'
AC_SUBST([NCURSES_LIBS], ["${ncurses_libs}"])

AC_CONFIG_FILES([Makefile
				 src/Makefile
				 pconf/Makefile
				 man/Makefile
				 man/ptype.6])

AC_OUTPUT
