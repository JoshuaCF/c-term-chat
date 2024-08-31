#!/bin/bash

FILES=$(find src/ -name "*.c")
LIB_FILES=$(find lib/ -name "*.o")

clang -o bin/chat -I src/headers -I lib/headers $LIB_FILES $FILES
clang -O0 -g -o bin/chat_dbg -I src/headers -I lib/headers $LIB_FILES $FILES
