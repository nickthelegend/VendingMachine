# Requirements Document

## Introduction

This document specifies enhancements to the ESP32-based vending machine system to support multiple blockchain networks (Algorand and Cardano) with a toggle mechanism in the WiFi provisioning interface, and to provide configurable display orientation for portrait mode with 180-degree rotation support.

## Glossary

- **ESP32**: The microcontroller device running the vending machine firmware
- **Display**: The TFT screen showing QR codes and status messages
- **Provisioning Interface**: The captive portal web interface for configuring WiFi and device settings
- **Blockchain Toggle**: A UI control allowing selection between Algorand and Cardano networks
- **Portrait Mode**: Display orientation where height is greater than width
- **Display Rotation**: The orientation angle of the display (0°, 90°, 180°, 270°)
- **Supabase**: The backend database service with separate instances for each blockchain
- **API Endpoint**: The server URL used for merchant verification and payment processing
- **Chain Configuration**: The set of parameters (Supabase URL, API key, endpoints) specific to a blockchain

## Requirements

### Requirement 1

**User Story:** As a vending machine operator, I want to select between Algorand and Cardano blockchains during device setup, so that I can process payments on my preferred blockchain network.

#### Acceptance Criteria

1. WHEN the provisioning interface loads THEN the system SHALL display a toggle button or radio selection for choosing between Algorand and Cardano
2. WHEN the operator selects Algorand THEN the system SHALL configure the device to use the Algorand Supabase database and API endpoints
3. WHEN the operator selects Cardano THEN the system SHALL configure the device to use the Cardano Supabase database (https://ifxllaqnfvupxhsxtscs.supabase.co) and Cardano API endpoints
4. WHEN a blockchain is selected THEN the system SHALL persist the selection to non-volatile memory
5. WHEN the device restarts THEN the system SHALL load and apply the previously selected blockchain configuration

### Requirement 2

**User Story:** As a vending machine operator, I want the system to use blockchain-specific API endpoints, so that payment verification and processing work correctly for the selected network.

#### Acceptance Criteria

1. WHEN Algorand is selected THEN the system SHALL use merchant.abcxjntuh.in/api/ for merchant verification
2. WHEN Algorand is selected THEN the system SHALL use vendchain.abcxjntuh.in/pay/ for payment URL generation
3. WHEN Cardano is selected THEN the system SHALL use cardano-vending-merchant.vercel.app/api/ for merchant verification
4. WHEN Cardano is selected THEN the system SHALL use cardano-vending-machine.vercel.app/pay/ for payment URL generation
5. WHEN the blockchain selection changes THEN the system SHALL update all API endpoint references immediately

### Requirement 3

**User Story:** As a vending machine operator, I want the system to connect to the correct Supabase database for the selected blockchain, so that payment notifications are received from the appropriate network.

#### Acceptance Criteria

1. WHEN Cardano is selected THEN the system SHALL use Supabase URL https://ifxllaqnfvupxhsxtscs.supabase.co
2. WHEN Cardano is selected THEN the system SHALL use the Cardano-specific Supabase anonymous key
3. WHEN Algorand is selected THEN the system SHALL use the existing Algorand Supabase configuration
4. WHEN establishing WebSocket connection THEN the system SHALL use the Supabase credentials matching the selected blockchain
5. WHEN payment notifications arrive THEN the system SHALL process them using the active blockchain's database connection

### Requirement 4

**User Story:** As a vending machine operator, I want to configure the display orientation to portrait mode with optional 180-degree rotation, so that the screen is readable regardless of how the device is physically mounted.

#### Acceptance Criteria

1. WHEN the device boots THEN the system SHALL set the display to portrait orientation by default
2. WHEN the provisioning interface loads THEN the system SHALL display a toggle or checkbox for "Rotate Display 180°"
3. WHEN the operator enables 180-degree rotation THEN the system SHALL apply rotation value 2 to the display
4. WHEN the operator disables 180-degree rotation THEN the system SHALL apply rotation value 0 to the display
5. WHEN display rotation is changed THEN the system SHALL persist the setting to non-volatile memory
6. WHEN the device restarts THEN the system SHALL apply the saved rotation setting to all display operations

### Requirement 5

**User Story:** As a vending machine operator, I want the display rotation to apply to all screens (QR codes, status messages, payment screens), so that the entire user interface is consistently oriented.

#### Acceptance Criteria

1. WHEN displaying the WiFi setup QR code THEN the system SHALL apply the configured rotation setting
2. WHEN displaying payment QR codes THEN the system SHALL apply the configured rotation setting
3. WHEN displaying status messages THEN the system SHALL apply the configured rotation setting
4. WHEN the rotation setting changes THEN the system SHALL immediately update the current display with the new orientation
5. WHEN generating QR codes THEN the system SHALL ensure they remain scannable in the configured orientation
