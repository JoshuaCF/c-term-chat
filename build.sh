#!/bin/bash

if test ! -e "bin"; then
	mkdir -p "bin/release"
	mkdir -p "bin/debug"
fi

FILES=$(find "src/" -name "*.c")
LIB_FILES=$(find "lib/" -name "*.o")

COMPILER=clang

if ! command -v clang; then
	COMPILER=gcc
fi

BINARY_NAME="chat"

$COMPILER -I "src/headers" -I "lib/headers" -O3 $LIB_FILES $FILES -o "bin/release/$BINARY_NAME"
$COMPILER -I "src/headers" -I "lib/headers" -O0 -g $LIB_FILES $FILES -o "bin/debug/$BINARY_NAME"
