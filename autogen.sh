#!/bin/bash
# Run this to generate all the initial makefiles, etc.

package=netcf

usage() {
  echo >&2 "\
Usage: $0 [OPTION]...
Generate makefiles and other infrastructure needed for building


Options:
 --gnulib-srcdir=DIRNAME  Specify the local directory where gnulib
                          sources reside.  Use this if you already
                          have gnulib sources on your machine, and
                          do not want to waste your bandwidth downloading
                          them again.
 --help                   Print this message
 any other option         Pass to the 'configure' script verbatim

Running without arguments will suffice in most cases.
"
}

BUILD_AUX=build/aux
GNULIB_DIR=gnulib

set -e
srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

THEDIR=`pwd`
cd "$srcdir"

test -f src/netcf.c || {
    echo "You must run this script in the top-level libvirt directory"
    exit 1
}

EXTRA_ARGS=
if test "x$1" = "x--system"; then
    shift
    prefix=/usr
    libdir=$prefix/lib
    sysconfdir=/etc
    localstatedir=/var
    if [ -d /usr/lib64 ]; then
      libdir=$prefix/lib64
    fi
    EXTRA_ARGS="--prefix=$prefix --sysconfdir=$sysconfdir --localstatedir=$localstatedir --libdir=$libdir"
    echo "Running ./configure with $EXTRA_ARGS $@"
else
    if test -z "$*" && test ! -f "$THEDIR/config.status"; then
        echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
    fi
fi

# Split out options for bootstrap and for configure
declare -a CF_ARGS
for option
do
  case $option in
  --help)
    usage
    exit;;
  --gnulib-srcdir=*)
    GNULIB_SRCDIR=$option;;
  *)
    CF_ARGS[${#CF_ARGS[@]}]=$option;;
  esac
done

#Check for OSX
case `uname -s` in
Darwin) LIBTOOLIZE=glibtoolize;;
*) LIBTOOLIZE=libtoolize;;
esac


DIE=0

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have autoconf installed to compile $package."
	echo "Download the appropriate package for your distribution,"
	echo "or see http://www.gnu.org/software/autoconf"
	DIE=1
}

(automake --version) < /dev/null > /dev/null 2>&1 || {
	echo
	DIE=1
	echo "You must have automake installed to compile $package."
	echo "Download the appropriate package for your distribution,"
	echo "or see http://www.gnu.org/software/automake"
}

if test "$DIE" -eq 1; then
	exit 1
fi

if test -z "${CF_ARGS[*]}"; then
	echo "I am going to run ./configure with --enable-warnings - if you "
        echo "wish to pass any extra arguments to it, please specify them on "
        echo "the $0 command line."
fi

mkdir -p $BUILD_AUX

touch ChangeLog
$LIBTOOLIZE --copy --force
./bootstrap ${GNULIB_SRCDIR:+--gnulib-srcdir="$GNULIB_SRCDIR"}
aclocal -I gnulib/m4
autoheader
automake --add-missing
autoconf

cd $THEDIR

if test x$OBJ_DIR != x; then
    mkdir -p "$OBJ_DIR"
    cd "$OBJ_DIR"
fi

"$srcdir/configure" $EXTRA_ARGS "${CF_ARGS[@]}" && {
    echo
    echo "Now type 'make' to compile $package."
}
