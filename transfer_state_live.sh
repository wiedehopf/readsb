#!/bin/bash


# ssh access required for both the source and target box
# this script will transfer the traces for last 24h and current aircraft positions
# it reads the state from the source and will replace the target state except for aircraft that exist on the target but not on the source

# source
SHOST=localhost
SDIR=/var/globe_history/internal_state

# target
THOST=localhost
TDIR=/var/globe_history/internal_state

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
for num in $(seq 0 255); do
    blob="$(printf "%02x\n" "$num")"
    TRIGGER="$SDIR/writeState"
    LZOL="blob_${blob}.lzol"
    $SCMD "echo $blob > $TRIGGER; while [[ -f $TRIGGER ]]; do sleep 0.1; done;" &
    echo "transferring $LZOL"
    wait
    sleep 0.2 &
    $SCMD "tar -C $SDIR -c -f - $LZOL" | $TCMD "tar -C $TTDIR --overwrite -x -f - && chmod a+w $TTDIR/$LZOL && mv -f $TTDIR/$LZOL $RDIR/$LZOL;" &
    wait
done

echo "transfer done, waiting for completion of state load on the target side"
$TCMD "while ls $RDIR | grep -v tmp &>/dev/null; do sleep 1; done"
$TCMD "if ls $RDIR | grep lzol; then echo transfer or state loading incomplete, check target readsb log; else echo state loading completed on target; fi"
$TCMD "rm -rf $RDIR $TTDIR"

rm -rf "$SSHDIR"
