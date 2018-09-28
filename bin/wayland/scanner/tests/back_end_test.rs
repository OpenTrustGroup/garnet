// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod test {
    use fuchsia_wayland_core::{Arg, ArgKind, FromMessage, IntoMessage, Message, MessageHeader};
    use fuchsia_zircon::{self as zx, HandleBased};
    use test_protocol::{TestInterfaceEvent, TestInterfaceRequest};
    use zerocopy::AsBytes;

    static SENDER_ID: u32 = 3;

    // Force a compile error if value does not have the AsBytes trait.
    fn is_as_bytes<T: AsBytes>(_: &T) { }

    macro_rules! message_bytes(
        ($sender:expr, $opcode:expr, $val:expr) => {
            unsafe {
                is_as_bytes(&$val);
                use std::mem;
                let value: u32 = mem::transmute($val);
                &[
                    $sender as u8,
                    ($sender >> 8) as u8,
                    ($sender >> 16) as u8,
                    ($sender >> 24) as u8,
                    $opcode as u8,
                    ($opcode >> 8) as u8, // opcode
                    0x0c, 0x00, // length
                    value as u8,
                    (value >> 8) as u8,
                    (value >> 16) as u8,
                    (value >> 24) as u8,
                ]
            }
        }
    );

    macro_rules! assert_match(
        ($e:expr, $p:pat => $a:expr) => (
            match $e {
                $p => $a,
                _ => panic!("Unexpected variant {:?}", $e),
            }
        )
    );

    static UINT_VALUE: u32 = 0x12345678;

    #[test]
    fn test_serialize_uint() {
        let (bytes, handles) = TestInterfaceEvent::Uint { arg: UINT_VALUE }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 0 /* opcode */, UINT_VALUE));
    }

    #[test]
    fn test_deserialize_uint() {
        let request = TestInterfaceRequest::from_message(Message::from_parts(
            message_bytes!(SENDER_ID, 0 /* opcode */, UINT_VALUE).to_vec(),
            Vec::new(),
        )).unwrap();

        assert_match!(request, TestInterfaceRequest::Uint{arg} => assert_eq!(arg, UINT_VALUE));
    }

    static INT_VALUE: i32 = -123;

    #[test]
    fn test_serialize_int() {
        let (bytes, handles) = TestInterfaceEvent::Int { arg: INT_VALUE }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 1 /* opcode */, INT_VALUE));
    }

    #[test]
    fn test_deserialize_int() {
        let request = TestInterfaceRequest::from_message(Message::from_parts(
            message_bytes!(SENDER_ID, 1 /* opcode */, INT_VALUE).to_vec(),
            Vec::new(),
        )).unwrap();

        assert_match!(request, TestInterfaceRequest::Int{arg} => assert_eq!(arg, INT_VALUE));
    }

    static FIXED_VALUE: u32 = 23332125;

    #[test]
    fn test_serialize_fixed() {
        let (bytes, handles) = TestInterfaceEvent::Fixed { arg: FIXED_VALUE }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(
            bytes,
            message_bytes!(SENDER_ID, 2 /* opcode */, FIXED_VALUE)
        );
    }

    #[test]
    fn test_deserialize_fixed() {
        let request = TestInterfaceRequest::from_message(Message::from_parts(
            message_bytes!(SENDER_ID, 2 /* opcode */, FIXED_VALUE).to_vec(),
            Vec::new(),
        )).unwrap();

        assert_match!(request, TestInterfaceRequest::Fixed{arg} => assert_eq!(arg, FIXED_VALUE));
    }

    static STRING_VALUE: &'static str = "This is a wayland string.";
    static STRING_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x03, 0x00, // opcode: 3 (string)
        0x28, 0x00, // length: 40
        0x1a, 0x00, 0x00, 0x00, // string (len) = 26
        0x54, 0x68, 0x69, 0x73, // 'This'
        0x20, 0x69, 0x73, 0x20, // ' is '
        0x61, 0x20, 0x77, 0x61, // 'a way'
        0x79, 0x6c, 0x61, 0x6e, // 'lan'
        0x64, 0x20, 0x73, 0x74, // 'd st'
        0x72, 0x69, 0x6e, 0x67, // 'ring'
        0x2e, 0x00, 0x00, 0x00, // '.' NULL PAD PAD
    ];

    #[test]
    fn test_serialize_string() {
        let (bytes, handles) = TestInterfaceEvent::String {
            arg: STRING_VALUE.to_owned(),
        }.into_message(SENDER_ID)
        .unwrap()
        .take();
        assert!(handles.is_empty());
        assert_eq!(bytes, STRING_MESSAGE_BYTES);
    }

    #[test]
    fn test_deserialize_string() {
        let request = TestInterfaceRequest::from_message(Message::from_parts(
            STRING_MESSAGE_BYTES.to_vec(),
            Vec::new(),
        )).unwrap();

        assert_match!(request, TestInterfaceRequest::String{arg} => assert_eq!(arg, STRING_VALUE));
    }

    static OBJECT_VALUE: u32 = 2;

    #[test]
    fn test_serialize_object() {
        let (bytes, handles) = TestInterfaceEvent::Object { arg: OBJECT_VALUE }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(
            bytes,
            message_bytes!(SENDER_ID, 4 /* opcode */, OBJECT_VALUE)
        );
    }

    #[test]
    fn test_deserialize_object() {
        let request = TestInterfaceRequest::from_message(Message::from_parts(
            message_bytes!(SENDER_ID, 4 /* opcode */, OBJECT_VALUE).to_vec(),
            Vec::new(),
        )).unwrap();

        assert_match!(request, TestInterfaceRequest::Object{arg} => assert_eq!(arg, OBJECT_VALUE));
    }

    static NEW_ID_VALUE: u32 = 112233;

    #[test]
    fn test_serialize_new_id() {
        let (bytes, handles) = TestInterfaceEvent::NewId { arg: NEW_ID_VALUE }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(
            bytes,
            message_bytes!(SENDER_ID, 5 /* opcode */, NEW_ID_VALUE)
        );
    }

    #[test]
    fn test_deserialize_new_id() {
        let request = TestInterfaceRequest::from_message(Message::from_parts(
            message_bytes!(SENDER_ID, 5 /* opcode */, NEW_ID_VALUE).to_vec(),
            Vec::new(),
        )).unwrap();

        assert_match!(request, TestInterfaceRequest::NewId{arg} => assert_eq!(arg, NEW_ID_VALUE));
    }

    static ARRAY_VALUE: &'static [u8] = &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    static ARRAY_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x06, 0x00, // opcode: 6 (array)
        0x18, 0x00, // length: 24
        0x0a, 0x00, 0x00, 0x00, // array (len) = 10
        0x01, 0x02, 0x03, 0x04, // array[0..3]
        0x05, 0x06, 0x07, 0x08, // array[4..7]
        0x09, 0x0a, 0x00, 0x00, // array[8..9] PAD PAD
    ];

    #[test]
    fn test_serialize_array() {
        let (bytes, handles) = TestInterfaceEvent::Array {
            arg: ARRAY_VALUE.to_vec(),
        }.into_message(SENDER_ID)
        .unwrap()
        .take();
        assert!(handles.is_empty());
        assert_eq!(bytes, ARRAY_MESSAGE_BYTES);
    }

    #[test]
    fn test_deserialize_array() {
        let request = TestInterfaceRequest::from_message(Message::from_parts(
            ARRAY_MESSAGE_BYTES.to_vec(),
            Vec::new(),
        )).unwrap();

        assert_match!(request, TestInterfaceRequest::Array{arg} => assert_eq!(arg, ARRAY_VALUE));
    }

    static HANDLE_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x07, 0x00, // opcode: 7 (handle)
        0x08, 0x00, // length: 8
    ];

    #[test]
    fn test_serialize_handle() {
        let (s1, s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let (bytes, handles) = TestInterfaceEvent::Handle {
            arg: s1.into_handle(),
        }.into_message(SENDER_ID)
        .unwrap()
        .take();
        assert_eq!(bytes, HANDLE_MESSAGE_BYTES);
        assert_eq!(handles.len(), 1);
        assert!(!handles[0].is_invalid());
    }

    #[test]
    fn test_deserialize_handle() {
        let (s1, s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let request = TestInterfaceRequest::from_message(Message::from_parts(
            HANDLE_MESSAGE_BYTES.to_vec(),
            vec![s1.into_handle()],
        )).unwrap();

        assert_match!(request, TestInterfaceRequest::Handle{arg} => assert!(!arg.is_invalid()));
    }

    static COMPLEX_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x08, 0x00, // opcode: 8 (complex)
        0x44, 0x00, // length: 68
        0x78, 0x56, 0x34, 0x12, // uint 0x12345678
        0x85, 0xff, 0xff, 0xff, // int: -123
        0x02, 0x00, 0x00, 0x00, // object: 2
        0x1a, 0x00, 0x00, 0x00, // string (len) = 26
        0x54, 0x68, 0x69, 0x73, // 'This'
        0x20, 0x69, 0x73, 0x20, // ' is '
        0x61, 0x20, 0x77, 0x61, // 'a way'
        0x79, 0x6c, 0x61, 0x6e, // 'lan'
        0x64, 0x20, 0x73, 0x74, // 'd st'
        0x72, 0x69, 0x6e, 0x67, // 'ring'
        0x2e, 0x00, 0x00, 0x00, // '.' NULL PAD PAD
        0x0a, 0x00, 0x00, 0x00, // array (len) = 10
        0x01, 0x02, 0x03, 0x04, // array[0..3]
        0x05, 0x06, 0x07, 0x08, // array[4..7]
        0x09, 0x0a, 0x00, 0x00, // array[8..9] PAD PAD
    ];

    #[test]
    fn test_deserialize_complex() {
        let (s1, s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let message_handles: Vec<zx::Handle> = vec![s1.into_handle(), s2.into_handle()];
        let request = test_protocol::TestInterfaceRequest::from_message(Message::from_parts(
            COMPLEX_MESSAGE_BYTES.to_vec(),
            message_handles,
        )).unwrap();

        match request {
            test_protocol::TestInterfaceRequest::Complex {
                uint_arg,
                int_arg,
                handle_arg1,
                object_arg,
                handle_arg2,
                string_arg,
                array_arg,
            } => {
                assert_eq!(UINT_VALUE, uint_arg);
                assert_eq!(INT_VALUE, int_arg);
                assert!(!handle_arg1.is_invalid());
                assert_eq!(OBJECT_VALUE, object_arg);
                assert!(!handle_arg2.is_invalid());
                assert_eq!(STRING_VALUE, &string_arg);
                assert_eq!(ARRAY_VALUE, array_arg.as_slice());
            }
            _ => panic!("Message deserialized to incorrect variant {:?}", request),
        }
    }

    #[test]
    fn test_serialize_complex() {
        let (s1, s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let event = test_protocol::TestInterfaceEvent::Complex {
            uint_arg: UINT_VALUE,
            int_arg: INT_VALUE,
            handle_arg1: s1.into_handle(),
            object_arg: OBJECT_VALUE,
            handle_arg2: s2.into_handle(),
            string_arg: STRING_VALUE.to_owned(),
            array_arg: ARRAY_VALUE.to_vec(),
        };
        let message = event.into_message(SENDER_ID).unwrap();
        let (message_bytes, message_handles) = message.take();

        assert_eq!(COMPLEX_MESSAGE_BYTES, message_bytes.as_slice());
        assert_eq!(2, message_handles.len());
        assert!(!message_handles[0].is_invalid());
        assert!(!message_handles[1].is_invalid());
    }

    #[test]
    fn test_deserialize_request_invalid_opcode() {
        let message = vec![
            0x03, 0x00, 0x00, 0x00, // sender: 3
            0x6f, 0x00, // opcode: 111 (invalid)
            0x08, 0x00, // length: 8
        ];

        let result = test_protocol::TestInterfaceRequest::from_message(Message::from_parts(
            message,
            Vec::new(),
        ));
        assert!(result.is_err());
    }

    #[test]
    fn test_deserialize_request_message_too_short() {
        let message = vec![
            0x03, 0x00, 0x00, 0x00, // sender: 3
            0x00, 0x00, // opcode: 0
            0x44, 0x00, // length: 68
        ];

        let result = test_protocol::TestInterfaceRequest::from_message(Message::from_parts(
            message,
            Vec::new(),
        ));
        assert!(result.is_err());
    }

}
