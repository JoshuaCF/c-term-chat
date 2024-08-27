#!/bin/bash

FILES=$(find src/ -name "*.c")
LIB_FILES=$(find lib/ -name "*.o")

clang -O3 -o bin/chat -I src/headers -I lib/headers $LIB_FILES $FILES
