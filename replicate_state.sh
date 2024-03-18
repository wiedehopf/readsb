#!/bin/bash

set -e

# ssh access required for both the source and target box
# this script will transfer the traces for last 24h and current aircraft positions
# it reads the state from the source and will replace the target state except for aircraft that exist on the target but not on the source


# source
SHOST=root@box1
SDIR=/run/readsb

# target
THOST=root@box2
TDIR=/run/readsb

echo "$(date -u --rfc-3339=s) starting transfer from $SHOST to $THOST, 10 seconds to cancel with Ctrl-C"

sleep 10

SSHDIR="$HOME/.vee0za6ugohru6Id0ziK3ahv1ietahva"
rm -rf "$SSHDIR"
mkdir -p "$SSHDIR"
chmod 700 "$SSHDIR"

SSHPERSIST="-o ControlMaster=auto -o ControlPersist=30s"
SSHCOMMON="-o StrictHostKeyChecking=no"

P1="-S $SSHDIR/1cm-%r@%h:%p"
P2="-S $SSHDIR/2cm-%r@%h:%p"

SCMD="ssh $SHOST $SSHCOMMON $SSHPERSIST $P1"
TCMD="ssh $THOST $SSHCOMMON $SSHPERSIST $P2"

if ! $SCMD "cd $SDIR/getState" || ! $TCMD "cd $TDIR/getState"; then
    echo "---------------------------------------------------------"
    echo "ERROR: readsb version too old, use replicate_state_old.sh or update readsb to version >= v3.14.1618"
    echo "---------------------------------------------------------"
    exit 1
fi

GDIR="$SDIR/getState"
RDIR="$TDIR/replaceState"
TTDIR="$TDIR/replaceTmp"
$TCMD "mkdir -p $TTDIR && mkdir -p $RDIR && chmod a+w $RDIR"


suffix="zstl"

for num in $(seq 0 255); do
    blob="$(printf "%02x\n" "$num")"
    TRIGGER="$GDIR/writeState"
    BLOB="blob_${blob}.${suffix}"
    $SCMD "echo $blob > $TRIGGER; while [[ -f $TRIGGER ]]; do sleep 0.01; done;"

    $SCMD "tar -C $GDIR -c -f - $BLOB && rm -f $GDIR/$BLOB" | $TCMD "tar -C $TTDIR --overwrite -x -f - && chmod a+w $TTDIR/$BLOB && mv -f $TTDIR/$BLOB $RDIR/$BLOB;"
    echo "$(date -u --rfc-3339=s) transferring $BLOB"
done

wait # wait for last transfer

echo "$(date -u --rfc-3339=s) transfer done, waiting for completion of state load on the target side"
$TCMD "while ls $RDIR | grep -qs -v -e tmp; do sleep 1; done"
$TCMD "if ls $RDIR | grep -qs ${suffix}; then echo transfer or state loading incomplete, check target readsb log; else echo $(date -u --rfc-3339=s) state loading completed on target; fi"
$TCMD "rm -rf $RDIR $TTDIR"

rm -rf "$SSHDIR"
