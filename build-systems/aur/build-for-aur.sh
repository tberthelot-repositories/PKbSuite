#!/usr/bin/env bash
#
# Build script for the Arch Linux User Repository (AUR)
#
# Currently based on my local master repository
#


# uncomment this if you want to force a version
#PKBSUITE_VERSION=20.07.1

BRANCH=master

PROJECT_PATH="/tmp/PKbSuite-aur-$$"
# SRC_PATH="/mnt/Documents/_Thomas/1. Projects/_Repository/PKbSuite"
SRC_PATH="/home/thomas/1. Projects/PKbSuite"
CUR_DIR=$(pwd)

echo "Started the AUR packaging process"

if [ -d $PROJECT_PATH ]; then
    rm -rf $PROJECT_PATH
fi

mkdir $PROJECT_PATH
cd $PROJECT_PATH

echo "Project path: $PROJECT_PATH"

# Copy the file from reference folder
cp -r "$SRC_PATH/" .

cd PKbSuite

# remove huge .git folder
rm -Rf .git

# Compile translation files
lrelease src/PKbSuite.pro

if [ -z $PKBSUITE_VERSION ]; then
    # get version from version.h
    PKBSUITE_VERSION=`cat src/version.h | sed "s/[^0-9,.]//g"`
fi

cp build-systems/aur/PKGBUILD .

# replace the version in the PKGBUILD file
sed -i "s/VERSION-STRING/$PKBSUITE_VERSION/g" PKGBUILD

# Create archive
mv src pkbsuite-$PKBSUITE_VERSION
tar cfJ pkbsuite-$PKBSUITE_VERSION.tar.xz pkbsuite-$PKBSUITE_VERSION

# Generate checksums
updpkgsums

# Generate package
makepkg
