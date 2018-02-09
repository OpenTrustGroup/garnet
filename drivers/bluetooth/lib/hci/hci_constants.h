// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <cstddef>
#include <cstdint>

// This file contains constants and numbers used in HCI packet payloads.

namespace btlib {
namespace hci {

// HCI_Version Assigned Values See the "Assigned Numbers" document for
// reference.
// (https://www.bluetooth.com/specifications/assigned-numbers/host-controller-interface)
enum class HCIVersion : uint8_t {
  // Bluetooth Core Specification 1.0b
  k1_0b = 0,

  // Bluetooth Core Specification 1.1
  k1_1,

  // Bluetooth Core Specification 1.2
  k1_2,

  // Bluetooth Core Specification 2.0 + EDR
  k2_0_EDR,

  // Bluetooth Core Specification 2.1 + EDR
  k2_1_EDR,

  // Bluetooth Core Specification 3.0 + HS
  k3_0_HS,

  // Bluetooth Core Specification 4.0
  k4_0,

  // Bluetooth Core Specification 4.1
  k4_1,

  // Bluetooth Core Specification 4.2
  k4_2,

  // Bluetooth Core Specification 5.0
  k5_0,

  kReserved
};

// HCI Error Codes. Refer to Core Spec v5.0, Vol 2, Part D for definitions and
// descriptions. All enum values are in increasing numerical order, however the
// values are listed below for clarity.
enum Status : uint8_t {
  kSuccess                                      = 0x00,
  kUnknownCommand                               = 0x01,
  kUnknownConnectionId                          = 0x02,
  kHardwareFailure                              = 0x03,
  kPageTimeout                                  = 0x04,
  kAuthenticationFailure                        = 0x05,
  kPinOrKeyMissing                              = 0x06,
  kMemoryCapacityExceeded                       = 0x07,
  kConnectionTimeout                            = 0x08,
  kConnectionLimitExceeded                      = 0x09,
  kSynchronousConnectionLimitExceeded           = 0x0A,
  kConnectionAlreadyExists                      = 0x0B,
  kCommandDisallowed                            = 0x0C,
  kConnectionRejectedLimitedResources           = 0x0D,
  kConnectionRejectedSecurity                   = 0x0E,
  kConnectionRejectedBadBdAddr                  = 0x0F,
  kConnectionAcceptTimeoutExceeded              = 0x10,
  kUnsupportedFeatureOrParameter                = 0x11,
  kInvalidHCICommandParameters                  = 0x12,
  kRemoteUserTerminatedConnection               = 0x13,
  kRemoteDeviceTerminatedConnectionLowResources = 0x14,
  kRemoteDeviceTerminatedConnectionPowerOff     = 0x15,
  kConnectionTerminatedByLocalHost              = 0x16,
  kRepeatedAttempts                             = 0x17,
  kPairingNotAllowed                            = 0x18,
  kUnknownLMPPDU                                = 0x19,
  kUnsupportedRemoteFeature                     = 0x1A,
  kSCOOffsetRejected                            = 0x1B,
  kSCOIntervalRejected                          = 0x1C,
  kSCOAirModeRejected                           = 0x1D,
  kInvalidLMPOrLLParameters                     = 0x1E,
  kUnspecifiedError                             = 0x1F,
  kUnsupportedLMPOrLLParameterValue             = 0x20,
  kRoleChangeNotAllowed                         = 0x21,
  kLMPOrLLResponseTimeout                       = 0x22,
  kLMPErrorTransactionCollision                 = 0x23,
  kLMPPDUNotAllowed                             = 0x24,
  kEncryptionModeNotAcceptable                  = 0x25,
  kLinkKeyCannotBeChanged                       = 0x26,
  kRequestedQoSNotSupported                     = 0x27,
  kInstantPassed                                = 0x28,
  kPairingWithUnitKeyNotSupported               = 0x29,
  kDifferentTransactionCollision                = 0x2A,
  kReserved0                                    = 0x2B,
  kQoSUnacceptableParameter                     = 0x2C,
  kQoSRejected                                  = 0x2D,
  kChannelClassificationNotSupported            = 0x2E,
  kInsufficientSecurity                         = 0x2F,
  kParameterOutOfMandatoryRange                 = 0x30,
  kReserved1                                    = 0x31,
  kRoleSwitchPending                            = 0x32,
  kReserved2                                    = 0x33,
  kReservedSlotViolation                        = 0x34,
  kRoleSwitchFailed                             = 0x35,
  kExtendedInquiryResponseTooLarge              = 0x36,
  kSecureSimplePairingNotSupportedByHost        = 0x37,
  kHostBusyPairing                              = 0x38,
  kConnectionRejectedNoSuitableChannelFound     = 0x39,
  kControllerBusy                               = 0x3A,
  kUnacceptableConnectionParameters             = 0x3B,
  kDirectedAdvertisingTimeout                   = 0x3C,
  kConnectionTerminatedMICFailure               = 0x3D,
  kConnectionFailedToBeEstablished              = 0x3E,
  kMACConnectionFailed                          = 0x3F,
  kCoarseClockAdjustmentRejected                = 0x40,

  // 5.0
  kType0SubmapNotDefined                        = 0x41,
  kUnknownAdvertisingIdentifier                 = 0x42,
  kLimitReached                                 = 0x43,
  kOperationCancelledByHost                     = 0x44,

  // Our stack defined error to signal a command time out
  // TODO(armansito): Vendor commands could be using this code to report their custom errors. We
  // should introduce a wider (16-bit) Status type for errors reported via CommandChannel and assign
  // a number from beyond the reserved range for our own errors.
  kCommandTimeout                               = 0xFF,
};

// Bitmask values for the 8-octet Local Supported LMP Features bit-field. See Core Spec
// v5.0, Volume 2, Part C, Section 3.3 "Feature Mask Definition".
enum class LMPFeature : uint64_t {
  // Octet 0
  k3SlotPackets   = (1 << 0),
  k5SlotPackets   = (1 << 1),
  kEncryption     = (1 << 2),
  kSlotOffset     = (1 << 3),
  kTimingAccuracy = (1 << 4),
  kRoleSwitch     = (1 << 5),
  kHoldMode       = (1 << 6),
  kSniffMode      = (1 << 7),

  // Octet 1
  // Reserved (1 << 0 + 8)
  // TODO(armansito): Add definitions

  // Octet 2
  // TODO(armansito): Add definitions

  // Octet 3
  // TODO(armansito): Add definitions

  // Octet 4
  kEV4Packets         = (1ull << 32),
  kEV5Packets         = (1ull << 33),
  // Reserved
  kAFHCapableSlave    = (1ull << 35),
  kAFHClassSlave      = (1ull << 36),
  kBREDRNotSupported  = (1ull << 37),
  kLESupported        = (1ull << 38),
  k3SlotEDRACLPackets = (1ull << 39),

  // Octet 5
  // TODO(armansito): Add definitions

  // Octet 6
  // TODO(armansito): Add definitions

  // Octet 7
  kLinkSupervisionTimeoutChangedEvent = (1ull << 56),
  kInquiryTxPowerLevel                = (1ull << 57),
  kEnhancedPowerControl               = (1ull << 58),
  // Reserved
  // Reserved
  // Reserved
  // Reserved
  kExtendedFeatures                   = (1ull << 63),

  // Extended features (Page 1)
  kSecureSimplePairingHostSupport = (1ull << 0),
  kLESupportedHost                = (1ull << 1),
  kSimultaneousLEAndBREDR         = (1ull << 2),
  kSecureConnectionsHost          = (1ull << 3),

  // Extended features (Page 2)
  // TODO(armansito): Add definitions
};

// Bitmask values for the 64-octet Supported Commands bit-field. See Core Spec
// v5.0, Volume 2, Part E, Section 6.27 "Supported Commands".
enum class SupportedCommand : uint8_t {
  // Octet 0
  kInquiry                 = (1 << 0),
  kInquiryCancel           = (1 << 1),
  kPeriodicInquiryMode     = (1 << 2),
  kExitPeriodicInquiryMode = (1 << 3),
  kCreateConnection        = (1 << 4),
  kDisconnect              = (1 << 5),
  kAddSCOConnection        = (1 << 6),  // deprecated
  kCreateConnectionCancel  = (1 << 7),

  // Octet 1
  kAcceptConnectionRequest     = (1 << 0),
  kRejectConnectionRequest     = (1 << 1),
  kLinkKeyRequestReply         = (1 << 2),
  kLinkKeyRequestNegativeReply = (1 << 3),
  kPINCodeRequestReply         = (1 << 4),
  kPINCodeRequestNegativeReply = (1 << 5),
  kChangeConnectionPacketType  = (1 << 6),
  kAuthenticationRequested     = (1 << 7),

  // Octet 2
  kSetConnectionEncryption      = (1 << 0),
  kChangeConnectionLinkKey      = (1 << 1),
  kMasterLinkKey                = (1 << 2),
  kRemoteNameRequest            = (1 << 3),
  kRemoteNameRequestCancel      = (1 << 4),
  kReadRemoteSupportedFeatures  = (1 << 5),
  kReadRemoteExtendedFeatures   = (1 << 6),
  kReadRemoteVersionInformation = (1 << 7),

  // Octet 3
  kReadClockOffset = (1 << 0),
  kReadLMPHandle   = (1 << 1),

  // Octet 4
  kHoldMode      = (1 << 1),
  kSniffMode     = (1 << 2),
  kExitSniffMode = (1 << 3),
  kParkState     = (1 << 4),  // Reserved in 5.0
  kExitParkState = (1 << 5),  // Reserved in 5.0
  kQOSSetup      = (1 << 6),
  kRoleDiscovery = (1 << 7),

  // Octet 5
  kSwitchRole                     = (1 << 0),
  kReadLinkPolicySettings         = (1 << 1),
  kWriteLinkPolicySettings        = (1 << 2),
  kReadDefaultLinkPolicySettings  = (1 << 3),
  kWriteDefaultLinkPolicySettings = (1 << 4),
  kFlowSpecification              = (1 << 5),
  kSetEventMask                   = (1 << 6),
  kReset                          = (1 << 7),

  // Octet 6
  kSetEventFilter       = (1 << 0),
  kFlush                = (1 << 1),
  kReadPINType          = (1 << 2),
  kWritePINType         = (1 << 3),
  kCreateNewUnitKey     = (1 << 4),
  kReadStoredLinkKey    = (1 << 5),
  kWriteStoredLinkKey   = (1 << 6),
  kDeletedStoredLinkKey = (1 << 7),

  // Octet 7
  kWriteLocalName                = (1 << 0),
  kReadLocalname                 = (1 << 1),
  kReadConnectionAttemptTimeout  = (1 << 2),
  kWriteConnectionAttemptTimeout = (1 << 3),
  kReadPageTimeout               = (1 << 4),
  kWritePageTimeout              = (1 << 5),
  kReadScanEnable                = (1 << 6),
  kWriteScanEnable               = (1 << 7),

  // Octet 8
  kReadPageScanActivity      = (1 << 0),
  kWritePageScanActivity     = (1 << 1),
  kReadInquiryScanActivity   = (1 << 2),
  kWriteInquiryScanActivity  = (1 << 3),
  kReadAuthenticationEnable  = (1 << 4),
  kWriteAuthenticationEnable = (1 << 5),
  kReadEncryptionMode        = (1 << 6),  // deprecated
  kWriteEncryptionMode       = (1 << 7),  // deprecated

  // Octet 9
  kReadClassOfDevice                = (1 << 0),
  kWriteClassOfDevice               = (1 << 1),
  kReadVoiceSetting                 = (1 << 2),
  kWriteVoiceSetting                = (1 << 3),
  kReadAutomaticFlushTimeout        = (1 << 4),
  kWriteAutomaticFlushTimeout       = (1 << 5),
  kReadNumBroadcastRetransmissions  = (1 << 6),
  kWriteNumBroadcastRetransmissions = (1 << 7),

  // Octet 10
  kReadHoldModeActivity              = (1 << 0),
  kWriteHoldModeActivity             = (1 << 1),
  kReadTransmitPowerLevel            = (1 << 2),
  kReadSynchronousFlowControlEnable  = (1 << 3),
  kWriteSynchronousFlowControlEnable = (1 << 4),
  kSetControllerToHostFlowControl    = (1 << 5),
  kHostBufferSize                    = (1 << 6),
  kHostNumberOfCompletedPackets      = (1 << 7),

  // Octet 11
  kReadLinkSupervisionTimeout        = (1 << 0),
  kWriteLinkSupervisionTimeout       = (1 << 1),
  kReadNumberOfSupportedIAC          = (1 << 2),
  kReadCurrentIACLAP                 = (1 << 3),
  kWriteCurrentIACLAP                = (1 << 4),
  kReadPageScanModePeriod            = (1 << 5),  // deprecated
  kWritePageScanModePeriod           = (1 << 6),  // deprecated
  kReadPageScanMode                  = (1 << 7),  // deprecated

  // Octet 12
  kWritePageScanMode               = (1 << 0),  // deprecated
  kSetAFHHostChannelClassification = (1 << 1),
  kReadInquiryScanType             = (1 << 4),
  kWriteInquiryScanType            = (1 << 5),
  kReadInquiryMode                 = (1 << 6),
  kWriteInquiryMode                = (1 << 7),

  // Octet 13
  kReadPageScanType              = (1 << 0),
  kWritePageScanType             = (1 << 1),
  kReadAFHChannelAssessmentMode  = (1 << 2),
  kWriteAFHChannelAssessmentMode = (1 << 3),

  // Octet 14
  kReadLocalVersionInformation = (1 << 3),
  kReadLocalSupportedFeatures  = (1 << 5),
  kReadLocalExtendedFeatures   = (1 << 6),
  kReadBufferSize              = (1 << 7),

  // Octet 15
  kReadCountryCode           = (1 << 0),  // deprecated
  kReadBDADDR                = (1 << 1),
  kReadFailedContactCounter  = (1 << 2),
  kResetFailedContactCOunter = (1 << 3),
  kReadLinkQuality           = (1 << 4),
  kReadRSSI                  = (1 << 5),
  kReadAFHChannelMap         = (1 << 6),
  kReadClock                 = (1 << 7),

  // Octet 16
  kReadLoopbackMode                   = (1 << 0),
  kWriteLoopbackMode                  = (1 << 1),
  kEnableDeviceUnderTestMode          = (1 << 2),
  kSetupSynchronousConnectionRequest  = (1 << 3),
  kAcceptSynchronousConnectionRequest = (1 << 4),
  kRejectSynchronousConnectionRequest = (1 << 5),

  // Octet 17
  kReadExtendedInquiryResponse  = (1 << 0),
  kWriteExtendedInquiryResponse = (1 << 1),
  kRefreshEncryptionKey         = (1 << 2),
  kSniffSubrating               = (1 << 4),
  kReadSimplePairingMode        = (1 << 5),
  kWriteSimplePairingMode       = (1 << 6),
  kReadLocalOOBData             = (1 << 7),

  // Octet 18
  kReadInquiryResponseTransmitPowerLevel = (1 << 0),
  kWriteInquiryTransmitPowerLevel        = (1 << 1),
  kReadDefaultErroneousDataReporting     = (1 << 2),
  kWriteDefaultErroneousDataReporting    = (1 << 3),
  kIOCapabilityRequestReply              = (1 << 7),

  // Octet 19
  kUserConfirmationRequestReply         = (1 << 0),
  kUserConfirmationRequestNegativeReply = (1 << 1),
  kUserPasskeyRequestReply              = (1 << 2),
  kUserPasskeyRequestNegativeReply      = (1 << 3),
  kRemoteOOBDataRequestReply            = (1 << 4),
  kWriteSimplePairingDebugMode          = (1 << 5),
  kEnhancedFlush                        = (1 << 6),
  kRemoteOOBDataRequestNegativeReply    = (1 << 7),

  // Octet 20
  kSendKeypressNotification         = (1 << 2),
  kIOCapabilityRequestNegativeReply = (1 << 3),
  kReadEncryptionKeySize            = (1 << 4),

  // Octet 21
  kCreatePhysicalLink     = (1 << 0),
  kAcceptPhysicalLink     = (1 << 1),
  kDisconnectPhysicalLink = (1 << 2),
  kCreateLogicalLink      = (1 << 3),
  kAcceptLogicalLink      = (1 << 4),
  kDisconnectLogicalLink  = (1 << 5),
  kLogicalLinkCancel      = (1 << 6),
  kFlowSpecModify         = (1 << 7),

  // Octet 22
  kReadLogicalLinkAcceptTimeout  = (1 << 0),
  kWriteLogicalLinkAcceptTimeout = (1 << 1),
  kSetEventMaskPage2             = (1 << 2),
  kReadLocationData              = (1 << 3),
  kWriteLocationData             = (1 << 4),
  kReadLocalAMPInfo              = (1 << 5),
  kReadLocalAMPASSOC             = (1 << 6),
  kWriteRemoteAMPASSOC           = (1 << 7),

  // Octet 23
  kReadFlowControlMode      = (1 << 0),
  kWriteFlowControlMode     = (1 << 1),
  kReadDataBlockSize        = (1 << 2),
  kEnableAMPReceiverReports = (1 << 5),
  kAMPTestEnd               = (1 << 6),
  kAMPTest                  = (1 << 7),

  // Octet 24
  kReadEnhancedTransmitPowerLevel = (1 << 0),
  kReadBestEffortFlushTimeout     = (1 << 2),
  kWriteBestEffortFlushTimeout    = (1 << 3),
  kShortRangeMode                 = (1 << 4),
  kReadLEHostSupported            = (1 << 5),
  kWriteLEHostSupport             = (1 << 6),

  // Octet 25
  kLESetEventMask                  = (1 << 0),
  kLEReadBufferSize                = (1 << 1),
  kLEReadLocalSupportedFeatures    = (1 << 2),
  kLESetRandomAddress              = (1 << 4),
  kLESetAdvertisingParameters      = (1 << 5),
  kLEReadAdvertisingChannelTXPower = (1 << 6),
  kLESetAdvertisingData            = (1 << 7),

  // Octet 26
  kLESetScanResponseData    = (1 << 0),
  kLESetAdvertisingEnable   = (1 << 1),
  kLESetScanParameters      = (1 << 2),
  kLESetScanEnable          = (1 << 3),
  kLECreateConnection       = (1 << 4),
  kLECreateConnectionCancel = (1 << 5),
  kLEReadWhiteListSize      = (1 << 6),
  kLEClearWhiteList         = (1 << 7),

  // Octet 27
  kLEAddDeviceToWhiteList         = (1 << 0),
  kLERemoveDeviceFromWhiteList    = (1 << 1),
  kLEConnectionUpdate             = (1 << 2),
  kLESetHostChannelClassification = (1 << 3),
  kLEReadChannelMap               = (1 << 4),
  kLEReadRemoteUsedFeatures       = (1 << 5),
  kLEEncrypt                      = (1 << 6),
  kLERand                         = (1 << 7),

  // Octet 28
  kLEStartEncryption                 = (1 << 0),
  kLELongTermKeyRequestReply         = (1 << 1),
  kLELongTermKeyRequestNegativeReply = (1 << 2),
  kLEReadSupportedStates             = (1 << 3),
  kLEReceiverTest                    = (1 << 4),
  kLETransmitterTest                 = (1 << 5),
  kLETestEnd                         = (1 << 6),

  // Octet 29
  kEnhancedSetupSynchronousConnection  = (1 << 3),
  kEnhancedAcceptSynchronousConnection = (1 << 4),
  kReadLocalSupportedCodecs            = (1 << 5),
  kSetMWSChannelParameters             = (1 << 6),
  kSetExternalFrameConfiguration       = (1 << 7),

  // Octet 30
  kSetMWSSignaling                   = (1 << 0),
  kSetMWSTransportLayer              = (1 << 1),
  kSetMWSScanFrequencyTable          = (1 << 2),
  kGetMWSTransportLayerConfiguration = (1 << 3),
  kSetMWSPATTERNConfiguration        = (1 << 4),
  kSetTriggeredClockCapture          = (1 << 5),
  kTruncatedPage                     = (1 << 6),
  kTruncatedPageCancel               = (1 << 7),

  // Octet 31
  kSetConnectionlessSlaveBroadcast        = (1 << 0),
  kSetConnectionlessSlaveBroadcastReceive = (1 << 1),
  kStartSynchronizationTrain              = (1 << 2),
  kReceiveSynchronizationTrain            = (1 << 3),
  kSetReservedLTADDR                      = (1 << 4),
  kDeleteReservedLTADDR                   = (1 << 5),
  kSetConnectionlessSlaveBroadcastData    = (1 << 6),
  kReadSynchronizationTrainParameters     = (1 << 7),

  // Octet 32
  kWriteSynchronizationTrainParameters = (1 << 0),
  kRemoteOOBExtendedDataRequestReply   = (1 << 1),
  kReadSecureConnectionsHostSupport    = (1 << 2),
  kWriteSecureConnectionsHostSupport   = (1 << 3),
  kReadAuthenticatedPayloadTimeout     = (1 << 4),
  kWriteAuthenticatedPayloadTimeout    = (1 << 5),
  kReadLocalOOBExtendedData            = (1 << 6),
  kWriteSecureConnectionsTestMode      = (1 << 7),

  // Octet 33
  kReadExtendedPageTimeout                         = (1 << 0),
  kWriteExtendedPageTimeout                        = (1 << 1),
  kReadExtendedInquiryLength                       = (1 << 2),
  kWriteExtendedInquiryLength                      = (1 << 3),
  kLERemoteConnectionParameterRequestReply         = (1 << 4),
  kLERemoteConnectionParameterRequestNegativeReply = (1 << 5),
  kLESetDataLength                                 = (1 << 6),
  kLEReadSuggestedDefaultDataLength                = (1 << 7),

  // Octet 34
  kLEWriteSuggestedDefaultDataLength = (1 << 0),
  kLEReadLocalP256PublicKey          = (1 << 1),
  kLEGenerateDHKey                   = (1 << 2),
  kLEAddDeviceToResolvingList        = (1 << 3),
  kLERemoveDeviceFromResolvingList   = (1 << 4),
  kLEClearResolvingList              = (1 << 5),
  kLEReadResolvingListSize           = (1 << 6),
  kLEReadPeerResolvableAddress       = (1 << 7),

  // Octet 35
  kLEReadLocalResolvableAddress         = (1 << 0),
  kLESetAddressResolutionEnable         = (1 << 1),
  kLESetResolvablePrivateAddressTimeout = (1 << 2),
  kLEReadMaximumDataLength              = (1 << 3),

  // Added in 5.0 (octet 35 cont'd)
  kLEReadPHYCommand                     = (1 << 4),
  kLESetDefaultPHYCommand               = (1 << 5),
  kLESetPHYCommand                      = (1 << 6),
  kLEEnhancedReceiverTestCommand        = (1 << 7),

  // Octet 36
  kLEEnhancedTransmitterTestCommand              = (1 << 0),
  kLESetAdvertisingSetRandomAddressCommand       = (1 << 1),
  kLESetExtendedAdvertisingParametersCommand     = (1 << 2),
  kLESetExtendedAdvertisingDataCommand           = (1 << 3),
  kLESetExtendedScanResponseDataCommand          = (1 << 4),
  kLESetExtendedAdvertisingEnableCommand         = (1 << 5),
  kLEReadMaximumAdvertisingDataLengthCommand     = (1 << 6),
  kLEReadNumberOfSupportedAdvertisingSetsCommand = (1 << 7),

  // Octet 37
  kLERemoveAdvertisingSetCommand             = (1 << 0),
  kLEClearAdvertisingSetsCommand             = (1 << 1),
  kLESetPeriodicAdvertisingParametersCommand = (1 << 2),
  kLESetPeriodicAdvertisingDataCommand       = (1 << 3),
  kLESetPeriodicAdvertisingEnableCommand     = (1 << 4),
  kLESetExtendedScanParametersCommand        = (1 << 5),
  kLESetExtendedScanEnableCommand            = (1 << 6),
  kLEExtendedCreateConnectionCommand         = (1 << 7),

  // Octet 38
  kLEPeriodicAdvertisingCreateSyncCommand          = (1 << 0),
  kLEPeriodicAdvertisingCreateSyncCancelCommand    = (1 << 1),
  kLEPeriodicAdvertisingTerminateSyncCommand       = (1 << 2),
  kLEAddDeviceToPeriodicAdvertiserListCommand      = (1 << 3),
  kLERemoveDeviceFromPeriodicAdvertiserListCommand = (1 << 4),
  kLEClearPeriodicAdvertiserListCommand            = (1 << 5),
  kLEReadPeriodicAdvertiserListSizeCommand         = (1 << 6),
  kLEReadTransmitPowerCommand                      = (1 << 7),

  // Octet 39
  kLEReadRFPathCompensationCommand   = (1 << 0),
  kLEWriteRFPathCompensationCommand  = (1 << 1),
  kLESetPrivacyMode                  = (1 << 2),
};

// Bitmask values for the 8-octet LE Supported Features bit-field. See Core Spec
// v5.0, Volume 6, Part B, Section 4.6 "Feature Support".
enum class LESupportedFeature : uint64_t {
  kLEEncryption                         = (1 << 0),
  kConnectionParametersRequestProcedure = (1 << 1),
  kExtendedRejectIndication             = (1 << 2),
  kSlaveInitiatedFeaturesExchange       = (1 << 3),
  kLEPing                               = (1 << 4),
  kLEDataPacketLengthExtension          = (1 << 5),
  kLLPrivacy                            = (1 << 6),
  kExtendedScannerFilterPolicies        = (1 << 7),

  // Added in 5.0
  kStableModulationIndexTransmitter     = (1 << 8),
  kStableModulationIndexReceiver        = (1 << 9),
  kLECodedPHY                           = (1 << 10),
  kLEExtendedAdvertising                = (1 << 11),
  kLEPeriodicAdvertising                = (1 << 12),
  kChannelSelectionAlgorithm2           = (1 << 13),
  kLEPowerClass1                        = (1 << 14),
  kMinimumNumberOfUsedChannelsProcedure = (1 << 15),

  // The rest is reserved for future use.
};

// Bitmask values for the 8-octet HCI_Set_Event_Mask command parameter.
enum class EventMask : uint64_t {
  kInquiryCompleteEvent = (1 << 0),
  kInquiryResultEvent = (1 << 1),
  kConnectionCompleteEvent = (1 << 2),
  kConnectionRequestEvent = (1 << 3),
  kDisconnectionCompleteEvent = (1 << 4),
  kAuthenticationCompleteEvent = (1 << 5),
  kRemoteNameRequestCompleteEvent = (1 << 6),
  kEncryptionChangeEvent = (1 << 7),
  kChangeConnectionLinkKeyCompleteEvent = (1 << 8),
  kMasterLinkKeyCompleteEvent = (1 << 9),
  kReadRemoteSupportedFeaturesCompleteEvent = (1 << 10),
  kReadRemoteVersionInformationCompleteEvent = (1 << 11),
  kQoSSetupCompleteEvent = (1 << 12),
  // Reserved For Future Use: (1 << 13)
  // Reserved For Future Use: (1 << 14)
  kHardwareErrorEvent = (1 << 15),
  kFlushOccurredEvent = (1 << 16),
  kRoleChangeEvent = (1 << 17),
  // Reserved For Future Use: (1 << 18)
  kModeChangeEvent = (1 << 19),
  kReturnLinkKeysEvent = (1 << 20),
  kPINCodeRequestEvent = (1 << 21),
  kLinkKeyRequestEvent = (1 << 22),
  kLinkKeyNotificationEvent = (1 << 23),
  kLoopbackCommandEvent = (1 << 24),
  kDataBufferOverflowEvent = (1 << 25),
  kMaxSlotsChangeEvent = (1 << 26),
  kReadClockOffsetCompleteEvent = (1 << 27),
  kConnectionPacketTypeChangedEvent = (1 << 28),
  kQoSViolationEvent = (1 << 29),
  kPageScanModeChangeEvent = (1 << 30),  // deprecated
  kPageScanRepetitionModeChangeEvent = (1ull << 31),
  kFlowSpecificationCompleteEvent = (1ull << 32),
  kInquiryResultWithRSSIEvent = (1ull << 33),
  kReadRemoteExtendedFeaturesCompleteEvent = (1ull << 34),
  // Reserved For Future Use: (1ull << 35)
  // Reserved For Future Use: (1ull << 36)
  // Reserved For Future Use: (1ull << 37)
  // Reserved For Future Use: (1ull << 38)
  // Reserved For Future Use: (1ull << 39)
  // Reserved For Future Use: (1ull << 40)
  // Reserved For Future Use: (1ull << 41)
  // Reserved For Future Use: (1ull << 42)
  kSynchronousConnectionCompleteEvent = (1ull << 43),
  kSynchronousConnectionChangedEvent = (1ull << 44),
  kSniffSubratingEvent = (1ull << 45),
  kExtendedInquiryResultEvent = (1ull << 46),
  kEncryptionKeyRefreshCompleteEvent = (1ull << 47),
  kIOCapabilityRequestEvent = (1ull << 48),
  kIOCapabilityResponseEvent = (1ull << 49),
  kUserConfirmationRequestEvent = (1ull << 50),
  kUserPasskeyRequestEvent = (1ull << 51),
  kRemoteOOBDataRequestEvent = (1ull << 52),
  kSimplePairingCompleteEvent = (1ull << 53),
  // Reserved For Future Use: (1ull << 54)
  kLinkSupervisionTimeoutChangedEvent = (1ull << 55),
  kEnhancedFlushCompleteEvent = (1ull << 56),
  // Reserved For Future Use: (1ull << 57)
  kUserPasskeyNotificationEvent = (1ull << 58),
  kKeypressNotificationEvent = (1ull << 59),
  kRemoteHostSupportedFeaturesNotificationEvent = (1ull << 60),
  kLEMetaEvent = (1ull << 61),
  // Reserved For Future Use: (1ull << 62)
  // Reserved For Future Use: (1ull << 63)
};

// Bitmask values for the 8-octet HCI_Set_Event_Mask_Page_2 command parameter.
enum class EventMaskPage2 : uint64_t {
  kPhysicalLinkCompleteEvent = (1 << 0),
  kChannelSelectedEvent = (1 << 1),
  kDisconnectionPhysicalLinkCompleteEvent = (1 << 2),
  kPhysicalLinkLossEarlyWarningEvent = (1 << 3),
  kPhysicalLinkRecoveryEvent = (1 << 4),
  kLogicalLinkCompleteEvent = (1 << 5),
  kDisconnectionLogicalLinkCompleteEvent = (1 << 6),
  kFlowSpecModifyCompleteEvent = (1 << 7),
  kNumberOfCompletedDataBlocksEvent = (1 << 8),
  kAMPStartTestEvent = (1 << 9),
  kAMPTestEndEvent = (1 << 10),
  kAMPReceiverReportEvent = (1 << 11),
  kShortRangeModeChangeCompleteEvent = (1 << 12),
  kAMPStatusChangeEvent = (1 << 13),
  kTriggeredClockCaptureEvent = (1 << 14),
  kSynchronizationTrainCompleteEvent = (1 << 15),
  kSynchronizationTrainReceivedEvent = (1 << 16),
  kConnectionlessSlaveBroadcastReceiveEvent = (1 << 17),
  kConnectionlessSlaveBroadcastTimeoutEvent = (1 << 18),
  kTruncatedPageCompleteEvent = (1 << 19),
  kSlavePageResponseTimeoutEvent = (1 << 20),
  kConnectionlessSlaveBroadcastChannelMapChangeEvent = (1 << 21),
  kInquiryResponseNotificationEvent = (1 << 22),
  kAuthenticatedPayloadTimeoutExpiredEvent = (1 << 23),
  kSAMStatusChangeEvent = (1 << 24),
};

// Bitmask values for the 8-octet HCI_LE_Set_Event_Mask command parameter.
enum class LEEventMask : uint64_t {
  kLEConnectionComplete                 = (1 << 0),
  kLEAdvertisingReport                  = (1 << 1),
  kLEConnectionUpdateComplete           = (1 << 2),
  kLEReadRemoteFeaturesComplete         = (1 << 3),
  kLELongTermKeyRequest                 = (1 << 4),
  kLERemoteConnectionParameterRequest   = (1 << 5),
  kLEDataLengthChange                   = (1 << 6),
  kLEReadLocalP256PublicKeyComplete     = (1 << 7),
  kLEGenerateDHKeyComplete              = (1 << 8),
  kLEEnhancedConnectionComplete         = (1 << 9),
  kLEDirectedAdvertisingReport          = (1 << 10),
  kLEPHYUpdateComplete                  = (1 << 11),
  kLEExtendedAdvertisingReport          = (1 << 12),
  kLEPeriodicAdvertisingSyncEstablished = (1 << 13),
  kLEPeriodicAdvertisingReport          = (1 << 14),
  kLEPeriodicAdvertisingSyncLost        = (1 << 15),
  kLEExtendedScanTimeout                = (1 << 16),
  kLEExtendedAdvertisingSetTerminated   = (1 << 17),
  kLEScanRequestReceived                = (1 << 18),
  kLEChannelSelectionAlgorithm          = (1 << 19),
};

// Binary values that can be generically passed to HCI commands that expect a
// 1-octet boolean "enable"/"disable" parameter.
enum class GenericEnableParam : uint8_t {
  kDisable = 0x00,
  kEnable = 0x01,
};

// Values that can be passed to the Type parameter in a HCI_Read_Transmit_Power_Level command.
enum class ReadTransmitPowerType : uint8_t {
  // Read Current Transmit Power Level.
  kCurrent = 0x00,

  // Read Maximum Transmit Power Level.
  kMax = 0x01,
};

// HCI command timeout interval (milliseconds)
constexpr int64_t kCommandTimeoutMs = 2000;

// The minimum and maximum range values for the LE advertising interval
// parameters.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5)
constexpr uint16_t kLEAdvertisingIntervalMin = 0x0020;
constexpr uint16_t kLEAdvertisingIntervalMax = 0x4000;

// The minimum and maximum range values for the LE periodic advertising interval
// parameters.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.61)
constexpr uint16_t kLEPeriodicAdvertisingIntervalMin = 0x0006;
constexpr uint16_t kLEPeriodicAdvertisingIntervalMax = 0xFFFF;

// The minimum and maximum range values for the LE extended advertising interval parameters.
constexpr uint32_t kLEExtendedAdvertisingIntervalMin = 0x000020;
constexpr uint32_t kLEExtendedAdvertisingIntervalMax = 0xFFFFFF;

// The default LE advertising interval parameter value, corresponding to 1.28
// seconds (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5).
constexpr uint16_t kLEAdvertisingIntervalDefault = 0x0800;

// The minimum and maximum range values for the LE scan interval parameters.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.10)
constexpr uint16_t kLEScanIntervalMin = 0x0004;
constexpr uint16_t kLEScanIntervalMax = 0x4000;

// The minimum and maximum range values for the LE extended scan interval parameters.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.64)
constexpr uint16_t kLEExtendedScanIntervalMin = 0x0004;
constexpr uint16_t kLEExtendedScanIntervalMax = 0xFFFF;

// The default LE scan interval parameter value, corresponding to 10
// milliseconds (see Core Spec v5.0, Vol 2, Part E, Section 7.8.10).
constexpr uint16_t kLEScanIntervalDefault = 0x0010;

// The minimum and maximum range values for the LE connection interval parameters.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.12)
constexpr uint16_t kLEConnectionIntervalMin = 0x0006;
constexpr uint16_t kLEConnectionIntervalMax = 0x0C80;

// The maximum value that can be used for the |conn_latency| parameter in a HCI_LE_Create_Connection
// command (see Core Spec v5.0, Vol 2, Part E, Section 7.8.12).
constexpr uint16_t kLEConnectionLatencyMax = 0x01F3;

// The minimum and maximum range values for LE connection supervision timeout parameters.
constexpr uint16_t kLEConnectionSupervisionTimeoutMin = 0x000A;
constexpr uint16_t kLEConnectionSupervisionTimeoutMax = 0x0C80;

// The minimum and maximum range values for LE link layer tx PDU used on connections.
constexpr uint16_t kLEMaxTxOctetsMin = 0x001B;
constexpr uint16_t kLEMaxTxOctetsMax = 0x00FB;

// The minimum and maximum range values for LE link layer tx maximum packet transmission time used
// on connections.
constexpr uint16_t kLEMaxTxTimeMin = 0x0148;
constexpr uint16_t kLEMaxTxTimeMax = 0x4290;

// Minimum, maximum, default values for the Resolvable Private Address timeout parameter.
constexpr uint16_t kLERPATimeoutMin = 0x0001;      // 1 second
constexpr uint16_t kLERPATimeoutMax = 0xA1B8;      // Approx. 11.5 hours
constexpr uint16_t kLERPATimeoutDefault = 0x0384;  // 900 seconds or 15 minutes.

// LE advertising types (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5).
enum class LEAdvertisingType : uint8_t {
  // ADV_IND: Connectable and scannable undirected advertising (the default
  // value used by the controller).
  kAdvInd = 0x00,

  // ADV_DIRECT_IND (high duty cycle): Connectable high duty cycle directed
  // advertising.
  kAdvDirectIndHighDutyCycle = 0x01,

  // ADV_SCAN_IND: Scannable undirected advertising.
  kAdvScanInd = 0x02,

  // ADV_NONCONN_IND: Non-connectable undirected advertising.
  kAdvNonConnInd = 0x03,

  // ADV_DIRECT_IND (low duty cycle): Connectable low duty cycle advertising.
  kAdvDirectIndLowDutyCycle = 0x04,

  // The rest is reserved for future use
};

// LE Advertising event types that can be reported in a LE Advertising Report
// event.
enum class LEAdvertisingEventType : uint8_t {
  // Connectable and scannable undirected advertising (ADV_IND)
  kAdvInd = 0x00,

  // Connectable directed advertising (ADV_DIRECT_IND)
  kAdvDirectInd = 0x01,

  // Scannable undirected advertising (ADV_SCAN_IND)
  kAdvScanInd = 0x02,

  // Non connectable undirected advertising (ADV_NONCONN_IND)
  kAdvNonConnInd = 0x03,

  // Scan Response (SCAN_RSP)
  kScanRsp = 0x04,

  // The rest is reserved for future use
};

// Possible values that can be reported for the |address_type| parameter in a LE
// Advertising Report event.
enum class LEAddressType : uint8_t {
  // Public Device Address
  kPublic = 0x00,

  // Random Device Address
  kRandom = 0x01,

  // Public Identity Address (Corresponds to Resolved Private Address)
  kPublicIdentity = 0x02,

  // Random (static) Identity Address (Corresponds to Resolved Private Address)
  kRandomIdentity = 0x03,

  // This is a special value used in LE Extended Advertising Report events to indicate a random
  // address that the controller was unable to resolve.
  kRandomUnresolved = 0xFE,

  // This is a special value that is only used in LE Directed Advertising Report events.
  // Meaning: No address provided (anonymous advertisement)
  kAnonymous = 0xFF,
};

// Possible values that can be used for the |own_address_type| parameter in a
// HCI_LE_Set_Advertising_Parameters or a HCI_LE_Set_Scan_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Sections 7.8.5 and 7.8.10)
enum class LEOwnAddressType : uint8_t {
  // Public device address (default)
  kPublic = 0x00,

  // Random device address
  kRandom = 0x01,

  // Controller generates Resolvable Private Address based on the local IRK from
  // the resolving list. If the resolving list contains no matching entry, use
  // the public address.
  kPrivateDefaultToPublic = 0x02,

  // Controller generates Resolvable Private Address based on the local IRK from
  // the resolving list. If the resolving list contains no matching entry, use
  // the random address from LE_Set_Random_Address.
  kPrivateDefaultToRandom = 0x03,

  // The rest is reserved for future use
};

// Possible values that can be used for the |peer_address_type| parameter in a
// HCI_LE_Set_Advertising_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5)
enum class LEPeerAddressType : uint8_t {
  // Public Device Address (default) or Public Identity Address
  kPublic = 0x00,

  // Random Device Address or Random (static) Identity Address
  kRandom = 0x01,

  // This is a special value that should only be used with the HCI_LE_Add_Device_To_White_List and
  // HCI_LE_Remove_Device_From_White_List commands for peer devices sending anonymous
  // advertisements.
  kAnonymous = 0xFF,
};

// Possible values that can be used for the |adv_channel_map| bitfield in a
// HCI_LE_Set_Advertising_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5)
constexpr uint8_t kLEAdvertisingChannel37 = 0x01;
constexpr uint8_t kLEAdvertisingChannel38 = 0x02;
constexpr uint8_t kLEAdvertisingChannel39 = 0x04;
constexpr uint8_t kLEAdvertisingChannelAll = 0x07;

// Possible values that can be used for the |adv_filter_policy| parameter in a
// HCI_LE_Set_Advertising_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5)
enum class LEAdvFilterPolicy : uint8_t {
  // Process scan and connection requests from all devices (i.e., the White List
  // is not in use) (default).
  kAllowAll = 0x00,

  // Process connection requests from all devices and only scan requests from
  // devices that are in the White List.
  kConnAllScanWhiteList = 0x01,

  // Process scan requests from all devices and only connection requests from
  // devices that are in the White List.
  kScanAllConnWhiteList = 0x02,

  // Process scan and connection requests only from devices in the White List.
  kWhiteListOnly = 0x03,

  // The rest is reserved for future use.
};

// Possible values that can be used for the Filter_Policy parameter in a
// HCI_LE_Periodic_Advertising_Create_Sync command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.67)
enum class LEPeriodicAdvFilterPolicy : uint8_t {
  // Use the Advertising_SID, Advertising_Address_Type, and Advertising_Address parameters to
  // determine which advertiser to listen to.
  kListNotUsed = 0x00,

  // Use the Periodic Advertiser List to determine which advertiser to listen to.
  kUsePeriodicAdvList = 0x01,
};

// Possible values that can be used for the |scan_type| parameter in a
// LE_Set_Scan_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.10)
enum class LEScanType : uint8_t {
  // Passive Scanning. No scanning PDUs shall be sent (default)
  kPassive = 0x00,

  // Active scanning. Scanning PDUs may be sent.
  kActive = 0x01,

  // The rest is reserved for future use.
};

// Possible values that can be used for the |filter_policy| parameter
// in a HCI_LE_Set_Scan_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.10)
enum class LEScanFilterPolicy : uint8_t {
  // Accept all advertising packets except directed advertising packets not
  // addressed to this device (default).
  kNoWhiteList = 0x00,

  // Accept only advertising packets from devices where the advertiser’s address
  // is in the White List. Directed advertising packets which are not addressed
  // to this device shall be ignored.
  kUseWhiteList = 0x01,

  // Accept all advertising packets except directed advertising packets where
  // the initiator's identity address does not address this device.
  // Note: Directed advertising packets where the initiator's address is a
  // resolvable private address that cannot be resolved are also accepted.
  kNoWhiteListWithPrivacy = 0x02,

  // Accept all advertising packets except:
  //
  //   - advertising packets where the advertiser's identity address is not in
  //     the White List; and
  //
  //   - directed advertising packets where the initiator's identity address
  //     does not address this device
  //
  // Note: Directed advertising packets where the initiator's address is a
  // resolvable private address that cannot be resolved are also accepted.
  kUseWhiteListWithPrivacy = 0x03,
};

// Possible values that can be used for the |filter_duplicates| parameter in a
// HCI_LE_Set_Extended_Scan_Enable command.
enum class LEExtendedDuplicateFilteringOption : uint8_t {
  kDisabled = 0x00,
  kEnabled = 0x01,

  // Duplicate advertisements in a single scan period should not be sent to the Host in advertising
  // report events; this setting shall only be used if the Period parameter is non-zero.
  kEnabledResetForEachScanPeriod = 0x02,
};

// The PHY bitfield values that can be used in HCI_LE_Set_PHY and HCI_LE_Set_Default_PHY commands
// that can be used for the TX_PHYS and RX_PHYS parameters.
constexpr uint8_t kLEPHYBit1M = (1 << 0);
constexpr uint8_t kLEPHYBit2M = (1 << 1);
constexpr uint8_t kLEPHYBitCoded = (1 << 2);

// The PHY bitfield values that can be used in HCI_LE_Set_PHY and HCI_LE_Set_Default_PHY commands
// that can be used for the ALL_PHYS parameter.
constexpr uint8_t kLEAllPHYSBitTxNoPreference = (1 << 0);
constexpr uint8_t kLEAllPHYSBitRxNoPreference = (1 << 1);

// Potential values that can be used for the LE PHY parameters in HCI commands and events.
enum class LEPHY : uint8_t {
  kLE1M = 0x01,
  kLE2M = 0x02,

  // Only for the HCI_LE_Enhanced_Transmitter_Test command this value implies S=8 data coding.
  // Otherwise this refers to general LE Coded PHY.
  kLECoded = 0x03,

  // This should ony be used with the HCI_LE_Enhanced_Transmitter_Test command.
  kLECodedS2 = 0x04,

  // Some HCI events may use this value to indicate that no packets were sent on a particular PHY,
  // specifically the LE Extended Advertising Report event.
  kNone = 0x00,
};

// Potential values that can be used for the PHY_options parameter in a HCI_LE_Set_PHY command.
enum class LEPHYOptions : uint16_t {
  kNoPreferredEncoding = 0x00,
  kPreferS2Coding = 0x01,
  kPreferS8Coding = 0x02,
};

// Potential values that can be passed for the Modulation_Index parameter in a
// HCI_LE_Enhanced_Receiver_Test command.
enum class LETestModulationIndex : uint8_t {
  kAssumeStandard = 0x00,
  kAssumeStable = 0x01,
};

// Potential values for the Operation parameter in a HCI_LE_Set_Extended_Advertising_Data command.
enum class LESetExtendedAdvDataOp : uint8_t {
  // Intermediate fragment of fragmented extended advertising data.
  kIntermediateFragment = 0x00,

  // First fragment of fragmented extended advertising data.
  kFirstFragment = 0x01,

  // Last fragment of fragmented extended advertising data.
  kLastFragment = 0x02,

  // Complete extended advertising data.
  kComplete = 0x03,

  // Unchanged data (just update the Advertising DID)
  kUnchangedData = 0x04,
};

// Potential values for the Fragment_Preference parameter in a HCI_LE_Set_Extended_Advertising_Data
// command.
enum class LEExtendedAdvFragmentPreference : uint8_t {
  // The Controller may fragment all Host advertising data
  kMayFragment = 0x00,

  // The Controller should not fragment or should minimize fragmentation of Host advertising data
  kShouldNotFragment = 0x01,
};

// The Advertising_Event_Properties bitfield values used in a
// HCI_LE_Set_Extended_Advertising_Parameters command.
constexpr uint16_t kLEAdvEventPropBitConnectable                      = (1 << 0);
constexpr uint16_t kLEAdvEventPropBitScannable                        = (1 << 1);
constexpr uint16_t kLEAdvEventPropBitDirected                         = (1 << 2);
constexpr uint16_t kLEAdvEventPropBitHighDutyCycleDirectedConnectable = (1 << 3);
constexpr uint16_t kLEAdvEventPropBitUseLegacyPDUs                    = (1 << 4);
constexpr uint16_t kLEAdvEventPropBitAnonymousAdvertising             = (1 << 5);
constexpr uint16_t kLEAdvEventPropBitIncludeTxPower                   = (1 << 6);

// The Event_Type bitfield values reported in a LE Extended Advertising Report Event.
constexpr uint16_t kLEExtendedAdvEventTypeConnectable  = (1 << 0);
constexpr uint16_t kLEExtendedAdvEventTypeScannable    = (1 << 1);
constexpr uint16_t kLEExtendedAdvEventTypeDirected     = (1 << 2);
constexpr uint16_t kLEExtendedAdvEventTypeScanResponse = (1 << 3);
constexpr uint16_t kLEExtendedAdvEventTypeLegacy       = (1 << 4);

// LE Advertising data status properties stored in bits 5 and 6 of the Event_Type bitfield of a LE
// Extended Advertising Report event and in a LE Periodic Advertising Report event.
enum class LEAdvertisingDataStatus : uint16_t {
  // Data is complete.
  kComplete = 0x00,

  // Data is incomplete, more data to come in future events.
  kIncomplete = 0x01,

  // Data is incomplete and truncated, no more data to come.
  kIncompleteTruncated = 0x02,
};

// The Periodic_Advertising_Properties bitfield used in a HCI_LE_Set_Periodic_Advertising_Parameters
// command.
constexpr uint16_t kLEPeriodicAdvPropBitIncludeTxPower = (1 << 6);

// Potential values for the Privacy_Mode parameter in a HCI_LE_Set_Privacy_Mode command.
enum class LEPrivacyMode : uint8_t {
  // Use Network Privacy Mode for this peer device (default).
  kNetwork = 0x00,

  // Use Device Privacy Mode for this peer device.
  kDevice = 0x01,
};

// The maximum length of advertising data that can get passed to the
// HCI_LE_Set_Advertising_Data command.
//
// This constant should be used on pre-5.0 controllers. On controllers that support 5.0+ the host
// should use the HCI_LE_Read_Maximum_Advertising_Data_Length command to obtain this information.
constexpr size_t kMaxLEAdvertisingDataLength = 0x1F;  // (31)

// The maximum length of advertising data that can get passed to the
// HCI_LE_Set_Extended_Advertising_Data command. The advertised data can be larger than this value
// (based on the value returned for the LE Read Maximum Advertising Data Length parameter), however
// must be fragmented among multiple command packets.
constexpr size_t kMaxLEExtendedAdvertisingDataLength = 251;

// Maximum value of the Advertising SID subfield in the ADI field of the PDU
constexpr uint8_t kLEAdvertsingSIDMax = 0xEF;

// Invalid RSSI value.
constexpr int8_t kRSSIInvalid = 127;

// Invalid Tx Power value
constexpr int8_t kTxPowerInvalid = 127;

// The maximum length of Local Name that can be assigned to a BR/EDR controller,
// in octets.
constexpr size_t kMaxLocalNameLength = 248;

// The maximum number of bytes in a HCI Command Packet payload, excluding the
// header. See Core Spec v5.0 Vol 2, Part E, 5.4.1, paragraph 2.
constexpr size_t kMaxCommandPacketPayloadSize = 255;

// The maximum number of bytes in a HCI event Packet payload, excluding the
// header. See Core Spec v5.0 Vol 2, Part E, 5.4.4, paragraph 1.
constexpr size_t kMaxEventPacketPayloadSize = 255;

// The maximum number of bytes in a HCI ACL data packet payload supported by our stack.
constexpr size_t kMaxACLPayloadSize = 1024;

// Values that can be used in HCI Read|WriteFlowControlMode commands.
enum class FlowControlMode : uint8_t {
  // Packet based data flow control mode (default for a Primary Controller)
  kPacketBased = 0x00,

  // Data block based flow control mode (default for an AMP Controller)
  kDataBlockBased = 0x01
};

// The Packet Boundary Flag is contained in bits 4 and 5 in the second octet of a HCI ACL Data
// packet.
enum class ACLPacketBoundaryFlag : uint8_t {
  kFirstNonFlushable  = 0x00,
  kContinuingFragment = 0x01,
  kFirstFlushable     = 0x02,
  kCompletePDU        = 0x03,
};

// The Broadcast Flag is contained in bits 6 and 7 in the second octet of a HCI ACL Data packet.
enum class ACLBroadcastFlag : uint8_t {
  kPointToPoint         = 0x00,
  kActiveSlaveBroadcast = 0x01,
};

// The LE connection role.
enum class LEConnectionRole : uint8_t {
  kMaster = 0x00,
  kSlave = 0x01,
};

// Possible values that can be reported for the Master_Clock_Accuracy and Advertiser_Clock_Accuracy
// parameters.
enum class LEClockAccuracy : uint8_t {
  k500Ppm = 0x00,
  k250Ppm = 0x01,
  k150Ppm = 0x02,
  k100Ppm = 0x03,
  k75Ppm = 0x04,
  k50Ppm = 0x05,
  k30Ppm = 0x06,
  k20Ppm = 0x07,
};

// Possible values that can be reported in a LE Channel Selection Algorithm event.
enum class LEChannelSelectionAlgorithm : uint8_t {
  kAlgorithm1 = 0x00,
  kAlgorithm2 = 0x01,
};

// "Hosts and Controllers shall be able to accept HCI ACL Data Packets with up to 27 bytes of data
// excluding the HCI ACL Data Packet header on Connection Handles associated with an LE-U logical
// link." (See Core Spec v5.0, Volume 2, Part E, Section 5.4.2)
constexpr size_t kMinLEACLDataBufferLength = 27;

// The maximum value that can be used for a 12-bit connection handle.
constexpr uint16_t kConnectionHandleMax = 0x0EFF;

// The maximum value that can ve used for a 8-bit advertising set handle.
constexpr uint8_t kAdvertisingHandleMax = 0xEF;

// The maximum value that can be sert for the length of an Inquiry
constexpr uint8_t kInquiryLengthMax = 0x30;

// The page scan repetition mode, representing a maximum time between Page Scans.
// (See Core Spec v5.0, Volume 2, Part B, Section 8.3.1)
enum class PageScanRepetitionMode : uint8_t {
  kR0 = 0x00, // Continuous Scan
  kR1 = 0x01, // <= 1.28s
  kR2 = 0x02, // <= 2.56s
};

// LAPs defined for use in Inquiry LAP fields by Bluetooth SIG
// See: https://www.bluetooth.com/specifications/assigned-numbers/baseband
// The General/Unlimited Inquiry Access Code
constexpr std::array<uint8_t, 3> kGIAC {{ 0x33, 0x8B, 0x9E }};

// The Limited Dedicated Inquiry Access Code (LIAC)
constexpr std::array<uint8_t, 3> kLIAC {{ 0x00, 0x8B, 0x9E }};

}  // namespace hci
}  // namespace btlib
