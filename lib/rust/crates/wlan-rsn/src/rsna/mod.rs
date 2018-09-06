// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::akm::Akm;
use bytes::Bytes;
use crate::cipher::{Cipher, TKIP, GROUP_CIPHER_SUITE};
use eapol;
use failure::{self, bail, ensure};
use crate::Error;
use crate::rsne::Rsne;
use crate::key::{gtk::Gtk, ptk::Ptk, exchange::Key};

pub mod esssa;
#[cfg(test)]
pub mod test_util;

#[derive(Debug, Clone, PartialEq)]
pub struct NegotiatedRsne {
    pub group_data: Cipher,
    pub pairwise: Cipher,
    pub akm: Akm,
    pub mic_size: u16,
}

impl NegotiatedRsne {

    pub fn from_rsne(rsne: &Rsne) -> Result<NegotiatedRsne, failure::Error> {
        ensure!(rsne.group_data_cipher_suite.is_some(), Error::InvalidNegotiatedRsne);
        let group_data = rsne.group_data_cipher_suite.as_ref().unwrap();

        ensure!(rsne.pairwise_cipher_suites.len() == 1, Error::InvalidNegotiatedRsne);
        let pairwise = &rsne.pairwise_cipher_suites[0];

        ensure!(rsne.akm_suites.len() == 1, Error::InvalidNegotiatedRsne);
        let akm = &rsne.akm_suites[0];

        let mic_size = akm.mic_bytes();
        ensure!(mic_size.is_some(), Error::InvalidNegotiatedRsne);
        let mic_size = mic_size.unwrap();

        Ok(NegotiatedRsne{
            group_data: group_data.clone(),
            pairwise: pairwise.clone(),
            akm: akm.clone(),
            mic_size,
        })
    }

    pub fn to_full_rsne(&self) -> Rsne {
        let mut s_rsne = Rsne::new();
        s_rsne.group_data_cipher_suite = Some(self.group_data.clone());
        s_rsne.pairwise_cipher_suites = vec![self.pairwise.clone()];
        s_rsne.akm_suites = vec![self.akm.clone()];
        s_rsne
    }
}

// EAPOL Key frames carried in this struct comply with IEEE Std 802.11-2016, 12.7.2.
#[derive(Debug, Clone, PartialEq)]
pub struct VerifiedKeyFrame<'a> {
    frame: &'a eapol::KeyFrame,
    kd_plaintext: Bytes,
}

impl <'a> VerifiedKeyFrame<'a> {

    pub fn from_key_frame(
        frame: &'a eapol::KeyFrame,
        role: &Role,
        rsne: &NegotiatedRsne,
        key_replay_counter: u64,
        ptk: Option<&Ptk>,
        gtk: Option<&Gtk>
    ) -> Result<VerifiedKeyFrame<'a>, failure::Error> {
        let sender = match role {
            Role::Supplicant => Role::Authenticator,
            Role::Authenticator => Role::Supplicant,
        };

        // IEEE Std 802.11-2016, 12.7.2 a)
        // IEEE Std 802.1X-2010, 11.9
        let key_descriptor = match eapol::KeyDescriptor::from_u8(frame.descriptor_type) {
            Some(eapol::KeyDescriptor::Ieee802dot11) => eapol::KeyDescriptor::Ieee802dot11,
            // Use of RC4 is deprecated.
            Some(_) => bail!(Error::InvalidKeyDescriptor(
                frame.descriptor_type,
                eapol::KeyDescriptor::Ieee802dot11,
            )),
            // Invalid value.
            None => bail!(Error::UnsupportedKeyDescriptor(frame.descriptor_type)),
        };


        // IEEE Std 802.11-2016, 12.7.2 b.1)
        let expected_version = derive_key_descriptor_version(key_descriptor, rsne);
        ensure!(frame.key_info.key_descriptor_version() == expected_version,
                Error::UnsupportedKeyDescriptorVersion(frame.key_info.key_descriptor_version()));

        // IEEE Std 802.11-2016, 12.7.2 b.2)
        // IEEE Std 802.11-2016, 12.7.2 b.4)
        match frame.key_info.key_type() {
            eapol::KEY_TYPE_PAIRWISE => {},
            eapol::KEY_TYPE_GROUP_SMK => {
                // IEEE Std 802.11-2016, 12.7.2 b.4 ii)
                ensure!(!frame.key_info.install(), Error::InvalidInstallBitGroupSmkHandshake);
            },
            _ => bail!(Error::UnsupportedKeyDerivation),
        };

        // IEEE Std 802.11-2016, 12.7.2 b.5)
        if let Role::Supplicant = sender {
            ensure!(!frame.key_info.key_ack(), Error::InvalidKeyAckBitSupplicant);
        }

        // IEEE Std 802.11-2016, 12.7.2 b.6)
        // MIC is validated at the end once all other basic validations succeeded.

        // IEEE Std 802.11-2016, 12.7.2 b.7)
        if frame.key_info.secure() {
            let ptk_established = ptk.map_or(false, |_| true);
            let gtk_established = gtk.map_or(false, |_| true);

            match sender {
                // Frames sent by the Authenticator must not have the secure bit set before the
                // Supplicant *can derive* the PTK and GTK, which allows the Authenticator to send
                // "unsecured" frames after the PTK was derived but before the GTK was received.
                // Because the 4-Way Handshake is the only supported method for PTK and GTK
                // derivation so far and no known key exchange method sends such "unsecured" frames
                // in between PTK and GTK derivation, we can relax IEEE's assumption and require the
                // secure bit to only be set if at least the PTK was derived.
                Role::Authenticator => ensure!(ptk_established, Error::SecureBitWithUnknownPtk),
                // Frames sent by Supplicant must have the secure bit set once PTKSA and GTKSA are
                // established.
                Role::Supplicant => ensure!(ptk_established && gtk_established,
                                            Error::SecureBitNotSetWithKnownPtkGtk),
            };
        }

        // IEEE Std 802.11-2016, 12.7.2 b.8)
        if let Role::Authenticator = sender {
            ensure!(!frame.key_info.error(), Error::InvalidErrorBitAuthenticator);
        }

        // IEEE Std 802.11-2016, 12.7.2 b.9)
        if let Role::Authenticator = sender {
            ensure!(!frame.key_info.request(), Error::InvalidRequestBitAuthenticator);
        }

        // IEEE Std 802.11-2016, 12.7.2 b.10)
        // Encrypted key data is validated at the end once all other validations succeeded.

        // IEEE Std 802.11-2016, 12.7.2 b.11)
        ensure!(!frame.key_info.smk_message(), Error::SmkHandshakeNotSupported);

        // IEEE Std 802.11-2016, 12.7.2 c)
        match sender {
            // Supplicant always uses a key length of 0.
            Role::Supplicant if frame.key_len != 0 => {
                bail!(Error::InvalidKeyLength(frame.key_len, 0))
            },
            // Authenticator must use the pairwise cipher's key length.
            Role::Authenticator => match frame.key_info.key_type() {
                eapol::KEY_TYPE_PAIRWISE => {
                    let tk_bits = rsne.pairwise.tk_bits().ok_or(Error::UnsupportedCipherSuite)?;
                    if frame.key_len != tk_bits / 8 {
                        bail!(Error::InvalidKeyLength(frame.key_len, tk_bits / 8))
                    }
                },
                // IEEE Std 802.11-2016, 12.7.2 c) conflicts with IEEE Std 802.11-2016, 12.7.7.2
                // such that latter one requires the key length to be set to 0, while former is
                // to vague to derive any key type specific requirements.
                // Thus, leave it to the group key exchange method to enforce its requirements.
                eapol::KEY_TYPE_GROUP_SMK => {},
                _ => bail!(Error::UnsupportedKeyDerivation),
            },
            _ => {}
        };

        // IEEE Std 802.11-2016, 12.7.2, d)
        if key_replay_counter > 0 {
            ensure!(frame.key_replay_counter > key_replay_counter,
                    Error::InvalidKeyReplayCounter(frame.key_replay_counter, key_replay_counter));
        }

        // IEEE Std 802.11-2016, 12.7.2, e)
        // Validation is specific for the selected key exchange method.

        // IEEE Std 802.11-2016, 12.7.2, f)
        // Validation is specific for the selected key exchange method.

        // IEEE Std 802.11-2016, 12.7.2, g)
        // Validation is specific for the selected key exchange method.

        // IEEE Std 802.11-2016, 12.7.2 h)
        // IEEE Std 802.11-2016, 12.7.2 b.6)
        let mic_bytes = rsne.akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)?;
        ensure!(frame.key_mic.len() == mic_bytes as usize, Error::InvalidMicSize);

        if frame.key_info.key_mic() {
            // If a MIC is set but the PTK was not yet derived, the MIC cannot be verified.
            match ptk {
                // Verify MIC if PTK was derived.
                Some(ptk) => {
                    let mut buf = Vec::with_capacity(frame.len());
                    frame.as_bytes(true, &mut buf);
                    let valid_mic = rsne.akm
                        .integrity_algorithm()
                        .ok_or(Error::UnsupportedAkmSuite)?
                        .verify(ptk.kck(), &buf[..], &frame.key_mic[..]);
                    ensure!(valid_mic, Error::InvalidMic);
                },
                // If a MIC is set but the PTK was not yet derived, the MIC cannot be verified.
                None => bail!(Error::UnexpectedMic),
            };
        }

        // IEEE Std 802.11-2016, 12.7.2 i) & j)
        // IEEE Std 802.11-2016, 12.7.2 b.10)
        ensure!(frame.key_data_len as usize == frame.key_data.len(), Error::InvalidKeyDataLength);
        let kd_plaintext: Bytes;
        if frame.key_info.encrypted_key_data() {
            kd_plaintext = Bytes::from(match ptk {
                Some(ptk) => {
                    rsne.akm.keywrap_algorithm()
                        .ok_or(Error::UnsupportedAkmSuite)?
                        .unwrap(ptk.kek(), &frame.key_data[..])?
                },
                None => bail!(Error::UnexpectedEncryptedKeyData),
            });
        } else {
            kd_plaintext = Bytes::from(&frame.key_data[..]);
        }

        Ok(VerifiedKeyFrame{frame, kd_plaintext})
    }

    pub fn get(&self) -> &'a eapol::KeyFrame {
        self.frame
    }

    pub fn key_data_plaintext(&self) -> &[u8] {
        &self.kd_plaintext[..]
    }
}

// IEEE Std 802.11-2016, 12.7.2 b.1)
// Key Descriptor Version is based on the negotiated AKM, Pairwise- and Group Cipher suite.
fn derive_key_descriptor_version(key_descriptor_type: eapol::KeyDescriptor, rsne: &NegotiatedRsne)
    -> u16
{
    let akm = &rsne.akm;
    let pairwise = &rsne.pairwise;

    if !akm.has_known_algorithm() || !pairwise.has_known_usage() {
        return 0;
    }

    match akm.suite_type {
        1 | 2 => match key_descriptor_type {
            eapol::KeyDescriptor::Rc4 => match pairwise.suite_type {
                TKIP | GROUP_CIPHER_SUITE => 1,
                _ => 0,
            },
            eapol::KeyDescriptor::Ieee802dot11  if pairwise.is_enhanced()
                || rsne.group_data.is_enhanced() => {
                2
            }
            _ => 0,
        },
        // Interestingly, IEEE 802.11 does not specify any pairwise- or group cipher
        // requirements for these AKMs.
        3...6 => 3,
        _ => 0,
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum Role {
    Authenticator,
    Supplicant,
}

#[derive(Debug, PartialEq)]
pub enum SecAssocStatus {
    // TODO(hahnr): Rather than reporting wrong password as a status, report it as an error.
    WrongPassword,
    EssSaEstablished,
}

#[derive(Debug, PartialEq)]
pub enum SecAssocUpdate {
    TxEapolKeyFrame(eapol::KeyFrame),
    Key(Key),
    Status(SecAssocStatus),
}

pub type SecAssocResult = Result<Vec<SecAssocUpdate>, failure::Error>;

#[cfg(test)]
mod tests {
    use super::*;
    use bytes::Bytes;
    use crate::akm;
    use crate::cipher;
    use crate::rsne::Rsne;
    use crate::suite_selector::OUI;

    #[test]
    fn test_negotiated_rsne_from_rsne() {
        let rsne = make_rsne(Some(cipher::GCMP_256), vec![cipher::CCMP_128], vec![akm::PSK]);
        NegotiatedRsne::from_rsne(&rsne).expect("error, could not create negotiated RSNE");

        let rsne = make_rsne(None, vec![cipher::CCMP_128], vec![akm::PSK]);
        NegotiatedRsne::from_rsne(&rsne).expect_err("error, created negotiated RSNE");

        let rsne = make_rsne(Some(cipher::CCMP_128), vec![], vec![akm::PSK]);
        NegotiatedRsne::from_rsne(&rsne).expect_err("error, created negotiated RSNE");

        let rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![]);
        NegotiatedRsne::from_rsne(&rsne).expect_err("error, created negotiated RSNE");
    }

    #[test]
    fn test_to_rsne() {
        let rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let negotiated_rsne = NegotiatedRsne::from_rsne(&rsne)
            .expect("error, could not create negotiated RSNE")
            .to_full_rsne();
       assert_eq!(negotiated_rsne, rsne);
    }

    fn make_cipher(suite_type: u8) -> cipher::Cipher {
        cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type }
    }

    fn make_akm(suite_type: u8) -> akm::Akm {
        akm::Akm { oui: Bytes::from(&OUI[..]), suite_type }
    }

    fn make_rsne(data: Option<u8>, pairwise: Vec<u8>, akms: Vec<u8>) -> Rsne {
        let mut rsne = Rsne::new();
        rsne.group_data_cipher_suite = data.map(make_cipher);
        rsne.pairwise_cipher_suites = pairwise.into_iter().map(make_cipher).collect();
        rsne.akm_suites = akms.into_iter().map(make_akm).collect();
        rsne
    }

}
