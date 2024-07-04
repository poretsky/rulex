# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.69)

AC_INIT([RuLex], [3.8.5])
AC_CONFIG_SRCDIR([src/rulex.c])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([foreign])
LT_INIT

# Guess environment.
AC_CANONICAL_HOST
AC_CANONICAL_BUILD

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL

# Language choice.
AC_LANG([C])
AX_CHECK_VSCRIPT

# Checks for required headers and libraries.
AC_CHECK_HEADERS([stdlib.h unistd.h string.h stdio.h fcntl.h limits.h sys/types.h sys/stat.h], [],
                 [AC_MSG_ERROR([Some system header files are not found])])
AX_BERKELEY_DB([5.1], [], [AC_MSG_ERROR([Berkeley DB library is mandatory])])
CHECK_FOR_LOCALES_SUPPORT

# Output results.
AC_CONFIG_FILES([Makefile src/Makefile data/Makefile manpages/Makefile])
AC_OUTPUT
