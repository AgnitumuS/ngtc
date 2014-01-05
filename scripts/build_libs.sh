#!/bin/bash

SRC="src"
BUILD="build"

if [ ! -d ${SRC} ]; then
	echo "Creating ${SRC} and ${BUILD} directories"
	mkdir ${SRC}
	mkdir ${BUILD}
fi
PREFIX=`pwd`/${BUILD}
cd ${SRC}

echo ${PREFIX}
echo ${SRC}
pwd

CPU="$1"

SYSTEM=`uname`
MACHINE=`uname -m`

if [ "${MACHINE}" != "x86_64" ]; then
	MODE="64"
else
	if [ "${CPU}" == "32" ]; then
		MODE="32"
	else
		MODE="64"
	fi
fi

LINUX_GCC="-Wl,-z,noexecstack -ffast-math -pthread -rdynamic -static"
WIN_GCC="-static -static-libgcc"

FAAC_FLAGS=
LAME_FLAGS=
X264_FLAGS=
ZLIB_FLAGS=
FFMPEG_FLAGS=
FFMPEG_CC=
MEMALIGN=

LIBDIR="${PREFIX}/lib${CPU}"

if [ "${SYSTEM}" != "Linux" ] ; then
        echo "Building for Windows"
        GCC_ARGS=$WIN_GCC
	MEMALIGN="--enable-memalign-hack"
else
        echo "Building for Linux"
        GCC_ARGS=$LINUX_GCC
fi

if [ "${MODE}" == "64" ] ; then
        echo "Cross Compiling for 64bit Linux"
        LINUX_32=
        export CFLAGS=""
        export CXXFLAGS=${CFLAGS}
        export LDFLAGS=${CFLAGS}
	export NEW_CC=gcc

	ZLIB_FLAGS="--64"
else
        echo "Cross Compiling for 32bit Linux"
        export CFLAGS="-m32"
        export CXXFLAGS=${CFLAGS}
        export LDFLAGS=${CFLAGS}

	export NEW_CC="i686-pc-linux-gnu-gcc"

	X264_FLAGS="--host=i686-linux"
	LAME_FLAGS="--build=i686-linux"

        FFMPEG_CC="--enable-cross-compile --arch=i686 --target-os=linux"
	FFMPEG_FLAGS=" --host-cflags=\"${CFLAGS}\" --host-ldflags=\"${LDFLAGS}\""
fi

# ZLIB
if [ -d ./zlib ]; then
	cd zlib
	make clean
	CFGARGS="--prefix=${PREFIX} --libdir=${LIBDIR} --static ${ZLIB_FLAGS}"
	echo "Running ZLIB configure with args:"
	echo " ${CFGARGS}"
	./configure ${CFGARGS}
	make
	make install
	make clean
	cd ../
fi

# LIBPNG
if [ ! -d ./libpng ]; then
	echo "Downloading PNG Library"
	git clone git://libpng.git.sourceforge.net/gitroot/libpng/libpng
fi
cd libpng
./autogen.sh
make clean
CFGARGS="--prefix=${PREFIX} --libdir=${LIBDIR} --disable-shared --enable-static"
echo "Running PNG configure with args:"
echo " ${CFGARGS}"
./configure ${CFGARGS}
make
make install
make clean
cd ../

# FAAC
if [ ! -d ./faac ]; then
	echo "Downloading FAAC Library"
	cvs -d:pserver:anonymous:@faac.cvs.sourceforge.net:/cvsroot/faac login
	cvs -z3 -d:pserver:anonymous@faac.cvs.sourceforge.net:/cvsroot/faac co -P faac
fi
#
cd faac
./bootstrap
make clean
CFGARGS="--prefix=${PREFIX} --libdir=${LIBDIR} --disable-shared --enable-static --without-mp4v2"
echo "Running FAAC configure with args:"
echo " ${CFGARGS}"
./configure ${CFGARGS}
make
make install
make clean
cd ../

# MP3LAME
if [ ! -d ./lame ]; then
	echo "Downloading Lame Library"
	cvs -d:pserver:anonymous:@lame.cvs.sourceforge.net:/cvsroot/lame login
	cvs -z3 -d:pserver:anonymous@lame.cvs.sourceforge.net:/cvsroot/lame co -P lame
fi
#
cd lame
make clean
export CC=${NEW_CC}
CFGARGS="${LAME_FLAGS} --prefix=${PREFIX} --libdir=${LIBDIR} --disable-shared --enable-static \
	--disable-frontend"
echo "Running LAME configure with args:"
echo " ${CFGARGS}"
./configure ${CFGARGS}
make
make install
make clean
cd ../
unset CC

# X264
if [ ! -d ./x264 ]; then
	echo "Downloading X264 Library"
	git clone git://git.videolan.org/x264.git
fi
#
cd x264
make clean
CFGARGS="--prefix=${PREFIX} --libdir=${LIBDIR} --disable-lavf --disable-ffms --disable-gpac \
	--disable-swscale ${X264_FLAGS}"
echo "Running X264 configure with args:"
echo " ${CFGARGS}"
./configure ${CFGARGS}
make
make install
make clean
cd ../

# FFMPEG
if [ ! -d ./ffmpeg ]; then
	echo "Downloading FFMPEG Library"
	svn checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
fi
#
cd ffmpeg
make clean
export CFLAGS="${CFLAGS} -msse -mmmx -I${PREFIX}/include"
export LDFLAGS="${CFLAGS} -L${LIBDIR}"
./configure ${FFMPEG_CC} ${FFMPEG_FLAGS} ${MEMALIGN} \
	--extra-ldflags="-L${LIBDIR}" --extra-cflags="-I${PREFIX}/include" \
        --prefix=${PREFIX} --libdir=${LIBDIR} \
        --extra-libs="${GCC_ARGS} -ldl -lm -lfaac -lx264 -lmp3lame -lm -lpng" \
        --enable-libmp3lame --enable-libx264 --disable-libxvid --enable-libfaac \
        --disable-ffserver --enable-postproc --enable-avfilter --enable-pthreads \
        --enable-nonfree --enable-gpl --enable-static --disable-shared \
        --disable-outdevs --enable-debug --disable-stripping \
        --disable-vdpau --enable-network --disable-libvpx --disable-libvorbis \
	--enable-decoder=png
#echo "Running FFMPEG configure with args:"
#echo " ${CFGARGS}"
#./configure ${CFGARGS}
make
make install
make clean
cd ../


