#!/usr/bin/env bash


# uncomment this if you want to force a version
#PKBSUITE_VERSION=16.05.2

#BRANCH=develop
BRANCH=master

PROJECT_PATH="/tmp/PKbSuite-aur-$$"
CUR_DIR=$(pwd)

if [ -d $PROJECT_PATH ]; then
    rm -rf $PROJECT_PATH
fi

mkdir $PROJECT_PATH
cd $PROJECT_PATH || exit 1

echo "Project path: $PROJECT_PATH"

# checkout the source code
git clone --depth=1 https://github.com/tberthelot-repositories/PKbSuite.git PKbSuite -b $BRANCH
cd PKbSuite || exit 1


gitCommitHash=`git rev-parse HEAD`
echo "Current commit: $gitCommitHash"

# checkout submodules
git submodule update --init

# build binary translation files
lrelease src/PKbSuite.pro

# remove huge .git folder
rm -Rf .git

if [ -z $PKBSUITE_VERSION ]; then
    # get version from version.h
    PKBSUITE_VERSION=`cat src/version.h | sed "s/[^0-9,.]//g"`
else
    # set new version if we want to override it
    echo "#define VERSION \"$PKBSUITE_VERSION\"" > src/version.h
fi

# set the release string
echo "#define RELEASE \"Production\"" > src/release.h

echo "Using version $PKBSUITE_VERSION..."

pkbsuiteSrcDir="pkbsuite-${PKBSUITE_VERSION}"

# copy some needed files file
cp README.md src
cp CHANGELOG.md src

# rename the src directory
mv src $pkbsuiteSrcDir

# make sure we are in RELEASE mode
sed -i "s/debug//g" $pkbsuiteSrcDir/PKbSuite.pro

archiveFile="$pkbsuiteSrcDir.tar.xz"

# archive the source code
echo "Creating archive $archiveFile..."
tar -cJf $archiveFile $pkbsuiteSrcDir

PKBSUITE_ARCHIVE_MD5=`md5sum ${archiveFile} | awk '{ print $1 }' | tee ${archiveFile}.md5`
PKBSUITE_ARCHIVE_SHA256=`sha256sum ${archiveFile} | awk '{ print $1 }' | tee ${archiveFile}.sha256`
PKBSUITE_ARCHIVE_SHA512=`sha512sum ${archiveFile} | awk '{ print $1 }' | tee ${archiveFile}.sha512`
PKBSUITE_ARCHIVE_SIZE=`stat -c "%s" ${archiveFile}`

echo ""
echo "Sums:"
echo $PKBSUITE_ARCHIVE_MD5
echo $PKBSUITE_ARCHIVE_SHA256
echo $PKBSUITE_ARCHIVE_SHA512
echo ""
echo "Size:"
echo $PKBSUITE_ARCHIVE_SIZE

# write temporary checksum variable file for the deployment scripts
_PKbSuiteCheckSumVarFile="/tmp/PKbSuite.checksum.vars"
echo "PKBSUITE_ARCHIVE_MD5=$PKBSUITE_ARCHIVE_MD5" > ${_PKbSuiteCheckSumVarFile}
echo "PKBSUITE_ARCHIVE_SHA256=$PKBSUITE_ARCHIVE_SHA256" >> ${_PKbSuiteCheckSumVarFile}
echo "PKBSUITE_ARCHIVE_SHA512=$PKBSUITE_ARCHIVE_SHA512" >> ${_PKbSuiteCheckSumVarFile}
echo "PKBSUITE_ARCHIVE_SIZE=$PKBSUITE_ARCHIVE_SIZE" >> ${_PKbSuiteCheckSumVarFile}

if [[ ! -f ${_PKbSuiteCheckSumVarFile} ]]; then
	echo "${_PKbSuiteCheckSumVarFile} doesn't exist."
	exit 1
fi

if [ -z ${PKBSUITE_ARCHIVE_SHA256} ]; then
    echo "PKBSUITE_ARCHIVE_SHA256 was not set!"
	exit 1
fi

if [ -z $PKBSUITE_VERSION ]; then
    # get version from version.h
    PKBSUITE_VERSION=`cat src/version.h | sed "s/[^0-9,.]//g"`
fi

cp ../PKbSuite/build-systems/aur/PKGBUILD .
cp ../PKbSuite/build-systems/aur/.SRCINFO .

# replace the version in the PKGBUILD file
sed -i "s/VERSION-STRING/$PKBSUITE_VERSION/g" PKGBUILD

# replace the commit hash in the PKGBUILD file
sed -i "s/COMMIT-HASH/$gitCommitHash/g" PKGBUILD

# replace the archive sha256 hash in the PKGBUILD file
sed -i "s/ARCHIVE-SHA256/$PKBSUITE_ARCHIVE_SHA256/g" PKGBUILD
echo "Archive sha256: ${PKBSUITE_ARCHIVE_SHA256}"

# replace the version in the .SRCINFO file
sed -i "s/VERSION-STRING/$PKBSUITE_VERSION/g" .SRCINFO

makepkg -si


# remove everything after we are done
if [ -d $PROJECT_PATH ]; then
    rm -rf $PROJECT_PATH
fi
