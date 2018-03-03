#!/bin/bash -ex

BUILDDIR=build/$TRAVIS_COMMIT/$HOST
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
