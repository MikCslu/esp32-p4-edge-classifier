#!/bin/bash
# Usage: ./espdl_to_carray.sh <input.espdl> <variable_name> [output.h]
set -e

INFILE="$1"
VARNAME="${2:-model_data}"
OUTFILE="${3:-${VARNAME}.h}"

if [ -z "$INFILE" ]; then
    echo "Usage: $0 <input.espdl> <variable_name> [output.h]"
    exit 1
fi

xxd -i "$INFILE" | awk -v name="$VARNAME" '
NR==1 {print "const unsigned char " name "[] __attribute__((aligned(16))) = {"; next}
/unsigned int .*_len/ {print "const unsigned int " name "_len = " $NF ";"; next}
{print}
' | sed 's/;;$/;/' > "$OUTFILE"

echo "Generated: $OUTFILE"
