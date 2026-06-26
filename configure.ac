# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.69)

AC_INIT([RuLex], [3.9.2])
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

AC_ARG_WITH([bdb],
  [AS_HELP_STRING([--with-bdb], [use Berkeley DB backend instead of LMDB])],
  [use_bdb=$withval], [use_bdb=no])

# Checks for required headers and libraries.
AC_CHECK_HEADERS([stdlib.h unistd.h string.h stdio.h fcntl.h limits.h sys/types.h sys/stat.h], [],
                 [AC_MSG_ERROR([Some system header files are not found])])

AS_IF([test "x$use_bdb" = "xyes"],
  [AX_BERKELEY_DB([5.1],
     [AC_DEFINE([USE_BDB], [1], [Use Berkeley DB backend])],
     [AC_MSG_ERROR([Berkeley DB >= 5.1 not found])])],
  [PKG_CHECK_MODULES([LMDB], [lmdb],
     [],
     [AC_CHECK_HEADERS([lmdb.h], [],
        [AC_MSG_ERROR([LMDB header lmdb.h not found])])
      AC_CHECK_LIB([lmdb], [mdb_env_create], [LMDB_LIBS="-llmdb"],
        [AC_MSG_ERROR([LMDB library not found])])])])

AM_CONDITIONAL([USE_BDB], [test "x$use_bdb" = "xyes"])
CHECK_FOR_LOCALES_SUPPORT

# Output results.
AC_CONFIG_FILES([Makefile src/Makefile data/Makefile manpages/Makefile])
AC_OUTPUT
