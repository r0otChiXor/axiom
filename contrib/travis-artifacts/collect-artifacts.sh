#!/bin/bash -ex

if [ $# -lt 3 ] ; then
    echo "Usage: $0 basedir commit host"
    exit 1
fi

BASEDIR=$1
BUILDDIR=$BASEDIR/build/$2/$3
OUTDIR=$BASEDIR/out

cd $BASEDIR

mkdir -p $BUILDDIR
touch $BUILDDIR/.dummy

# Linux artifacts
cp -a $OUTDIR/bin $BUILDDIR || true

# MaxOSX artifacts
cp -a Castle-Qt.app $BUILDDIR || true
cp -a Castle-Core.dmg $BUILDDIR || true

# Windows artifacts
cp -a release/* $BUILDDIR || true
cp -a castle-*-win*-setup.exe $BUILDDIR || true

find $BUILDDIR
