Nebula Random creator.

1. draw
    draw random data created by TA to data file in REE.
    Users can enter folder "draw" and make/make run-qemu/make run-device as NORMAL TA.
    Data file includes 10000000 Bytes on default.
    User can change source code to create different length or path of file.

1.1 If run in QEMU, data is in "/var/tmp/nss", file name is "1.data".
    Data file path is in folder which set by argument "nss-srv" of "nbl_daemon --nss-srv xxxx".
    User can run command "ps -Af | grep nbl_daemon" in Android shell to get nbl_daemon command line.
    And then, user can use ssh (for Qemu) or adb (for Android) to pull data file.

1.2 It uses NSC library which is private to TA developer, just public for this draw program.
    Copy to SDK(nsc is NOT public):
    /nebula/third_party/hee-trusty/nbl_nsc/lib/include/nsc_nss.h -> draw/include/
    /nebula/third_party/hee-trusty/nbl_nsc/lib/include/nsc_common.h -> draw/include/
    /nebula/out/arm64/obj/third_party/hee-trusty/nbl_nsc/lib/libnbl_nsc.a -> draw/lib/
