AC_INIT([blip], [0.9])
AC_CONFIG_SRCDIR([src/blip.c])

AM_INIT_AUTOMAKE([no-define foreign])

AC_PROG_CC

AC_CHECK_LIB([gme], [gme_open_data], [], [AC_MSG_ERROR([libgme was not found!])])
AC_CHECK_LIB([sndfile], [sf_open_fd], [], [AC_MSG_ERROR([libsndfile was not found!])])

AC_OUTPUT([Makefile src/Makefile])
