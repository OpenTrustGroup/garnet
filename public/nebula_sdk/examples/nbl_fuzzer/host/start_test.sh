#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

UUID="a47f667c-842b-4f11-b22b-0a32e9ac2150"
DEVICE_ID=
DEVICE_IP=
TEST_FUNC=
TEST_CNT=10
TEST_SEED=
TEST_MODE="round"
LOG_REE_FILE="/var/tmp/nss/fuzzing*"
LOG_HOST_PATH="${SCRIPT_PATH}/log"
LOG_PPID=
LOG_PID=
LOG_FILE=
LOG_CMD="nblmsg"

function usage() {
	printf >&2 'Usage: start fuzzing test.\n'
	printf >&2 ' -a <device id> adb device id used to do the test.\n'
	printf >&2 ' -i <ssh ip> ssh ip address used to do the test.\n'
	printf >&2 '   -a or -i, select only one.\n'
	printf >&2 ' -m <mode> mode is one of list/count/round/random/replay, default is "round".\n'
	printf >&2 '   list:   list all test functions.\n'
	printf >&2 '   count:  run one API on <count> times, and run next.\n'
	printf >&2 '   round:  run all APIs as one round, and run <count> rounds.\n'
	printf >&2 '   random: pick one API randomly, and run it. Repeat it on <count> times.\n'
	printf >&2 '   replay: replay function with the given random. Must set argument "-r" and "-f".\n'
	printf >&2 ' -c <count> count value, concrete meaning is depent on mode. Default is 10.\n'
	printf >&2 ' -f <name> function name for test, pattern is same with googletest.\n'
	printf >&2 '    glob pattern, can contain "*/?". ":" can split many patterns. and "-" is negative pattern.\n'
	printf >&2 '    Example: "abc*:123*-1234", "abcd" and "1235" match, but "1234" does not match.\n'
	printf >&2 ' -r <Hex data> Random value(8bytes), such as 0x77ff or 77ff.\n'
	printf >&2 '   If set function name and replay, then this random is used for this function invocation.\n'
	printf >&2 '   If set function name, then this random is used for seed of invoke this function.\n'
	printf >&2 '   If not set function name, then this random is used as start for testing.\n'
	printf >&2 ' If run test\n'
	printf >&2 '   for example: "-m round -c 1000".\n'
	printf >&2 ' If want replay crash, use "-m replay -f <xxx> -e -r <random>". <random> is in log on crash.\n'
	printf >&2 '   for example: 0x38dfc3d474b249f5==> file_handle=(random)3e81ca42a5f, file_size=586763518, opflags=4, ==>End.\n'
	printf >&2 '                ^-- random.\n'
	exit 1
}

function gen_command() {
	local command
	if [[ $DEVICE_ID != "" ]];then
		command="adb -s $DEVICE_ID shell "
	else
		command="ssh -o StrictHostKeyChecking=no root@${DEVICE_IP} "
	fi

	command="${command} ${1}"
	echo $command
}

function run_command() {
	local command=`gen_command "$1"`
	$command
}

function install_ta() {
	make -C $1
	if [[ $? != 0 ]];then
		exit 1
	fi
	if [[ "$DEVICE_ID" != "" ]];then
		make -C $1 install-device
		if [[ $? != 0 ]];then
			exit 1
		fi
	else
		DEVICE_IP=$DEVICE_IP make -C $1 install-qemu
		if [[ $? != 0 ]];then
		exit 1
		fi
	fi
}

function install_fuzzing_ta() {
	install_ta ${SCRIPT_PATH}/../simple
}

function install_ta_for_loader() {
	local TA_PATH=("${SCRIPT_PATH}/../../../examples/nbl_attest/nfc_demo_ta" \
					"${SCRIPT_PATH}/../../../examples/hee/trusty-api/")
	for v in ${TA_PATH[@]}; do
		install_ta $v;
	done
}

function prepare_fuzzing_test() {
	install_ta_for_loader
}

function check_exec_file_exists() {
	local command
	local res

	command=`gen_command "which ${1}"`
	res=`${command}`

	if [[ "$res" != "" ]];then
		return 0
	fi

	return -1
}

function start_log_dumper() {
	local file_name="nblmsg-`date "+%Y%m%d-%H%M%S"`"
	local cmd
	local log_pid
	local command
	local res

	check_exec_file_exists ${LOG_CMD}
	if [[ $? != 0 ]];then
		echo "Please install ${LOG_CMD} first."
		exit 1
	fi

	# run log dumper.
	LOG_FILE=${LOG_HOST_PATH}/${file_name}
	cmd="${LOG_CMD} -w > ${LOG_FILE}"
	mkdir -p ${LOG_HOST_PATH}
	command=`gen_command "${cmd}"`
	bash -c "$command" &
	LOG_PPID=$!

	# get pgid of log dumper process.
	command="ps -ef | awk '{if (\$3 == $LOG_PPID) print \$2 }'"
	LOG_PID=`bash -c "$command"`
}

start_fuzzing_test() {
	local command="nbl_daemon -w ${UUID} "
	local param=" -m ${TEST_MODE} "
	if [[ $TEST_FUNC != "" ]];then
		param="${param} -f $TEST_FUNC "
	fi
	if [[ $TEST_SEED != "" ]];then
		param="${param} -r $TEST_SEED "
	fi
	if [[ $TEST_CNT != "" ]];then
		param="${param} -c $TEST_CNT "
	fi
	if [[ "${param}" != "" ]];then
		command="${command} -a \"${param}\""
	fi
	run_command "$command"
	if [[ $? != 0 ]];then
		echo "Fuzzing test failed."
	else
		# If TA is killed as abnormal status, log will have "<== exception" string.
		if [ `grep -c "<== exception" ${LOG_FILE}` -eq '0' ]; then
			echo "Fuzzing test passed."
		else
			echo "Fuzzing test failed."
		fi
	fi

	echo "Log file location: ${LOG_FILE}"
	kill -9 ${LOG_PPID} ${LOG_PID}
}

while getopts "ha:i:r:f:c:m:" opt; do
	case "${opt}" in
		a) DEVICE_ID=$OPTARG;;
		i) DEVICE_IP=$OPTARG;;
		m) TEST_MODE=$OPTARG;;
		c) TEST_CNT=$OPTARG;;
		r) TEST_SEED=$OPTARG;;
		f) TEST_FUNC=$OPTARG;;
		*) usage;;
	esac
done

if [[ $DEVICE_ID == "" ]] && [[ $DEVICE_IP == "" ]];then
	printf >&2 'Use -a or -i to choose the test target.\n'
	usage
	exit 1
fi

case "$TEST_MODE" in
	"list") ;;
	"round") ;;
	"count") ;;
	"random") ;;
	"replay")
		if [ "${TEST_FUNC}" == "" ]; then
			printf >&2 "On replay mode, shall set test function name.\n"
			exit 1
		fi

		if [ "${TEST_SEED}" == "" ]; then
			printf >&2 "On replay mode, shall set test seed.\n"
			exit 1
		fi
	;;
	*)
		printf >&2 "${TEST_MODE} is unknown mode.\n"
		exit 1
esac

install_fuzzing_ta
prepare_fuzzing_test
start_log_dumper
start_fuzzing_test
