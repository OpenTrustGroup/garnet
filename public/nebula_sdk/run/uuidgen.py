## SPDX-License-Identifier: BSD-3-Clause
## Copyright (c) 2020 GoldenRiver Technology Co., Ltd.

import argparse
import uuid
import sys


class Uuid:
    def __init__(self, uuid):
        self.uuid = uuid
        self.time_low = self.uuid.hex[0:8]
        self.time_mid = self.uuid.hex[8:12]
        self.time_hi_and_version = self.uuid.hex[12:16]
        self.clock_seq_and_node = [
            self.uuid.hex[i:i + 2] for i in xrange(16, 32, 2)
        ]

    def json_string(self):
        return '''
{
    "uuid" : {
        "time_low" : "0x%s",
        "time_mid": "0x%s",
        "time_hi_and_version": "0x%s",
        "clock_seq_and_node": ["0x%s", "0x%s", "0x%s", "0x%s", "0x%s", "0x%s", "0x%s", "0x%s"]
    }
}
        ''' % tuple([self.time_low, self.time_mid, self.time_hi_and_version] +
                    self.clock_seq_and_node)

    def cpp_string(self):
        return '''
/* UUID : {%s} */
#define UUID {0x%s, 0x%s, 0x%s, {0x%s, 0x%s, 0x%s, 0x%s, 0x%s, 0x%s, 0x%s, 0x%s}}
        ''' % tuple([
            self.uuid, self.time_low, self.time_mid, self.time_hi_and_version
        ] + self.clock_seq_and_node)


def main(args):
    my_uuid = Uuid(uuid.UUID(args.uuid) if args.uuid else uuid.uuid4())
    if args.json:
        print(my_uuid.json_string())
    if args.cpp:
        print(my_uuid.cpp_string())


def parse_arguments(argv):
    parser = argparse.ArgumentParser()

    parser.add_argument(
        '--uuid',
        type=str,
        help='use specific uuid to generate json/c/c++ definition')
    parser.add_argument('--json',
                        help='output json definition',
                        action='store_true')
    parser.add_argument('--cpp',
                        help='output c/c++ definition',
                        action='store_true')

    return parser.parse_args(argv)


if __name__ == '__main__':
    main(parse_arguments(sys.argv[1:]))
