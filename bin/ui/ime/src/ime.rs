// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ime_service::ImeService;
use failure::ResultExt;
use fidl::encoding::OutOfLine;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_input::InputMethodEditorRequest as ImeReq;
use fuchsia_syslog::{fx_log, fx_log_err, fx_log_warn};
use futures::prelude::*;
use parking_lot::Mutex;
use std::char;
use std::ops::Range;
use std::sync::{Arc, Weak};

// TODO(lard): move constants into common, centralized location?
const HID_USAGE_KEY_BACKSPACE: u32 = 0x2a;
const HID_USAGE_KEY_RIGHT: u32 = 0x4f;
const HID_USAGE_KEY_LEFT: u32 = 0x50;
const HID_USAGE_KEY_ENTER: u32 = 0x28;

/// The internal state of the IME, usually held within the IME behind an Arc<Mutex>
/// so it can be accessed from multiple places.
pub struct ImeState {
    text_state: uii::TextInputState,
    client: Box<uii::InputMethodEditorClientProxyInterface>,
    keyboard_type: uii::KeyboardType,
    action: uii::InputMethodAction,
    ime_service: ImeService,
}

/// A service that talks to a text field, providing it edits and cursor state updates
/// in response to user input.
#[derive(Clone)]
pub struct Ime(Arc<Mutex<ImeState>>);

impl Ime {
    pub fn new<I: 'static + uii::InputMethodEditorClientProxyInterface>(
        keyboard_type: uii::KeyboardType, action: uii::InputMethodAction,
        initial_state: uii::TextInputState, client: I, ime_service: ImeService,
    ) -> Ime {
        let state = ImeState {
            text_state: initial_state,
            client: Box::new(client),
            keyboard_type: keyboard_type,
            action: action,
            ime_service: ime_service,
        };
        Ime(Arc::new(Mutex::new(state)))
    }

    pub fn downgrade(&self) -> Weak<Mutex<ImeState>> {
        Arc::downgrade(&self.0)
    }

    pub fn upgrade(weak: &Weak<Mutex<ImeState>>) -> Option<Ime> {
        weak.upgrade().map(|arc| Ime(arc))
    }

    pub fn bind_ime(&self, chan: fuchsia_async::Channel) {
        let self_clone = self.clone();
        let self_clone_2 = self.clone();
        fuchsia_async::spawn(
            async move {
                let mut stream = uii::InputMethodEditorRequestStream::from_channel(chan);
                while let Some(msg) = await!(stream.try_next())
                    .context("error reading value from IME request stream")?
                {
                    match msg {
                        ImeReq::SetKeyboardType { keyboard_type, .. } => {
                            let mut state = self_clone.0.lock();
                            state.keyboard_type = keyboard_type;
                        }
                        ImeReq::SetState { state, .. } => {
                            self_clone.set_state(state);
                        }
                        ImeReq::InjectInput { event, .. } => {
                            self_clone.inject_input(event);
                        }
                        ImeReq::Show { .. } => {
                            // clone to ensure we only hold one lock at a time
                            let ime_service = self_clone.0.lock().ime_service.clone();
                            ime_service.show_keyboard();
                        }
                        ImeReq::Hide { .. } => {
                            // clone to ensure we only hold one lock at a time
                            let ime_service = self_clone.0.lock().ime_service.clone();
                            ime_service.hide_keyboard();
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e))
                .then(async move |()| {
                    // this runs when IME stream closes
                    // clone to ensure we only hold one lock at a time
                    let ime_service = self_clone_2.0.lock().ime_service.clone();
                    ime_service.update_keyboard_visibility_from_ime(&self_clone_2.0, false);
                }),
        );
    }

    fn set_state(&self, input_state: uii::TextInputState) {
        self.0.lock().text_state = input_state;
        // the old C++ IME implementation didn't call did_update_state here, so we won't either.
    }

    pub fn inject_input(&self, event: uii::InputEvent) {
        let mut state = self.0.lock();
        let keyboard_event = match event {
            uii::InputEvent::Keyboard(e) => e,
            _ => return,
        };

        if keyboard_event.phase == uii::KeyboardEventPhase::Pressed
            || keyboard_event.phase == uii::KeyboardEventPhase::Repeat
        {
            if keyboard_event.code_point != 0 {
                state.type_keycode(keyboard_event.code_point);
                state.did_update_state(keyboard_event)
            } else {
                match keyboard_event.hid_usage {
                    HID_USAGE_KEY_BACKSPACE => {
                        state.delete_backward();
                        state.did_update_state(keyboard_event);
                    }
                    HID_USAGE_KEY_LEFT => {
                        state.cursor_horizontal_move(keyboard_event.modifiers, false);
                        state.did_update_state(keyboard_event);
                    }
                    HID_USAGE_KEY_RIGHT => {
                        state.cursor_horizontal_move(keyboard_event.modifiers, true);
                        state.did_update_state(keyboard_event);
                    }
                    HID_USAGE_KEY_ENTER => {
                        state.client.on_action(state.action).unwrap_or_else(|e| {
                            fx_log_warn!("error sending action to ImeClient: {:?}", e)
                        });
                    }
                    // we're ignoring many editing keys right now, this is where they would
                    // be added
                    _ => {
                        // not an editing key we recognize, so do nothing
                        ()
                    }
                }
            }
        }
    }
}

impl ImeState {
    pub fn did_update_state(&mut self, e: uii::KeyboardEvent) {
        self.client
            .did_update_state(
                &mut self.text_state,
                Some(OutOfLine(&mut uii::InputEvent::Keyboard(e))),
            ).unwrap_or_else(|e| fx_log_warn!("error sending state update to ImeClient: {:?}", e));
    }

    // gets start and len, and sets base/extent to start of string if don't exist
    pub fn selection(&mut self) -> Range<usize> {
        let s = &mut self.text_state.selection;
        s.base = s.base.max(0).min(self.text_state.text.len() as i64);
        s.extent = s.extent.max(0).min(self.text_state.text.len() as i64);
        let start = s.base.min(s.extent) as usize;
        let end = s.base.max(s.extent) as usize;
        (start..end)
    }

    pub fn type_keycode(&mut self, code_point: u32) {
        self.text_state.revision += 1;

        let replacement = match char::from_u32(code_point) {
            Some(v) => v.to_string(),
            None => return,
        };

        let selection = self.selection();
        self.text_state
            .text
            .replace_range(selection.clone(), &replacement);

        self.text_state.selection.base = selection.start as i64 + replacement.len() as i64;
        self.text_state.selection.extent = self.text_state.selection.base;
    }

    pub fn delete_backward(&mut self) {
        self.text_state.revision += 1;

        // set base and extent to 0 if either is -1, to ensure there is a selection/cursor
        self.selection();

        if self.text_state.selection.base == self.text_state.selection.extent {
            if self.text_state.selection.base > 0 {
                // Change cursor to 1-char selection, so that it can be uniformly handled
                // by the selection-deletion code below.
                self.text_state.selection.base -= 1;
            } else {
                // Cursor is at beginning of text; there is nothing previous to delete.
                return;
            }
        }

        // Delete the current selection.
        let selection = self.selection();
        self.text_state.text.replace_range(selection.clone(), "");
        self.text_state.selection.extent = selection.start as i64;
        self.text_state.selection.base = self.text_state.selection.extent;
    }

    pub fn cursor_horizontal_move(&mut self, modifiers: u32, go_right: bool) {
        self.text_state.revision += 1;

        let shift_pressed = modifiers & uii::MODIFIER_SHIFT != 0;
        let selection = self.selection();
        let text_is_selected = selection.start != selection.end;
        let mut new_position = self.text_state.selection.extent;

        if !shift_pressed && text_is_selected {
            // canceling selection, new position based on start/end of selection
            if go_right {
                new_position = selection.end as i64;
            } else {
                new_position = selection.start as i64;
            }
        } else {
            // new position based previous value of extent
            if go_right {
                new_position += 1
            } else {
                new_position -= 1
            }
            new_position = new_position.max(0).min(self.text_state.text.len() as i64);
        }

        self.text_state.selection.extent = new_position;
        if !shift_pressed {
            self.text_state.selection.base = new_position;
        }
        self.text_state.selection.affinity = uii::TextAffinity::Downstream;
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl;
    use fuchsia_zircon as zx;
    use std::sync::mpsc::{channel, Receiver, Sender};

    pub fn default_state() -> uii::TextInputState {
        uii::TextInputState {
            revision: 1,
            text: "".to_string(),
            selection: uii::TextSelection {
                base: -1,
                extent: -1,
                affinity: uii::TextAffinity::Upstream,
            },
            composing: uii::TextRange { start: -1, end: -1 },
        }
    }

    pub fn clone_state(state: &uii::TextInputState) -> uii::TextInputState {
        uii::TextInputState {
            revision: state.revision,
            text: state.text.clone(),
            selection: uii::TextSelection {
                base: state.selection.base,
                extent: state.selection.extent,
                affinity: state.selection.affinity,
            },
            composing: uii::TextRange {
                start: state.composing.start,
                end: state.composing.end,
            },
        }
    }

    fn set_up(
        text: &str, base: i64, extent: i64,
    ) -> (
        Ime,
        Receiver<uii::TextInputState>,
        Receiver<uii::InputMethodAction>,
    ) {
        let (client, statechan, actionchan) = MockImeClient::new();
        let mut state = default_state();
        state.text = text.to_string();
        state.selection.base = base;
        state.selection.extent = extent;
        let ime = Ime::new(
            uii::KeyboardType::Text,
            uii::InputMethodAction::Search,
            state,
            client,
            ImeService::new(),
        );
        (ime, statechan, actionchan)
    }

    fn simulate_keypress<K: Into<u32> + Copy>(
        ime: &mut Ime, key: K, hid_key: bool, shift_pressed: bool,
    ) {
        let hid_usage = if hid_key { key.into() } else { 0 };
        let code_point = if hid_key { 0 } else { key.into() };
        ime.inject_input(uii::InputEvent::Keyboard(uii::KeyboardEvent {
            event_time: 0,
            device_id: 0,
            phase: uii::KeyboardEventPhase::Pressed,
            hid_usage: hid_usage,
            code_point: code_point,
            modifiers: if shift_pressed {
                uii::MODIFIER_SHIFT
            } else {
                0
            },
        }));
        ime.inject_input(uii::InputEvent::Keyboard(uii::KeyboardEvent {
            event_time: 0,
            device_id: 0,
            phase: uii::KeyboardEventPhase::Released,
            hid_usage: hid_usage,
            code_point: code_point,
            modifiers: if shift_pressed {
                uii::MODIFIER_SHIFT
            } else {
                0
            },
        }));
    }

    struct MockImeClient {
        pub state: Mutex<Sender<uii::TextInputState>>,
        pub action: Mutex<Sender<uii::InputMethodAction>>,
    }
    impl MockImeClient {
        fn new() -> (
            MockImeClient,
            Receiver<uii::TextInputState>,
            Receiver<uii::InputMethodAction>,
        ) {
            let (s_send, s_rec) = channel();
            let (a_send, a_rec) = channel();
            let client = MockImeClient {
                state: Mutex::new(s_send),
                action: Mutex::new(a_send),
            };
            (client, s_rec, a_rec)
        }
    }
    impl uii::InputMethodEditorClientProxyInterface for MockImeClient {
        fn did_update_state(
            &self, state: &mut uii::TextInputState,
            mut _event: Option<fidl::encoding::OutOfLine<uii::InputEvent>>,
        ) -> Result<(), fidl::Error> {
            let state2 = clone_state(state);
            self.state
                .lock()
                .send(state2)
                .map_err(|_| fidl::Error::ClientWrite(zx::Status::PEER_CLOSED))
        }
        fn on_action(&self, action: uii::InputMethodAction) -> Result<(), fidl::Error> {
            self.action
                .lock()
                .send(action)
                .map_err(|_| fidl::Error::ClientWrite(zx::Status::PEER_CLOSED))
        }
    }

    #[test]
    fn test_mock_ime_channels() {
        let (client, statechan, actionchan) = MockImeClient::new();
        let mut ime = Ime::new(
            uii::KeyboardType::Text,
            uii::InputMethodAction::Search,
            default_state(),
            client,
            ImeService::new(),
        );
        assert_eq!(true, statechan.try_recv().is_err());
        assert_eq!(true, actionchan.try_recv().is_err());
        simulate_keypress(&mut ime, 'a', false, false);
        assert_eq!(false, statechan.try_recv().is_err());
        assert_eq!(true, actionchan.try_recv().is_err());
    }

    #[test]
    fn test_delete_backward_empty_string() {
        let (mut ime, statechan, _actionchan) = set_up("", -1, -1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);

        // a second delete still does nothing, but increments revision
        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_beginning_string() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 0);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_first_char_selected() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("bcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_end_string() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_delete_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 6);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcghi", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_delete_selection_inverted() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 6, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcghi", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_delete_no_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", -1, -1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_with_zero_width_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abdefghi", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_delete_with_zero_width_selection_at_end() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_delete_selection_out_of_bounds() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 20, 24);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_cursor_left_on_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 1, 5);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, true); // right with shift
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(1, state.selection.base);
        assert_eq!(6, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(5, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_cursor_left_on_inverted_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 6, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_cursor_right_on_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, true); // left with shift
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(3, state.selection.base);
        assert_eq!(8, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(5, state.revision);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    #[test]
    fn test_type_empty_string() {
        let (mut ime, statechan, _actionchan) = set_up("", 0, 0);

        simulate_keypress(&mut ime, 'a', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("a", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, 'b', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!("ab", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_type_at_beginning() {
        let (mut ime, statechan, _actionchan) = set_up("cde", 0, 0);

        simulate_keypress(&mut ime, 'a', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("acde", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, 'b', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!("abcde", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_type_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 2, 5);

        simulate_keypress(&mut ime, 'x', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abxf", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_type_inverted_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 5, 2);

        simulate_keypress(&mut ime, 'x', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abxf", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_type_invalid_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", -10, 1);

        simulate_keypress(&mut ime, 'x', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("xbcdef", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }

    #[test]
    fn test_set_state() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 1, 1);

        let mut override_state = default_state();
        override_state.text = "meow?".to_string();
        override_state.selection.base = 4;
        override_state.selection.extent = 5;
        ime.set_state(override_state);
        simulate_keypress(&mut ime, '!', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("meow!", state.text);
        assert_eq!(5, state.selection.base);
        assert_eq!(5, state.selection.extent);
    }

    #[test]
    fn test_action() {
        let (mut ime, statechan, actionchan) = set_up("abcdef", 1, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_ENTER, true, false);
        assert!(statechan.try_recv().is_err()); // assert did not update state
        assert!(actionchan.try_recv().is_ok()); // assert DID send action
    }

    // TODO(lard): fix unicode support so this passes
    // disabled: #[test]
    #[allow(dead_code)]
    fn test_unicode_selection() {
        let (mut ime, statechan, _actionchan) = set_up("m😸eow", 1, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, true);
        assert!(statechan.try_recv().is_ok());

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, true);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("meow", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }

    // TODO(lard): fix unicode support so this passes
    // disabled: #[test]
    #[allow(dead_code)]
    fn test_unicode_backspace() {
        let (mut ime, statechan, _actionchan) = set_up("m😸eow", 2, 2);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, true);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("meow", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }
}
