#!/bin/bash

FILES=$(find src/ -name "*.c")
LIB_FILES=$(find lib/ -name "*.o")

clang -O0 -g -o bin/chat_dbg -I src/headers -I lib/headers $LIB_FILES $FILES
