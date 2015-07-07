#!/bin/sh
valgrind --leak-check=full --suppressions=valgrind.supp ./werf "$@"

