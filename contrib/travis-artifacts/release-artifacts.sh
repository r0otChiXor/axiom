#!/bin/bash -ex

if [ $# -lt 4 ] ; then
    echo "Usage: $0 basedir tag host"
    exit 1
fi

BASEDIR=$1
OUTDIR=$2
RELEASEIR=$BASEDIR/release-build/$3/$4

cd $BASEDIR

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
