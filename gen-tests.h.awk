#!/usr/bin/awk -f
BEGIN {
}

match($0, /^TEST_[A-Za-z0-9_]+/) {
	name = substr($0, RSTART, RLENGTH)
	print "int " name "(void);"
	tests = tests "\t{" name ", \"" name "\"},\n"
}

END {
    print "\nstruct {"
    print "\tint (*func)(void);"
    print "\tconst char *name;"
    print "} TESTS[] = {"
    print tests "};"
}
