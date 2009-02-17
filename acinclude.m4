dnl
dnl Determine readline linker flags in a way that works on RHEL 5
dnl
AC_DEFUN([NETCF_CHECK_READLINE], [
  AC_CHECK_HEADERS([readline/readline.h])

  # Check for readline.
  AC_CHECK_LIB(readline, readline,
          [use_readline=yes; READLINE_LIBS=-lreadline],
          [use_readline=no])

  # If the above test failed, it may simply be that -lreadline requires
  # some termcap-related code, e.g., from one of the following libraries.
  # See if adding one of them to LIBS helps.
  if test $use_readline = no; then
      saved_libs=$LIBS
      LIBS=
      AC_SEARCH_LIBS(tgetent, ncurses curses termcap termlib)
      case $LIBS in
        no*) ;;  # handle "no" and "none required"
        *) # anything else is a -lLIBRARY
          # Now, check for -lreadline again, also using $LIBS.
          # Note: this time we use a different function, so that
          # we don't get a cached "no" result.
          AC_CHECK_LIB(readline, rl_initialize,
                  [use_readline=yes
                   READLINE_LIBS="-lreadline $LIBS"],,
                  [$LIBS])
          ;;
      esac
      test $use_readline = no &&
          AC_MSG_WARN([readline library not found])
      LIBS=$saved_libs
  fi

  if test $use_readline = no; then
    AC_MSG_ERROR(Could not find a working readline library (see config.log for details).)
  fi

  AC_SUBST(READLINE_LIBS)
])
