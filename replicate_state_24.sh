#!/bin/bash

set -e

# ssh access required for both the source and target box
# this script will transfer the traces for last 24h and current aircraft positions
# it reads the state from the source and will replace the target state except for aircraft that exist on the target but not on the source


# source
SHOST=box1
SDIR=/var/globe_history/internal_state

# target
THOST=box2
TDIR=/var/globe_history/internal_state

echo "$(date -u --rfc-3339=s) starting transfer from $SHOST to $THOST"

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

RDIR="$TDIR/replaceState"
TTDIR="$TDIR/tmp"
$TCMD "mkdir -p $TTDIR; mkdir -p $RDIR; chmod a+w $RDIR"


suffix="zstl"

for num in $(seq 0 255); do
    blob="$(printf "%02x\n" "$num")"
    TRIGGER="$SDIR/writeState"
    BLOB="blob_${blob}.${suffix}"
    $SCMD "echo $blob > $TRIGGER; while [[ -f $TRIGGER ]]; do sleep 0.01; done;"

    wait # wait for previous transfer to finish before starting new transfer

    $SCMD "tar -C $SDIR -c -f - $BLOB" | $TCMD "tar -C $TTDIR --overwrite -x -f - && chmod a+w $TTDIR/$BLOB && mv -f $TTDIR/$BLOB $RDIR/$BLOB;" &
    echo "$(date -u --rfc-3339=s) transferring $BLOB"
done

wait # wait for last transfer

echo "$(date -u --rfc-3339=s) transfer done, waiting for completion of state load on the target side"
$TCMD "while ls $RDIR | grep -qs -v -e tmp; do sleep 1; done"
$TCMD "if ls $RDIR | grep -qs ${suffix}; then echo transfer or state loading incomplete, check target readsb log; else echo $(date -u --rfc-3339=s) state loading completed on target; fi"
$TCMD "rm -rf $RDIR $TTDIR"

rm -rf "$SSHDIR"
