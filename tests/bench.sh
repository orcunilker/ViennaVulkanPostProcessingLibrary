#!/bin/bash
# runs the standalone benchmark for every configuration in interleaved, shuffled rounds
# usage: bench.sh <bench-binary> <rounds> <output.csv>
set -e

if [ $# -ne 3 ]; then
	echo "usage: bench.sh <bench-binary> <rounds> <output.csv>" >&2
	exit 1
fi
BENCH=$1
ROUNDS=$2
OUT=$3

# warm-up and iterations were chosen in the pilot
WARMUP=100
ITERATIONS=500
SIZES="1280x720 1920x1080 2560x1440 3840x2160"
CONFIGS="empty full tonemap chromatic greyscale vignette filmgrain colorgrade solarize sabattier emboss sobel speedlines highlight segmentation dither"

echo "round,config,width,height,warmup,iterations,mean_ms" > "$OUT"

for round in $(seq 1 "$ROUNDS"); do
	echo "round $round of $ROUNDS" >&2

	# every size and config once per round, in a new random order
	for size in $SIZES; do
		for config in $CONFIGS; do
			echo "${size%x*} ${size#*x} $config"
		done
	done | sort -R | while read -r width height config; do
		line=$("$BENCH" "$width" "$height" "$config" "$WARMUP" "$ITERATIONS")
		echo "$round,$line" >> "$OUT"
	done
done
