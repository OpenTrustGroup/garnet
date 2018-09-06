// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use crate::integrity;
use crate::Error;
use eapol;
use failure::{self, bail};
use crate::key::gtk::Gtk;
use crate::key::exchange::{Key, handshake::{group_key::{self, Config, GroupKeyHandshakeFrame}}};
use crate::key_data;
use crate::rsna::{SecAssocResult, SecAssocUpdate};

#[derive(Debug, PartialEq)]
pub struct Supplicant {
    pub cfg: group_key::Config,
    pub kck: Bytes,
}

impl Supplicant {
    // IEEE Std 802.11-2016, 12.7.7.2
    pub fn on_eapol_key_frame(&mut self, msg1: GroupKeyHandshakeFrame) -> SecAssocResult {
        // Extract GTK from data.
        let mut gtk: Option<key_data::kde::Gtk> = None;
        let elements = key_data::extract_elements(&msg1.key_data_plaintext()[..])?;
        for ele in elements {
            match ele {
                key_data::Element::Gtk(_, e) => gtk = Some(e),
                _ => {},
            }
        }
        let gtk = match gtk {
            None => bail!("GTK KDE not present in key data of Group-Key Handshakes's 1st message"),
            Some(gtk) => Gtk::from_gtk(gtk.gtk, gtk.info.key_id()),
        };

        // Construct second message of handshake.
        let msg2 = self.create_message_2(msg1.get())?;

        Ok(vec![
            SecAssocUpdate::TxEapolKeyFrame(msg2),
            SecAssocUpdate::Key(Key::Gtk(gtk)),
        ])
    }

    // IEEE Std 802.11-2016, 12.7.7.3
    fn create_message_2(&self, msg1: &eapol::KeyFrame) -> Result<eapol::KeyFrame, failure::Error> {
        let mut key_info = eapol::KeyInformation(0);
        key_info.set_key_descriptor_version(msg1.key_info.key_descriptor_version());
        key_info.set_key_type(msg1.key_info.key_type());
        key_info.set_key_mic(true);
        key_info.set_secure(true);

        let mut msg2 = eapol::KeyFrame {
            version: msg1.version,
            packet_type: eapol::PacketType::Key as u8,
            packet_body_len: 0, // Updated afterwards
            descriptor_type: eapol::KeyDescriptor::Ieee802dot11 as u8,
            key_info: key_info,
            key_len: 0,
            key_replay_counter: msg1.key_replay_counter,
            key_nonce: [0u8; 32],
            key_iv: [0u8; 16],
            key_rsc: 0,
            key_mic: Bytes::from(vec![0u8; msg1.key_mic.len()]),
            key_data_len: 0,
            key_data: Bytes::from(vec![]),
        };
        msg2.update_packet_body_len();

        // Update the frame's MIC.
        let akm = &self.cfg.akm;
        let integrity_alg = akm.integrity_algorithm().ok_or(Error::UnsupportedAkmSuite)?;
        let mic_len = akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)?;
        update_mic(&self.kck[..], mic_len, integrity_alg, &mut msg2)?;

        Ok(msg2)
    }

    pub fn destroy(self) -> Config {
        self.cfg
    }
}

fn update_mic(kck: &[u8], mic_len: u16, alg: Box<integrity::Algorithm>, frame: &mut eapol::KeyFrame)
    -> Result<(), failure::Error>
{
    let mut buf = Vec::with_capacity(frame.len());
    frame.as_bytes(true, &mut buf);
    let written = buf.len();
    buf.truncate(written);
    let mic = alg.compute(kck, &buf[..])?;
    frame.key_mic = Bytes::from(&mic[..mic_len as usize]);
    Ok(())
}
