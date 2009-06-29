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

dnl
dnl Set compiler warning flags using gnulib's warnings module
dnl
AC_DEFUN([NETCF_COMPILE_WARNINGS],[
    dnl ******************************
    dnl More compiler warnings
    dnl ******************************

    AC_ARG_ENABLE(compile-warnings,
        AC_HELP_STRING([--enable-compile-warnings=@<:@no/yes/error@:>@],
                  [Turn on compiler warnings]),,
                  [enable_compile_warnings="m4_default([$1],[yes])"])

    case "x$enable_compile_warnings" in
    xyes | xno | xerror)
    ;;
    *)
    AC_MSG_ERROR(Unknown argument '$enable_compile_warnings' to --enable-compile-warnings)
    ;;
    esac

    if test "x$enable_compile_warnings" != "xno"; then
        gl_WARN_ADD([-Wall])
        gl_WARN_ADD([-Wformat])
        gl_WARN_ADD([-Wformat-security])
        gl_WARN_ADD([-Wmissing-prototypes])
        gl_WARN_ADD([-Wnested-externs])
        gl_WARN_ADD([-Wpointer-arith])
        gl_WARN_ADD([-Wextra])
        gl_WARN_ADD([-Wshadow])
        gl_WARN_ADD([-Wcast-align])
        gl_WARN_ADD([-Wwrite-strings])
        gl_WARN_ADD([-Waggregate-return])
        gl_WARN_ADD([-Wstrict-prototypes])
        gl_WARN_ADD([-Winline])
        gl_WARN_ADD([-Wredundant-decls])
        gl_WARN_ADD([-Wno-sign-compare])
    fi

    if test "x$enable_compile_warnings" = "xerror"; then
        gl_WARN_ADD([-Werror])
    fi
])
