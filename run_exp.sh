# Absolute path of the script directory
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

LOG_FILE="${SCRIPT_DIR}/run_exp.log"
NUM_THREADS=2

for i in $(seq 1 $NUM_THREADS); do
  echo "Running client with $i threads..."
  ${SCRIPT_DIR}/client $i &> ${LOG_FILE}
  tput=$(cat $LOG_FILE | grep "Throughput" | awk '{print $2/1000000}')
  lat=$(cat $LOG_FILE | grep "99th" | awk '{print $4}')
  echo "Client with $i threads: ${tput}, ${lat}"
  rm $LOG_FILE
done
