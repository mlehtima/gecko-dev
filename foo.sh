#!/bin/bash
export BUILD_DIR=`pwd`/../obj-build-mer-qt-xr
export BASE_CONFIG=`pwd`/../embedding/embedlite/config/mozconfig.merqtxulrunner
export CARGO_HOME=`pwd`/.cargo
# This should be added very likely to the 
# export CARGOFLAGS=' -Z avoid-dev-deps'
# export CARGOFLAGS=' -j1'
mkdir -p $BUILD_DIR
cp -rf $BASE_CONFIG $BUILD_DIR/mozconfig
echo "export MOZCONFIG=$BUILD_DIR/mozconfig" >> $BUILD_DIR/rpm-shared.env
echo "export QT_QPA_PLATFORM=minimal" >> $BUILD_DIR/rpm-shared.env
echo "export MOZ_OBJDIR=$BUILD_DIR" >> $BUILD_DIR/rpm-shared.env
echo "export CARGO_HOME=$CARGO_HOME" >> $BUILD_DIR/rpm-shared.env
# echo "export CARGOFLAGS=' -Z avoid-dev-deps'" >> $BUILD_DIR/rpm-shared.env
source $BUILD_DIR/rpm-shared.env
#$PWD/mach build
# -j1
