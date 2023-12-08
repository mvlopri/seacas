#! /usr/bin/env bash
### The following assumes you are building in a subdirectory of ACCESS Root
if [ "X$ACCESS" == "X" ] ; then
  ACCESS=$(cd ../../../..; pwd)
  echo "ACCESS set to ${ACCESS}"
fi
INSTALL_PATH=${INSTALL_PATH:-${ACCESS}}

HDF5="${HDF5:-YES}"
DEBUG="${DEBUG:-NO}"
if [ "$DEBUG" == "YES" ]
then
  BUILD_TYPE="DEBUG"
else
  BUILD_TYPE="RELEASE"
fi

SHARED="${SHARED:-YES}"
if [[ "$SHARED" == "ON" || "$SHARED" == "YES" ]]
then
  OS=$(uname -s)
  if [ "$OS" = "Darwin" ] ; then
    LD_EXT="dylib"
  else
    LD_EXT="so"
  fi
else
  LD_EXT="a"
  EXTRA_DEPS="-DNC_EXTRA_DEPS=-ldl\;-lz"
fi

if [[ "$HDF5" == "ON" || "$HDF5" == "YES" ]]
then
    HDF5_INFO="-DHDF5_ROOT:PATH=${INSTALL_PATH} -DHDF5_DIR:PATH=${INSTALL_PATH} -DENABLE_NETCDF4:BOOL=ON"
else
    HDF5_INFO="-DENABLE_HDF5=OFF -DENABLE_NETCDF4:BOOL=OFF"
fi

NEEDS_ZLIB="${NEEDS_ZLIB:-NO}"
if [ "$NEEDS_ZLIB" == "YES" ]
then
   LOCAL_ZLIB="-DZLIB_INCLUDE_DIR:PATH=${INSTALL_PATH}/include -DZLIB_LIBRARY:FILEPATH=${INSTALL_PATH}/lib/libz.${LD_EXT}"
fi

NEEDS_SZIP="${NEEDS_SZIP:-NO}"
if [ "$NEEDS_SZIP" == "YES" ]
then
   LOCAL_SZIP="-DSZIP_INCLUDE_DIR:PATH=${INSTALL_PATH}/include -DSZIP_LIBRARY:FILEPATH=${INSTALL_PATH}/lib/libsz.${LD_EXT}"
fi

. ${ACCESS}/TPL/compiler.sh

# If using an XLF compiler on an IBM system, may need to add the following:
# -DCMAKE_Fortran_FLAGS="-qfixed=72" \
# -DCMAKE_EXE_LINKER_FLAGS:STRING="-lxl -lxlopt"

rm -f config.cache

cmake .. -DCMAKE_C_COMPILER:FILEPATH=${CC} \
         -DBUILD_SHARED_LIBS:BOOL=${SHARED} \
         -DBUILD_TESTING:BOOL=OFF \
         -DCMAKE_INSTALL_PREFIX=${INSTALL_PATH} \
         -DCMAKE_INSTALL_LIBDIR:PATH=lib \
	 -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
         -DENABLE_PNETCDF:BOOL=${MPI} \
         -DENABLE_CDF5=ON \
         -DENABLE_MMAP:BOOL=ON \
         -DENABLE_DAP:BOOL=OFF \
         -DENABLE_V2_API:BOOL=OFF \
	 -DENABLE_FILTER_TESTING:BOOL=OFF \
	 -DENABLE_TESTS:BOOL=OFF \
         ${LOCAL_ZLIB} \
         ${LOCAL_SZIP} \
         ${EXTRA_DEPS} \
	 ${HDF5_INFO} \
         -DENABLE_CONVERSION_WARNINGS:BOOL=OFF

echo ""
echo "         MPI: ${MPI}"
echo "    COMPILER: ${CC}"
echo "      ACCESS: ${ACCESS}"
echo "INSTALL_PATH: ${INSTALL_PATH}"
echo ""
