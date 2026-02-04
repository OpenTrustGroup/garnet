Fuzzing test tool is for TEE of Nebula system.

1. simple: simplest tool for fuzzing test.
   Written by GR programmers, and like Trinity interface(Must can call API and syscalls.).

2. use host/start_test.sh to start the fuzzing test.
    For example in QEMU:
        ./host/start_test.sh -i 192.168.3.53        # test all API
        ./host/start_test.sh -i 192.168.3.53 -m count -f storage_open -c 10  # test given function for some count
    If in real device:
        ./host/start_test.sh -a <device id> -m count -f storage_open -c 10

    In console running above command:
    1. "Fuzzing test failed." : There is "exception" happened in fuzzing.
        User can look for "exception" in below log file. (exception information is below)

(2)[08472.944] 01246.01275> <== exception: process storage[67093] thread initial-thread[67113]
(2)[08472.944] 01246.01275> <== sw breakpoint, PC at 0xdeb503682468
(2)[08472.944] 01246.01275>  x0                   0 x1                   0 x2                   0 x3                   0
(2)[08472.944] 01246.01275>  x4      0xdeb503625b52 x5      0xdda6b313fabe x6                0x29 x7                 0xa
(2)[08472.944] 01246.01275>  x8                 0x1 x9                   0 x10               0x7a x11                0xa
(2)[08472.944] 01246.01275>  x12 0xcccccccccccccccd x13            0x12889 x14                0xa x15     0xdeb50364e040
(2)[08472.945] 01246.01275>  x16     0xdeb5037043f8 x17     0xdeb503682468 x18                  0 x19     0x1700377d4a08
(3)[08472.947] 01246.01275>  x20     0x170037b870b0 x21     0x1700377d4508 x22     0xa5acf8049470 x23     0xa5acf804a330
(3)[08472.947] 01246.01275>  x24                0xa x25     0x170037b87080 x26     0x170037b87098 x27     0x170037b87070
(3)[08472.947] 01246.01275>  x28     0x1700377d4520 x29     0xdda6b313fcf0 lr      0xdeb5036824b0 sp      0xdda6b313fcf0
(3)[08472.947] 01246.01275>  pc      0xdeb503682468 psr         0x60000000
(3)[08472.947] 01246.01275> bottom of user stack:
(3)[08472.947] 01246.01275> 0x0000dda6b313fcf0: b313fd60 0000dda6 f806a634 0000a5ac |`.......4.......|
(3)[08472.947] 01246.01275> 0x0000dda6b313fd00: 377d4a08 00001700 03701268 0000deb5 |.J}7....h.p.....|
......

    2. "Log file location: xxxx/examples/nbl_fuzzer/host/log/nblmsg-20200401-174425",
        Log's path.

    If want replay crash, use "-m replay -f <xxx> -r <random>".
    <random> is in log on crash.
    for example: 0x38dfc3d474b249f5==> file_handle=(random)3e81ca42a5f, file_size=586763518, opflags=4, ==>End.
                 ^-- random value
    ./host/start_test.sh -i 192.168.3.53 -m replay -f storage_open -r 0x38dfc3d474b249f5
    Notice: Above log is after exception information, NOT before it.

Concrete information about arguments of start_test.sh is shown by "./host/start_test.sh -h".
