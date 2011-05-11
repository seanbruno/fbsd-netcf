#!/bin/sh
#
# netcf-transaction: save/restore current network interface configuration
#
# chkconfig:   - 09 91
# description: This script can save the current state of network config, \
#              and later revert to that config, or commit the new config \
#              (by deleting the snapshot). At boot time, if there are \
#              uncommitted changes to the network config, they are \
#              reverted (and the discarded changes are archived in \
#              /var/lib/netcf/network-rollback-*).

### BEGIN INIT INFO
# Provides: netcf-transaction
# Required-Start: $local_fs
# Short-Description: save/restore network configuration files
# Description: This script can save the current state of network config,
#              and later revert to that config, or commit the new config
#              (by deleting the snapshot). At boot time, if there are
#              uncommitted changes to the network config, they are
#              reverted (and the discarded changes are archived in
#              /var/lib/netcf/network-rollback-*).
#
### END INIT INFO

# special exit code that means a command was issued that is invalid for the
# current state (e.g. change-begin when there is already an open transaction)
EINVALID_IN_THIS_STATE=199

sysconfdir="@sysconfdir@"
localstatedir="@localstatedir@"

netconfdir="$sysconfdir"/sysconfig/network-scripts
snapshotdir="$localstatedir"/lib/netcf/network-snapshot
rollbackdirbase="$localstatedir"/lib/netcf/network-rollback

# Source function library.
test ! -r "$sysconfdir"/rc.d/init.d/functions ||
    . "$sysconfdir"/rc.d/init.d/functions

# take a snapshot of current network configuration scripts
change_begin ()
{
    if test -e "$snapshotdir"
    then
        echo "There is already an open transaction ($snapshotdir exists)"
        return $EINVALID_IN_THIS_STATE
    fi
    if ! mkdir -p "$snapshotdir"
    then
        echo "failed to create snapshot directory $snapshotdir"
        return 1
    fi
    for f in "$netconfdir"/ifcfg-* "$netconfdir"/route-* \
             "$netconfdir"/rule-*
    do
        test ! -f "$f" && continue
        if ! cp -p "$f" "$snapshotdir"
        then
            echo "failed to copy $f to $snapshotdir"
            return 1
        fi
    done
    date >"$snapshotdir"/date
}

change_commit ()
{
    if test ! -d "$snapshotdir"
    then
        echo "No pending transaction to commit"
        return $EINVALID_IN_THIS_STATE
    fi
    if ! rm -rf "$snapshotdir"
    then
        echo "Failed to remove obsolete snapshot directory $snapshotdir"
    fi
}

# rollback network configuration to last snapshot (if one exists)
change_rollback ()
{
    if test ! -d "$snapshotdir"
    then
        echo "No pending transaction to rollback"
        return $EINVALID_IN_THIS_STATE
    fi

    rollback_ret=0

    # eliminate all but the last 20 rollback saves
    LC_ALL=C ls -d "$rollbackdirbase"-* 2>/dev/null | head -n -20 |\
    while read r
    do
        if ! rm -rf "$r"
        then
            # indicate an error, but continue
            echo "Failed to remove obsolete rollback directory $r"
            rollback_ret=1
        fi
    done

    # save a copy of the current config before rollback "just in case"
    rollbackdir=$rollbackdirbase-$(date +%Y.%m.%d-%H:%M:%S)
    if ! mkdir -p "$rollbackdir"
    then
        echo "failed to create rollback directory $rollbackdir"
        return 1
    fi
    for f in "$netconfdir"/ifcfg-* "$netconfdir"/route-* \
             "$netconfdir"/rule-*
    do
        test ! -f "$f" && continue
        if ! cp -p "$f" "$rollbackdir"
        then
            echo "failed to copy $f to $rollbackdir"
            return 1
        fi
    done

    # There are 4 classes of files, each handled slightly differently to
    # minimize disruption in services:
    # 1) file in both, unmodified - just erase the snapshot copy
    # 2) file in both, modified   - copy the snapshot version over
    #                               the current & erase copy
    # 3) file in current only     - remove the file
    # 4) file in snapshot only    - copy the snapshot file to current
    #
    # We handle the 1st three cases in one loop going through all
    # current config files, and what is left over in snapshotdir is,
    # by definition, case 4.
    #
    # (NB: we can't mv the files, because then the selinux labels
    # don't get reset properly.)

    for f in "$netconfdir"/ifcfg-* "$netconfdir"/route-* \
             "$netconfdir"/rule-*
    do
        test ! -f "$f" && continue

        snapshotf=$snapshotdir/${f##*/}
        if test -f "$snapshotf"
        then
            # Case (1) & (2) (only copies if they're different)
            if ! cmp -s "$snapshotf" "$f"
            then
                if ! cp -pf "$snapshotf" "$f"
                then
                    echo "failed to restore $snapshotf to $f"
                    return 1
                fi
            fi
            if ! rm -f "$snapshotf"
            then
                # indicate an error, but continue
                echo "Failed to remove obsolete snapshot file $snapshotf"
                rollback_ret=1
            fi
        else
            # Case (3)
            if ! rm -f "$f"
            then
                # indicate an error, but continue
                echo "Failed to remove unwanted config file $f"
                rollback_ret=1
            fi
        fi
    done

    # Case (4)
    for f in "$snapshotdir"/ifcfg-* "$snapshotdir"/route-* \
             "$snapshotdir"/rule-*
    do
        test ! -f "$f" && continue
        if ! cp -pf "$f" "$netconfdir"
        then
            echo "failed to restore $f to $netconfdir"
            return 1
        fi
    done

    test -f "$snapshotdir"/date \
     && echo Rolled back to network config state of $(cat "$snapshotdir"/date)

    if ! rm -rf "$snapshotdir"
    then
        # indicate an error, but continue
        echo "Failed to remove obsolete snapshot directory $snapshotdir"
        rollback_ret=1
    fi

    return $rollback_ret
}

# usage [val]
# Display usage string, then exit with VAL (defaults to 2).
usage() {
    echo $"Usage: $0 {change-begin|change-commit|change-rollback|snapshot-dir|start|stop|status|restart|condrestart|try-restart|reload|force-reload}"
    exit ${1-2}
}

# See how we were called.
if test $# != 1; then
    usage
fi

retval=0
case "$1" in
    # commands required in all Fedora initscripts
    start|restart|reload|force-reload|condrestart|try-restart)
        echo -n $"Running $prog $1: "
        change_rollback
        # ignore the "no pending transaction" error
        test "$retval" != "$EINVALID_IN_THIS_STATE" && retval=$?
        echo
        ;;
    stop|status)
        if test -d "$snapshotdir"
        then
            echo $"There is an open transaction"
        else
            echo $"No open transaction"
        fi
        ;;

    --help)
        usage 0
        ;;
    # specific to netcf-transaction
    change-begin)
        change_begin
        retval=$?
        ;;
    change-commit)
        change_commit
        retval=$?
        ;;
    change-rollback)
        change_rollback
        retval=$?
        ;;
    snapshot-dir)
        echo $snapshotdir
        ;;
    *)
        usage
        ;;
esac

exit $retval
