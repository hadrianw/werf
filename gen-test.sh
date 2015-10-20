#!/bin/sh
(for i in "$@"; do
	nm "$i"
	objcopy -w -N 'TEST_*' "$i" > /dev/null
done) | awk '
$2 == "T" && $3 ~ /^TEST_/ {
	print "void " $3 "();"
	funcs = funcs "\t" $3 "();\n"
}

END {
	print "\nint\nmain(int argc, char *argv[]) {"
	print "\t(void)argc; (void)argv;"
	print funcs "}"
}'
