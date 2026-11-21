#!/usr/bin/env bash
# Cross-checks the reference SHA-256 against coreutils sha256sum (SPEC §12.2 stage 2).
#
#   tests/verify_sha256.sh PATH_TO_sha256_ref_tool [COUNT]
#
# Writes COUNT (default 2000) input files of length 0..120 — printable ASCII and arbitrary
# bytes — hashes all of them with both tools and diffs the outputs. Every length 0..120 is
# covered in both alphabets first (padding edges at 55/56 and 63/64/65 included); the rest
# have random lengths. On a mismatch the inputs are kept for inspection and the exit status
# is 1.

set -euo pipefail

tool=${1:?usage: verify_sha256.sh PATH_TO_sha256_ref_tool [COUNT]}
count=${2:-2000}
max_len=120

if [[ ! -x $tool ]]; then
    echo "verify_sha256: $tool is not an executable" >&2
    exit 2
fi
if ! command -v sha256sum >/dev/null; then
    echo "verify_sha256: sha256sum not found" >&2
    exit 2
fi
if ! [[ $count =~ ^[0-9]+$ ]] || (( count < 1 )); then
    echo "verify_sha256: COUNT must be a positive integer" >&2
    exit 2
fi

dir=$(mktemp -d "${TMPDIR:-/tmp}/berejlab-verify.XXXXXX")
keep=0
cleanup() {
    if (( keep == 0 )); then
        rm -rf "$dir"
    fi
}
trap cleanup EXIT

# One pool of printable characters, sliced per file, avoids two processes per file.
pool=$(head -c $(( (count + 1) * max_len * 3 )) /dev/urandom | LC_ALL=C tr -dc '[:print:]')
pool_len=${#pool}
if (( pool_len < max_len )); then
    echo "verify_sha256: could not generate random printable input" >&2
    exit 2
fi

n=0
make_input() {  # $1 = print|bytes, $2 = length
    local f
    printf -v f '%s/%05d.bin' "$dir" "$n"
    n=$(( n + 1 ))
    if [[ $1 == print ]]; then
        local off=$(( (RANDOM * 32768 + RANDOM) % (pool_len - $2 + 1) ))
        printf '%s' "${pool:off:$2}" > "$f"
    else
        head -c "$2" /dev/urandom > "$f"
    fi
}

for (( len = 0; len <= max_len && n < count; len++ )); do
    make_input print "$len"
    if (( n < count )); then
        make_input bytes "$len"
    fi
done
while (( n < count )); do
    if (( RANDOM % 2 )); then
        make_input print $(( RANDOM % (max_len + 1) ))
    else
        make_input bytes $(( RANDOM % (max_len + 1) ))
    fi
done

# Same argument list for both tools, so the outputs are comparable line by line.
files=("$dir"/*.bin)
sha256sum "${files[@]}" > "$dir/sha256sum.out"
"$tool" "${files[@]}" > "$dir/ref.out"

lines=$(wc -l < "$dir/ref.out")
if (( lines != count )); then
    keep=1
    echo "verify_sha256: FAIL: reference produced $lines lines, expected $count ($dir)" >&2
    exit 1
fi
if ! diff "$dir/sha256sum.out" "$dir/ref.out" > "$dir/diff.out"; then
    keep=1
    bad=$(grep -c '^>' "$dir/diff.out" || true)
    echo "verify_sha256: FAIL: $bad of $count digests differ; inputs kept in $dir" >&2
    head -n 20 "$dir/diff.out" >&2
    exit 1
fi
echo "verify_sha256: OK: $count inputs (lengths 0..$max_len, printable and binary)" \
     "match sha256sum"
