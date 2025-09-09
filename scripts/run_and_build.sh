#!/bin/bash
set -e

cmake --build ./build --target my_mysql_test
./build/tests/my_mysql_test
