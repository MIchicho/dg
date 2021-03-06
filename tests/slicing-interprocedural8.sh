#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

CODE="$TESTS_DIR/sources/interprocedural8.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME.sliced"
LINKEDFILE="$NAME.sliced.linked"

# compile in.c out.bc
compile "$CODE" "$BCFILE"

# slice the code
llvm-slicer $DG_TESTS_PTA -c test_assert "$BCFILE"

# link assert to the code
link_with_assert "$SLICEDFILE" "$LINKEDFILE"

OUTPUT="`lli "$LINKEDFILE" 2>&1`"

#in this test we must have both - PASSED and FAILED
echo "$OUTPUT" | grep -q 'Assertion PASSED' || { echo "$OUTPUT"; errmsg "Assertion 1 missing"; }
echo "$OUTPUT" | grep -q 'Assertion FAILED' || { echo "$OUTPUT"; errmsg "Assertion 2 missing"; }

echo "$OUTPUT"
echo "(First should PASS, the other should FAIL)"
exit 0
