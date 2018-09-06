use bytes::Bytes;
use failure::{bail, ensure, format_err};
use fidl_fuchsia_wlan_mlme::BssDescription;
use wlan_rsn::{akm, auth, cipher, rsne::{self, Rsne}, suite_selector::OUI};
use wlan_rsn::key::exchange;
use wlan_rsn::rsna::{esssa::EssSa, NegotiatedRsne, Role};

use crate::DeviceInfo;

#[derive(Debug, PartialEq)]
pub struct Rsna {
    pub negotiated_rsne: NegotiatedRsne,
    pub esssa: EssSa,
}

/// Supported Ciphers and AKMs:
/// Group Data Ciphers: CCMP-128, TKIP
/// Pairwise Cipher: CCMP-128
/// AKM: PSK
pub fn is_rsn_compatible(a_rsne: &Rsne) -> bool {
    let has_supported_group_data_cipher = match a_rsne.group_data_cipher_suite.as_ref() {
        Some(c) if c.has_known_usage() => match c.suite_type {
            // IEEE allows TKIP usage only in GTKSAs for compatibility reasons.
            // TKIP is considered broken and should never be used in a PTKSA or IGTKSA.
            cipher::CCMP_128 | cipher::TKIP => true,
            _ => false,
        },
        _ => false,
    };
    let has_supported_pairwise_cipher = a_rsne.pairwise_cipher_suites.iter()
        .any(|c| c.has_known_usage() && c.suite_type == cipher::CCMP_128);
    let has_supported_akm_suite = a_rsne.akm_suites.iter()
        .any(|a| a.has_known_algorithm() && a.suite_type == akm::PSK);

    has_supported_group_data_cipher && has_supported_pairwise_cipher && has_supported_akm_suite
}

pub fn get_rsna(device_info: &DeviceInfo, password: &[u8], bss: &BssDescription)
    -> Result<Option<Box<Rsna>>, failure::Error>
{
    ensure!(bss.rsn.is_none() == password.is_empty(), "invalid password param for BSS");
    let a_rsne_bytes = match &bss.rsn {
        None => return Ok(None),
        Some(x) => x
    };
    let a_rsne = rsne::from_bytes(&a_rsne_bytes[..]).to_full_result()
        .map_err(|e| format_err!("invalid RSNE {:?}: {:?}", &a_rsne_bytes[..], e))?;
    let s_rsne = derive_s_rsne(&a_rsne)?;
    let esssa = make_ess_sa(bss.ssid.as_bytes(), &password[..], device_info.addr,
                            s_rsne.clone(), bss.bssid, a_rsne)
        .map_err(|e| format_err!("failed to create ESS-SA: {:?}", e))?;
    let negotiated_rsne = NegotiatedRsne::from_rsne(&s_rsne)?;
    Ok(Some(Box::new(Rsna { negotiated_rsne, esssa })))
}

fn make_ess_sa(ssid: &[u8], passphrase: &[u8], sta_addr: [u8; 6], sta_rsne: Rsne, bssid: [u8; 6],
               bss_rsne: Rsne)
    -> Result<EssSa, failure::Error>
{
    let negotiated_rsne = NegotiatedRsne::from_rsne(&sta_rsne)?;
    let auth_cfg = auth::Config::for_psk(passphrase, ssid)?;
    let ptk_cfg = exchange::Config::for_4way_handshake(Role::Supplicant, sta_addr, sta_rsne, bssid, bss_rsne)?;
    let gtk_cfg = exchange::Config::for_groupkey_handshake(Role::Supplicant, negotiated_rsne.akm.clone());
    EssSa::new(Role::Supplicant, negotiated_rsne, auth_cfg, ptk_cfg, gtk_cfg)
}

/// Constructs Supplicant's RSNE with:
/// Group Data Cipher: same as A-RSNE (CCMP-128 or TKIP)
/// Pairwise Cipher: CCMP-128
/// AKM: PSK
fn derive_s_rsne(a_rsne: &Rsne) -> Result<Rsne, failure::Error> {
    if !is_rsn_compatible(&a_rsne) {
        bail!("incompatible RSNE {:?}", a_rsne);
    }

    // If Authenticator's RSNE is supported, construct Supplicant's RSNE.
    let mut s_rsne = Rsne::new();
    s_rsne.group_data_cipher_suite = a_rsne.group_data_cipher_suite.clone();
    let pairwise_cipher =
        cipher::Cipher{oui: Bytes::from(&OUI[..]), suite_type: cipher::CCMP_128 };
    s_rsne.pairwise_cipher_suites.push(pairwise_cipher);
    let akm = akm::Akm{oui: Bytes::from(&OUI[..]), suite_type: akm::PSK };
    s_rsne.akm_suites.push(akm);
    Ok(s_rsne)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::client::test_utils::{fake_protected_bss_description, fake_unprotected_bss_description};
    use crate::client::test_utils::{make_rsne, rsne_as_bytes};
    use std::collections::HashSet;

    const CLIENT_ADDR: [u8; 6] = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];

    #[test]
    fn test_incompatible_group_data_cipher() {
        let a_rsne = make_rsne(Some(cipher::GCMP_256), vec![cipher::CCMP_128], vec![akm::PSK]);
        assert_eq!(is_rsn_compatible(&a_rsne), false);
    }

    #[test]
    fn test_incompatible_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::BIP_CMAC_256], vec![akm::PSK]);
        assert_eq!(is_rsn_compatible(&a_rsne), false);
    }

    #[test]
    fn test_tkip_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::TKIP], vec![akm::PSK]);
        assert_eq!(is_rsn_compatible(&a_rsne), false);
    }

    #[test]
    fn test_tkip_group_data_cipher() {
        let a_rsne = make_rsne(Some(cipher::TKIP), vec![cipher::CCMP_128], vec![akm::PSK]);
        assert_eq!(is_rsn_compatible(&a_rsne), true);

        let s_rsne = derive_s_rsne(&a_rsne).unwrap();
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 2, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne), expected_rsne_bytes);
    }

    #[test]
    fn test_ccmp128_group_data_pairwise_cipher_psk() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        assert_eq!(is_rsn_compatible(&a_rsne), true);

        let s_rsne = derive_s_rsne(&a_rsne).unwrap();
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne), expected_rsne_bytes);
    }

    #[test]
    fn test_mixed_mode() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128, cipher::TKIP], vec![akm::PSK, akm::FT_PSK]);
        assert_eq!(is_rsn_compatible(&a_rsne), true);

        let s_rsne = derive_s_rsne(&a_rsne).unwrap();
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne), expected_rsne_bytes);
    }

    #[test]
    fn test_no_group_data_cipher() {
        let a_rsne = make_rsne(None, vec![cipher::CCMP_128], vec![akm::PSK]);
        assert_eq!(is_rsn_compatible(&a_rsne), false);
    }

    #[test]
    fn test_no_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![], vec![akm::PSK]);
        assert_eq!(is_rsn_compatible(&a_rsne), false);
    }

    #[test]
    fn test_no_akm() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![]);
        assert_eq!(is_rsn_compatible(&a_rsne), false);
    }

    #[test]
    fn test_incompatible_akm() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::EAP]);
        assert_eq!(is_rsn_compatible(&a_rsne), false);
    }

    #[test]
    fn test_get_rsna_password_for_unprotected_network() {
        let bss = fake_unprotected_bss_description(b"foo_bss".to_vec());
        let rsna = get_rsna(&make_device_info(), "somepass".as_bytes(), &bss);
        assert!(rsna.is_err(), "expect error when password is supplied for unprotected network")
    }

    #[test]
    fn test_get_rsna_no_password_for_protected_network() {
        let bss = fake_protected_bss_description(b"foo_bss".to_vec());
        let rsna = get_rsna(&make_device_info(), "".as_bytes(), &bss);
        assert!(rsna.is_err(), "expect error when no password is supplied for protected network")
    }

    fn make_device_info() -> DeviceInfo {
        DeviceInfo {
            supported_channels: HashSet::new(),
            addr: CLIENT_ADDR,
        }
    }
}