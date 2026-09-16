#!/bin/bash

##################################
# RDMA B+ Tree Client Experiment #
##################################

# Start the server first then run this script to get tput/latency

# Usage: ./run_exp.sh -o <output.csv> -b <bench>

while getopts "o:b:" opt; do
	case "$opt" in
		o) CSV_FILE=$(readlink -f "$OPTARG") ;;
		b) BENCH="$OPTARG" ;;
		*) echo "Usage: $0 -o <output.csv> -b <bench>"; exit 1 ;;
	esac
done

if [ -z "$CSV_FILE" ] || [ -z "$BENCH" ]; then
	echo "Usage: $0 -o <output.csv> -b <bench>"
	exit 1
fi

# Absolute path of the script directory
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

# Client binary, resolved relative to the script directory so this script
# can be invoked from anywhere
CLIENT_BIN="${SCRIPT_DIR}/client"

# Directory where experiment logs are stored, same directory as the CSV file
RESULTS_DIR=$(dirname "$CSV_FILE")
mkdir -p "$RESULTS_DIR"

NUM_THREADS=8
NUM_ITER=2
NUMA_NODE=0

LOG_FILE="${RESULTS_DIR}/bpt_experiment.log"
rm -f "$LOG_FILE"

echo "bench,threads,avg_tput_mpps,avg_lat_99th" > "$CSV_FILE"

for i in $(seq 1 $NUM_THREADS); do
	echo "Running experiment with $i threads..."
	for j in $(seq 1 $NUM_ITER); do
		numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE "$CLIENT_BIN" $i &>> "$LOG_FILE"
	done
	avg_tput=$(cat $LOG_FILE | grep "Throughput" | awk '{total += $2/1000000}END{print total/NR}')
	avg_lat=$(cat $LOG_FILE | grep "99th" | awk '{total += $4}END{print total/NR}')
	echo "Average tput,lat for $i threads: $avg_tput, $avg_lat"
	echo "${BENCH},${i},${avg_tput},${avg_lat}" >> "$CSV_FILE"
	rm "$LOG_FILE"
done
