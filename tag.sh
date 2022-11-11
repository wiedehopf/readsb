#!/bin/bash

set -e
trap 'echo "[ERROR] Error in line $LINENO when executing: $BASH_COMMAND"' ERR

VERSION="3.14.$(( $(cat version | cut -d'.' -f3) + 1 ))"
echo "$VERSION" > version
git add version

cat > debian/changelog <<EOF
readsb ($VERSION) UNRELEASED; urgency=medium

  * In development

 -- Matthias Wirth <matthias.wirth@gmail.com>  $(date -R)
EOF

git add debian/changelog
git commit -m "incrementing version: $VERSION"

git tag "v$VERSION"
git push
git push --tag

