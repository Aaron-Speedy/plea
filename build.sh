#!/bin/sh

rm -r bin
mkdir bin

set -xe

cc plea.c -o bin/plea -Wall -Wextra -std=c11 -ggdb -fsanitize=undefined,address
