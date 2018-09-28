// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Elliptic Curve-based cryptographic algorithms.

mod curve;

pub use public::ec::curve::{Curve, P256, P384, P521};

use boringssl::{CHeapWrapper, CStackWrapper};
use public::ec::curve::CurveKind;
use public::ec::inner::EcKey;
use public::{inner::DerKey, DerPrivateKey, DerPublicKey, PrivateKey, PublicKey};
use util::Sealed;
use Error;

mod inner {
    use std::marker::PhantomData;

    use boringssl::{self, BoringError, CHeapWrapper, CStackWrapper};
    use public::ec::curve::Curve;
    use public::inner::BoringDerKey;
    use Error;

    // A convenience wrapper around boringssl::EC_KEY.
    //
    // EcKey maintains the following invariants:
    // - The key is valid.
    // - The key is on the curve C.
    //
    // This is marked pub and put in this (non-public) module so that using it in impls of
    // the Key trait don't result in public-in-private errors.
    #[derive(Clone)]
    pub struct EcKey<C: Curve> {
        pub key: CHeapWrapper<boringssl::EC_KEY>,
        _marker: PhantomData<C>,
    }

    impl<C: Curve> EcKey<C> {
        pub fn generate() -> Result<EcKey<C>, BoringError> {
            let mut key = CHeapWrapper::default();
            // EC_KEY_set_group only errors if there's already a group set
            key.ec_key_set_group(&C::group()).unwrap();
            key.ec_key_generate_key()?;
            Ok(EcKey {
                key,
                _marker: PhantomData,
            })
        }

        /// Creates an `EcKey` from a BoringSSL `EC_KEY`.
        ///
        /// `from_EC_KEY` validates that `key`'s curve is `C`.
        ///
        /// # Panics
        ///
        /// `from_EC_KEY` panics if `key`'s group is not set.
        #[allow(non_snake_case)]
        pub fn from_EC_KEY(key: CHeapWrapper<boringssl::EC_KEY>) -> Result<EcKey<C>, Error> {
            // ec_key_get0_group returns the EC_KEY's internal group pointer,
            // which is guaranteed to be set by the caller.
            C::validate_group(key.ec_key_get0_group().unwrap())?;
            Ok(EcKey {
                key,
                _marker: PhantomData,
            })
        }
    }

    impl<C: Curve> BoringDerKey for EcKey<C> {
        fn pkey_assign(&self, pkey: &mut CHeapWrapper<boringssl::EVP_PKEY>) {
            pkey.evp_pkey_assign_ec_key(self.key.clone())
        }

        // NOTE: panics if the key is an EC key and doesn't have a group set
        // (due to EcKey::from_EC_KEY)
        fn pkey_get(pkey: &mut CHeapWrapper<boringssl::EVP_PKEY>) -> Result<Self, Error> {
            let key = pkey.evp_pkey_get1_ec_key()?;
            EcKey::from_EC_KEY(key)
        }

        fn parse_private_key(cbs: &mut CStackWrapper<boringssl::CBS>) -> Result<EcKey<C>, Error> {
            // The last argument is a group. If it's not None, then it is either
            // used as the group or, if the DER encoding also contains a group,
            // the encoded group is validated against the group passed as an
            // argument. Note that this validation is mostly redundant - similar
            // validation is performed in EcKey::from_EC_KEY - however, it's not
            // fully redundant, since it allows keys to be parsed which have no
            // group.
            let key = CHeapWrapper::ec_key_parse_private_key(cbs, Some(C::group()))?;
            EcKey::from_EC_KEY(key)
        }

        fn marshal_private_key(
            &self, cbb: &mut CStackWrapper<boringssl::CBB>,
        ) -> Result<(), Error> {
            self.key.ec_key_marshal_private_key(cbb).map_err(From::from)
        }
    }

    #[cfg(test)]
    mod tests {
        use std::mem;

        use super::*;
        use public::ec::{P256, P384, P521};

        #[test]
        fn test_refcount() {
            fn test<C: Curve>() {
                let key = EcKey::<C>::generate().unwrap();
                for i in 0..8 {
                    // make i clones and then free them all
                    let mut keys = Vec::new();
                    for _ in 0..i {
                        keys.push(key.clone());
                    }
                    mem::drop(keys);
                }
                mem::drop(key);
            }

            test::<P256>();
            test::<P384>();
            test::<P521>();
        }
    }
}

/// An elliptic curve public key.
///
/// `EcPubKey` is a public key over the curve `C`.
pub struct EcPubKey<C: Curve> {
    inner: EcKey<C>,
}

impl<C: Curve> Sealed for EcPubKey<C> {}
impl<C: Curve> DerPublicKey for EcPubKey<C> {}

impl<C: Curve> DerKey for EcPubKey<C> {
    type Boring = EcKey<C>;
    fn get_boring(&self) -> &EcKey<C> {
        &self.inner
    }
    fn from_boring(inner: EcKey<C>) -> EcPubKey<C> {
        EcPubKey { inner }
    }
}

impl<C: Curve> PublicKey for EcPubKey<C> {
    type Private = EcPrivKey<C>;
}

/// An elliptic curve private key.
///
/// `EcPrivKey` is a private key over the curve `C`.
pub struct EcPrivKey<C: Curve> {
    inner: EcKey<C>,
}

impl<C: Curve> EcPrivKey<C> {
    /// Generates a new private key.
    #[must_use]
    pub fn generate() -> Result<EcPrivKey<C>, Error> {
        Ok(EcPrivKey {
            inner: EcKey::generate()?,
        })
    }
}

impl<C: Curve> Sealed for EcPrivKey<C> {}
impl<C: Curve> DerPrivateKey for EcPrivKey<C> {}

impl<C: Curve> DerKey for EcPrivKey<C> {
    type Boring = EcKey<C>;
    fn get_boring(&self) -> &EcKey<C> {
        &self.inner
    }
    fn from_boring(inner: EcKey<C>) -> EcPrivKey<C> {
        EcPrivKey { inner }
    }
}

impl<C: Curve> PrivateKey for EcPrivKey<C> {
    type Public = EcPubKey<C>;

    fn public(&self) -> EcPubKey<C> {
        EcPubKey {
            inner: self.inner.clone(),
        }
    }
}

/// An elliptic curve public key whose curve is unknown at compile time.
///
/// An `EcPubKeyAnyCurve` is an enum of `EcPubKey`s over the three supported
/// curves. It is returned from [`parse_public_key_der_any_curve`].
#[must_use]
#[allow(missing_docs)]
pub enum EcPubKeyAnyCurve {
    P256(EcPubKey<P256>),
    P384(EcPubKey<P384>),
    P521(EcPubKey<P521>),
}

/// An elliptic curve private key whose curve is unknown at compile time.
///
/// An `EcPrivKeyAnyCurve` is an enum of `EcPrivKey`s over the three supported
/// curves. It is returned from [`parse_private_key_der_any_curve`].
#[must_use]
#[allow(missing_docs)]
pub enum EcPrivKeyAnyCurve {
    P256(EcPrivKey<P256>),
    P384(EcPrivKey<P384>),
    P521(EcPrivKey<P521>),
}

impl EcPrivKeyAnyCurve {
    /// Gets the public key corresponding to this private key.
    pub fn public(&self) -> EcPubKeyAnyCurve {
        match self {
            EcPrivKeyAnyCurve::P256(key) => EcPubKeyAnyCurve::P256(key.public()),
            EcPrivKeyAnyCurve::P384(key) => EcPubKeyAnyCurve::P384(key.public()),
            EcPrivKeyAnyCurve::P521(key) => EcPubKeyAnyCurve::P521(key.public()),
        }
    }
}

/// Parses a public key in DER format with any curve.
///
/// `parse_public_key_der_any_curve` is like [`::public::parse_public_key_der`],
/// but it accepts any [`Curve`] rather than a particular, static curve.
///
/// Since `parse_public_key_der` takes a `PublicKey` type argument, and
/// [`EcPubKey`] requires a static [`Curve`] type parameter,
/// `parse_public_key_der` can only be called when the curve is known ahead of
/// time. `parse_public_key_der_any_curve`, on the other hand, accepts any
/// curve. It returns an `EcPubKeyAnyCurve`, which is an enum of keys over the
/// three supported curves.
///
/// Because the curve is not known statically, one must be specified in the DER
/// input.
#[must_use]
pub fn parse_public_key_der_any_curve(bytes: &[u8]) -> Result<EcPubKeyAnyCurve, Error> {
    CStackWrapper::cbs_with_temp_buffer(bytes, |cbs| {
        let mut evp_pkey = CHeapWrapper::evp_parse_public_key(cbs)?;
        let key = evp_pkey.evp_pkey_get1_ec_key()?;
        if cbs.cbs_len() > 0 {
            return Err(Error::new("malformed DER input".to_string()));
        }

        // EVP_parse_public_key guarantees that the returned key has its group
        // set, so this unwrap is safe.
        let group = key.ec_key_get0_group().unwrap();
        Ok(
            match CurveKind::from_nid(group.ec_group_get_curve_name())? {
                CurveKind::P256 => EcPubKeyAnyCurve::P256(EcPubKey {
                    inner: EcKey::from_EC_KEY(key.clone())?,
                }),
                CurveKind::P384 => EcPubKeyAnyCurve::P384(EcPubKey {
                    inner: EcKey::from_EC_KEY(key.clone())?,
                }),
                CurveKind::P521 => EcPubKeyAnyCurve::P521(EcPubKey {
                    inner: EcKey::from_EC_KEY(key.clone())?,
                }),
            },
        )
    })
}

/// Parses a private key in DER format with any curve.
///
/// `parse_private_key_der_any_curve` is like
/// [`::public::parse_private_key_der`], but it accepts any [`Curve`] rather
/// than a particular, static curve.
///
/// Since `parse_private_key_der` takes a `PrivateKey` type argument, and
/// [`EcPrivKey`] requires a static [`Curve`] type parameter,
/// `parse_private_key_der` can only be called when the curve is known ahead of
/// time. `parse_private_key_der_any_curve`, on the other hand, accepts any
/// curve. It returns an `EcPrivKeyAnyCurve`, which is an enum of keys over the
/// three supported curves.
///
/// Because the curve is not known statically, one must be specified in the DER
/// input.
#[must_use]
pub fn parse_private_key_der_any_curve(bytes: &[u8]) -> Result<EcPrivKeyAnyCurve, Error> {
    CStackWrapper::cbs_with_temp_buffer(bytes, |cbs| {
        // The last argument is a group. Since it's None,
        // EC_KEY_parse_private_key will require the DER to name the group.
        let key = CHeapWrapper::ec_key_parse_private_key(cbs, None)?;
        if cbs.cbs_len() > 0 {
            return Err(Error::new("malformed DER input".to_string()));
        }

        // TODO(joshlf): Add documentation to EC_KEY_parse_private_key
        // guaranteeing that the internal group pointer is set.
        let group = key.ec_key_get0_group().unwrap();
        Ok(
            match CurveKind::from_nid(group.ec_group_get_curve_name())? {
                CurveKind::P256 => EcPrivKeyAnyCurve::P256(EcPrivKey {
                    inner: EcKey::from_EC_KEY(key.clone())?,
                }),
                CurveKind::P384 => EcPrivKeyAnyCurve::P384(EcPrivKey {
                    inner: EcKey::from_EC_KEY(key.clone())?,
                }),
                CurveKind::P521 => EcPrivKeyAnyCurve::P521(EcPrivKey {
                    inner: EcKey::from_EC_KEY(key.clone())?,
                }),
            },
        )
    })
}

/// The Elliptic Curve Digital Signature Algorithm.
pub mod ecdsa {
    use std::marker::PhantomData;

    use boringssl;
    use hash::{inner::Digest, Hasher, Sha256, Sha384};
    use public::{ec::{Curve, EcPrivKey, EcPubKey, P256, P384, P521},
                 Signature};
    use util::Sealed;
    use Error;

    /// A hash function which is compatible with ECDSA signatures over the curve
    /// `C`.
    ///
    /// An ECDSA signature is constructed by hashing the message and then
    /// signing the resulting digest. However, EC keys over certain curves may
    /// not be compatible with all hashes. In particular, some digests may be
    /// too long (in number of bytes) and thus not correspond to a point on the
    /// curve. `EcdsaHash<C>` is implemented by all hash functions whose digests
    /// are compatible with ECDSA signatures over the curve `C`.
    pub trait EcdsaHash<C: Curve>: Sealed {}

    impl EcdsaHash<P256> for Sha256 {}
    impl EcdsaHash<P384> for Sha256 {}
    impl EcdsaHash<P384> for Sha384 {}
    impl EcdsaHash<P521> for Sha256 {}
    impl EcdsaHash<P521> for Sha384 {}

    // The maximum length of an ECDSA signature over P-521. Since this isn't
    // exposed in the API, we can increase later if we add support for curves
    // with larger signatures.
    //
    // This was calculated with the following equation, which is thanks to
    // agl@google.com:
    //
    //  r = s = (521 + 7)/8              # Bytes to store the integers r and s
    //        = 66
    //  DER encoded bytes =   (1         # type byte 0x02
    //                      +  1         # length byte
    //                      +  1         # possible 0 padding
    //                      +  66) * 2   # one for each of r and s
    //                      +  1         # ASN.1 SEQUENCE type byte
    //                      +  2         # outer length
    //                    =    141
    const MAX_SIGNATURE_LEN: usize = 141;

    /// A DER-encoded ECDSA signature.
    #[must_use]
    pub struct EcdsaSignature<C: Curve, H: Hasher + EcdsaHash<C>> {
        bytes: [u8; MAX_SIGNATURE_LEN],
        // Invariant: len is in [0; MAX_SIGNATURE_LEN). If len is 0, it
        // indicates an invalid signature. Invalid signatures can be produced
        // when a caller invokes from_bytes with a byte slice longer than
        // MAX_SIGNATURE_LEN. Such signatures cannot possibly have been
        // generated by an ECDSA signature over any of the curves we support,
        // and so it could not possibly be valid. In other words, it would never
        // be correct for ecdsa_verify to return true when invoked on such a
        // signature.
        //
        // However, if we were to simply truncate the byte slice and store a
        // subset of it, then we might open ourselves up to attacks in which an
        // attacker induces a mismatch between the signature that the caller
        // /thinks/ is being verified and the signature that is /actually/ being
        // verified. Thus, it's important that we always reject such signatures.
        //
        // Finally, it's OK for us to use 0 as the sentinal value to mean
        // "invalid signature" because ECDSA can never produce a 0-byte
        // signature. Thus, we will never produce a 0-byte signature from
        // ecdsa_sign, and similarly, if the caller constructs a 0-byte
        // signature using from_bytes, it's correct for us to treat it as
        // invalid.
        len: usize,
        _marker: PhantomData<(C, H)>,
    }

    impl<C: Curve, H: Hasher + EcdsaHash<C>> EcdsaSignature<C, H> {
        /// Constructs an `EcdsaSignature` from raw bytes.
        #[must_use]
        pub fn from_bytes(bytes: &[u8]) -> EcdsaSignature<C, H> {
            if bytes.len() > MAX_SIGNATURE_LEN {
                // see comment on the len field for why we do this
                return Self::empty();
            }
            let mut ret = Self::empty();
            (&mut ret.bytes[..bytes.len()]).copy_from_slice(bytes);
            ret.len = bytes.len();
            ret
        }

        /// Gets the raw bytes of this `EcdsaSignature`.
        #[must_use]
        pub fn bytes(&self) -> &[u8] {
            &self.bytes[..self.len]
        }

        fn is_valid(&self) -> bool {
            self.len != 0
        }

        fn empty() -> EcdsaSignature<C, H> {
            EcdsaSignature {
                bytes: [0u8; MAX_SIGNATURE_LEN],
                len: 0,
                _marker: PhantomData,
            }
        }
    }

    impl<C: Curve, H: Hasher + EcdsaHash<C>> Sealed for EcdsaSignature<C, H> {}
    impl<C: Curve, H: Hasher + EcdsaHash<C>> Signature for EcdsaSignature<C, H> {
        type PrivateKey = EcPrivKey<C>;

        fn sign(key: &EcPrivKey<C>, message: &[u8]) -> Result<EcdsaSignature<C, H>, Error> {
            let digest = H::hash(message);
            let mut sig = EcdsaSignature::empty();
            sig.len = boringssl::ecdsa_sign(digest.as_ref(), &mut sig.bytes[..], &key.inner.key)?;
            Ok(sig)
        }

        fn verify(&self, key: &EcPubKey<C>, message: &[u8]) -> bool {
            if !self.is_valid() {
                // see comment on EcdsaSignature::len for why we do this
                return false;
            }
            let digest = H::hash(message);
            boringssl::ecdsa_verify(digest.as_ref(), self.bytes(), &key.inner.key)
        }
    }

    #[cfg(test)]
    mod tests {
        use super::{super::*, *};

        #[test]
        fn test_smoke() {
            // Sign the message, verify the signature, and return the signature.
            // Also verify that, if the wrong signature is used, the signature
            // fails to verify. Also verify that EcdsaSignature::from_bytes
            // works.
            fn sign_and_verify<C: Curve, H: Hasher + EcdsaHash<C>>(
                key: &EcPrivKey<C>, message: &[u8],
            ) -> EcdsaSignature<C, H> {
                let sig = EcdsaSignature::<C, H>::sign(&key, message).unwrap();
                assert!(sig.verify(&key.public(), message));
                let sig2 = EcdsaSignature::<C, H>::sign(&key, sig.bytes()).unwrap();
                assert!(!sig2.verify(&key.public(), message));
                EcdsaSignature::from_bytes(sig.bytes())
            }

            let p256 = EcPrivKey::<P256>::generate().unwrap();
            let p384 = EcPrivKey::<P384>::generate().unwrap();
            let p521 = EcPrivKey::<P521>::generate().unwrap();

            // Sign an empty message, and verify the signature. Use the
            // signature as the next message to test, and repeat many times.
            let mut msg = Vec::new();
            for _ in 0..4 {
                msg = sign_and_verify::<_, Sha256>(&p256, &msg).bytes().to_vec();
                msg = sign_and_verify::<_, Sha256>(&p384, &msg).bytes().to_vec();
                msg = sign_and_verify::<_, Sha384>(&p384, &msg).bytes().to_vec();
                msg = sign_and_verify::<_, Sha256>(&p521, &msg).bytes().to_vec();
                msg = sign_and_verify::<_, Sha384>(&p521, &msg).bytes().to_vec();
            }
        }

        #[test]
        fn test_invalid_signature() {
            fn test_is_invalid(sig: &EcdsaSignature<P256, Sha256>) {
                assert_eq!(sig.len, 0);
                assert!(!sig.is_valid());
                assert!(!sig.verify(&EcPrivKey::<P256>::generate().unwrap().public(), &[],));
            }
            test_is_invalid(&EcdsaSignature::from_bytes(&[0; MAX_SIGNATURE_LEN + 1]));
            test_is_invalid(&EcdsaSignature::from_bytes(&[]));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hash::Sha256;
    use public::ec::ecdsa::*;
    use public::{marshal_private_key_der, marshal_public_key_der, parse_private_key_der,
                 parse_public_key_der, Signature};
    use util::should_fail;

    #[test]
    fn test_generate() {
        EcPrivKey::<P256>::generate().unwrap();
        EcPrivKey::<P384>::generate().unwrap();
        EcPrivKey::<P521>::generate().unwrap();
    }

    #[test]
    fn test_marshal_parse() {
        // Test various combinations of parsing and serializing keys.
        //
        // Since we need to test dynamic parsing (the
        // parse_private_key_der_any_curve and parse_public_key_der_any_curve
        // functions), we need a way of unwrapping their return values into a
        // static key type. Unfortunately, there's no way (on stable Rust) to do
        // that generically, so the caller must pass a function which will do
        // it.
        fn test<
            C: Curve,
            F: Fn(EcPrivKeyAnyCurve) -> EcPrivKey<C>,
            G: Fn(EcPubKeyAnyCurve) -> EcPubKey<C>,
        >(
            unwrap_priv_any: F, unwrap_pub_any: G,
        ) where
            Sha256: EcdsaHash<C>,
        {
            const MESSAGE: &[u8] = &[0, 1, 2, 3, 4, 5, 6, 7];
            let key = EcPrivKey::<C>::generate().unwrap();

            let parsed_key: EcPrivKey<C> =
                parse_private_key_der(&marshal_private_key_der(&key)).unwrap();
            let parsed_key_any_curve = unwrap_priv_any(
                parse_private_key_der_any_curve(&marshal_private_key_der(&key)).unwrap(),
            );
            let pubkey = key.public();
            let parsed_pubkey: EcPubKey<C> =
                parse_public_key_der(&marshal_public_key_der(&pubkey)).unwrap();
            let parsed_pubkey_any_curve = unwrap_pub_any(
                parse_public_key_der_any_curve(&marshal_public_key_der(&pubkey)).unwrap(),
            );

            fn sign_and_verify<C1: Curve, C2: Curve>(privkey: &EcPrivKey<C1>, pubkey: &EcPubKey<C2>)
            where
                Sha256: EcdsaHash<C1>,
                Sha256: EcdsaHash<C2>,
            {
                let sig = EcdsaSignature::<C1, Sha256>::sign(&privkey, MESSAGE).unwrap();
                assert!(
                    EcdsaSignature::<C2, Sha256>::from_bytes(sig.bytes()).verify(&pubkey, MESSAGE)
                )
            }

            // Sign and verify with every pair of keys to make sure we parsed
            // the same key we marshaled.
            sign_and_verify(&key, &pubkey);
            sign_and_verify(&key, &parsed_pubkey);
            sign_and_verify(&key, &parsed_pubkey_any_curve);
            sign_and_verify(&parsed_key, &pubkey);
            sign_and_verify(&parsed_key, &parsed_pubkey);
            sign_and_verify(&parsed_key, &parsed_pubkey_any_curve);
            sign_and_verify(&parsed_key_any_curve, &pubkey);
            sign_and_verify(&parsed_key_any_curve, &parsed_pubkey);
            sign_and_verify(&parsed_key_any_curve, &parsed_pubkey_any_curve);

            let _ = marshal_public_key_der::<EcPubKey<C>>;
            let _ = parse_public_key_der::<EcPubKey<C>>;
        }

        macro_rules! unwrap_any_curve {
            ($name:ident, $any_type:ty, $key_type:ty, $curve_variant:path) => {
                fn $name(key: $any_type) -> $key_type {
                    match key {
                        $curve_variant(key) => key,
                        _ => panic!("unexpected curve"),
                    }
                }
            };
        }

        unwrap_any_curve!(
            unwrap_priv_key_any_p256,
            EcPrivKeyAnyCurve,
            EcPrivKey<P256>,
            EcPrivKeyAnyCurve::P256
        );
        unwrap_any_curve!(
            unwrap_priv_key_any_p384,
            EcPrivKeyAnyCurve,
            EcPrivKey<P384>,
            EcPrivKeyAnyCurve::P384
        );
        unwrap_any_curve!(
            unwrap_priv_key_any_p521,
            EcPrivKeyAnyCurve,
            EcPrivKey<P521>,
            EcPrivKeyAnyCurve::P521
        );
        unwrap_any_curve!(
            unwrap_pub_key_any_p256,
            EcPubKeyAnyCurve,
            EcPubKey<P256>,
            EcPubKeyAnyCurve::P256
        );
        unwrap_any_curve!(
            unwrap_pub_key_any_p384,
            EcPubKeyAnyCurve,
            EcPubKey<P384>,
            EcPubKeyAnyCurve::P384
        );
        unwrap_any_curve!(
            unwrap_pub_key_any_p521,
            EcPubKeyAnyCurve,
            EcPubKey<P521>,
            EcPubKeyAnyCurve::P521
        );

        test::<P256, _, _>(unwrap_priv_key_any_p256, unwrap_pub_key_any_p256);
        test::<P384, _, _>(unwrap_priv_key_any_p384, unwrap_pub_key_any_p384);
        test::<P521, _, _>(unwrap_priv_key_any_p521, unwrap_pub_key_any_p521);
    }

    #[test]
    fn test_parse_fail() {
        // Test that invalid input is rejected.
        fn test_parse_invalid<C: Curve>() {
            should_fail(
                parse_private_key_der::<EcPrivKey<C>>(&[]),
                "parse_private_key_der",
                "elliptic curve routines:OPENSSL_internal:DECODE_ERROR",
            );
            should_fail(
                parse_public_key_der::<EcPubKey<C>>(&[]),
                "parse_public_key_der",
                "public key routines:OPENSSL_internal:DECODE_ERROR",
            );
            should_fail(
                parse_private_key_der_any_curve(&[]),
                "parse_private_key_der_any_curve",
                "elliptic curve routines:OPENSSL_internal:DECODE_ERROR",
            );
            should_fail(
                parse_public_key_der_any_curve(&[]),
                "parse_public_key_der_any_curve",
                "public key routines:OPENSSL_internal:DECODE_ERROR",
            );
        }

        test_parse_invalid::<P256>();
        test_parse_invalid::<P384>();
        test_parse_invalid::<P521>();

        // Test that, when a particular curve is expected, other curves are
        // rejected.
        fn test_parse_wrong_curve<C1: Curve, C2: Curve>() {
            let privkey = EcPrivKey::<C1>::generate().unwrap();
            let key_der = marshal_private_key_der(&privkey);
            should_fail(
                parse_private_key_der::<EcPrivKey<C2>>(&key_der),
                "parse_private_key_der",
                "elliptic curve routines:OPENSSL_internal:GROUP_MISMATCH",
            );
            let key_der = marshal_public_key_der(&privkey.public());
            should_fail(
                parse_public_key_der::<EcPubKey<C2>>(&key_der),
                "parse_public_key_der",
                "mundane: unexpected curve:",
            );
        }

        // All pairs of curves, (X, Y), such that X != Y.
        test_parse_wrong_curve::<P256, P384>();
        test_parse_wrong_curve::<P256, P521>();
        test_parse_wrong_curve::<P384, P256>();
        test_parse_wrong_curve::<P384, P521>();
        test_parse_wrong_curve::<P521, P256>();
        test_parse_wrong_curve::<P521, P384>();
    }
}
