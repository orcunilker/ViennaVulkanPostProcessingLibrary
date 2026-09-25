#!/bin/bash
# runs the standalone benchmark for every configuration in interleaved, shuffled rounds
# usage: bench.sh <bench-binary> <rounds> <output.csv>
set -euo pipefail

if [ $# -ne 3 ]; then
	echo "usage: bench.sh <bench-binary> <rounds> <output.csv>" >&2
	exit 1
fi
BENCH=$1
ROUNDS=$2
OUT=$3
if ! [[ "$ROUNDS" =~ ^[1-9][0-9]*$ ]] || [ ! -x "$BENCH" ]; then
	echo "expected an executable benchmark and a positive round count" >&2
	exit 1
fi
if [ -e "$OUT" ]; then
	echo "refusing to overwrite $OUT" >&2
	exit 1
fi

WARMUP=500
ITERATIONS=500
SIZES="1920x1080 3840x2160"
CONFIGS="empty full tonemap chromatic greyscale vignette filmgrain colorgrade solarize sabattier emboss sobel speedlines highlight segmentation dither invert"

echo "round,config,width,height,warmup,iterations,mean_ms" > "$OUT"

for round in $(seq 1 "$ROUNDS"); do
	echo "round $round of $ROUNDS" >&2

	# Keep each resolution in a block so its empty reference is nearby.
	for size in $SIZES; do
		echo "$size"
	done | sort -R | while read -r size; do
		for config in $CONFIGS; do
			echo "$config"
		done | sort -R | while read -r config; do
			line=$("$BENCH" "${size%x*}" "${size#*x}" "$config" "$WARMUP" "$ITERATIONS")
			echo "$round,$line" >> "$OUT"
		done
	done
done
