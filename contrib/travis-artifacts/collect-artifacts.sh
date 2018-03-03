#!/bin/bash -ex

if [ $# -lt 4 ] ; then
    echo "Usage: $0 basedir outdir commit host"
    exit 1
fi

BASEDIR=$1
OUTDIR=$2
BUILDDIR=$BASEDIR/build/$3/$4

cd $BASEDIR/bitcoin-$4

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
