#!/bin/bash -ex

RELEASEDIR=release-build/$TRAVIS_TAG/$HOST
mkdir -p $RELEASEDIR
ZIPFILES=$OUTDIR/bin/*

# Linux artifacts
[ "$ZIPFILES" = "$OUTDIR/bin/*" ] || \
       zip -uj $RELEASEDIR/castle-$TRAVIS_TAG.zip $ZIPFILES

# MaxOSX artifacts
cp -a Castle-Core.dmg $RELEASEDIR || true

# Windows artifacts
cp -a castle-*-win*-setup.exe $RELEASEDIR || true

find $RELEASEDIR
