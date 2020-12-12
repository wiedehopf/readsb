#!/bin/bash
set -e

TARGET=$1

if ! [[ -d "$TARGET" ]]; then
    echo argument is not a directory!
    exit 1
fi

cd "$TARGET"
UG=$(stat -c "%U:%G" "$TARGET")

for DATE in ????-??-??; do
    YEAR="${DATE:0:4}"
    MM="${DATE:5:2}"
    DD="${DATE:8:2}"
    if ! [[ -d "$YEAR" ]]; then
        mkdir "$YEAR"
        chown "$UG" "$YEAR"
    fi
    if ! [[ -d "$YEAR/$MM" ]]; then
        mkdir "$YEAR/$MM"
        chown "$UG" "$YEAR/$MM"
    fi

    mv -T "$DATE" "$YEAR/$MM/$DD"
done
