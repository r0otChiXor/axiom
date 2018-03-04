#!/bin/bash -ex

if [ $# -lt 5 ] ; then
    echo "Usage: $0 basedir outdir releasedir commit host"
    exit 1
fi

BASEDIR=$1
OUTDIR=$2
RELEASEBASE=$3
COMMIT=$4
HOST=$5

RELEASEDIR=$BASEDIR/$RELEASEBASE/$COMMIT/$HOST

cd $BASEDIR/bitcoin-$HOST

mkdir -p $RELEASEDIR
ZIPFILES=$(ls $OUTDIR/bin/* || true)

# Linux artifacts
[ -z "$ZIPFILES" ] || \
       zip -uj $RELEASEDIR/castle-$COMMIT.zip ${ZIPFILES}

# MaxOSX artifacts
cp -a Castle-Core.dmg $RELEASEDIR || true

# Windows artifacts
cp -a castle-*-win*-setup.exe $RELEASEDIR || true

find $RELEASEDIR
