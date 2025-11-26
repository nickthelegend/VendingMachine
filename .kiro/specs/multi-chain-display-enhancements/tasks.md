# Implementation Plan

- [x] 1. Create blockchain configuration data structures and initialization







  - Define ChainConfig struct with all required fields (name, supabaseUrl, supabaseAnonKey, merchantApiBase, paymentUrlBase)
  - Create global variables for algorandConfig, cardanoConfig, and activeChain pointer
  - Implement initChainConfigs() function to populate both configurations with correct values
  - Add global variable for displayRotation setting
  - _Requirements: 1.2, 1.3, 2.1, 2.2, 2.3, 2.4, 3.1, 3.2, 3.3_

- [x] 2. Implement settings persistence functions



  - Create loadSettings() function to read blockchain selection and rotation from Preferences
  - Create saveSettings() function to write blockchain selection and rotation to Preferences
  - Implement setActiveChain() function to switch between blockchain configurations
  - Add default value handling (Algorand and 0° rotation as defaults)
  - _Requirements: 1.4, 1.5, 4.5, 4.6_

- [ ]* 2.1 Write property test for settings persistence
  - **Property 2: Settings persistence round-trip**
  - **Validates: Requirements 1.4, 1.5, 4.5, 4.6**

- [x] 3. Add blockchain and rotation controls to provisioning web interface



  - Modify generateProvisioningPage() to include blockchain selection dropdown (Algorand/Cardano)
  - Add checkbox for "Rotate Display 180°" option
  - Update HTML form to include new input fields with appropriate names
  - _Requirements: 1.1, 4.2_

- [x] 4. Update handleSave() to process blockchain and rotation settings



  - Parse "chain" parameter from form submission
  - Parse "rotate180" checkbox parameter from form submission
  - Call setActiveChain() with selected blockchain
  - Set displayRotation to 0 or 2 based on checkbox
  - Call saveSettings() to persist both settings
  - _Requirements: 1.2, 1.3, 1.4, 4.3, 4.4, 4.5_

- [ ]* 4.1 Write property test for chain configuration consistency
  - **Property 1: Chain configuration consistency**
  - **Validates: Requirements 1.2, 1.3, 2.1, 2.2, 2.3, 2.4, 3.1, 3.2, 3.3, 3.4**

- [x] 5. Create API endpoint helper functions


  - Implement getMerchantApiUrl(String apiKey) using activeChain->merchantApiBase
  - Implement getPaymentUrl(String deviceId) using activeChain->paymentUrlBase
  - Ensure functions return complete HTTPS URLs
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

- [x] 6. Update merchant verification to use dynamic API endpoint


  - Modify handleAddProduct() to call getMerchantApiUrl() instead of hardcoded URL
  - Verify API calls work with both Algorand and Cardano endpoints
  - _Requirements: 2.1, 2.3, 2.5_

- [x] 7. Update payment QR code generation to use dynamic URL



  - Modify showPaymentQRCode() to call getPaymentUrl() instead of hardcoded URL
  - Ensure QR codes contain correct blockchain-specific payment URLs
  - _Requirements: 2.2, 2.4, 2.5_

- [x] 8. Update WebSocket connection to use active blockchain configuration


  - Modify setupWebSocket() to use activeChain->supabaseUrl
  - Update WebSocket path to use activeChain->supabaseAnonKey
  - Ensure WebSocket connects to correct Supabase instance based on blockchain selection
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

- [x] 9. Implement display rotation management functions


  - Create initDisplay() function that calls display.init() and applies saved rotation
  - Create setDisplayRotation(int rotation) function to apply rotation to display object
  - Add rotation validation (only allow 0 or 2)
  - _Requirements: 4.1, 4.3, 4.4, 4.6_

- [x] 10. Apply rotation to all display functions


  - Modify showAPQRCode() to call display.setRotation(displayRotation) before rendering
  - Modify showPaymentQRCode() to call display.setRotation(displayRotation) before rendering
  - Modify showMessageOnDisplay() to call display.setRotation(displayRotation) before rendering
  - _Requirements: 5.1, 5.2, 5.3, 5.4_

- [ ]* 10.1 Write property test for display rotation consistency
  - **Property 3: Display rotation consistency**
  - **Validates: Requirements 4.1, 4.3, 4.4, 5.1, 5.2, 5.3, 5.4**

- [ ]* 10.2 Write property test for rotation setting application
  - **Property 4: Rotation setting application**
  - **Validates: Requirements 5.4**

- [x] 11. Update setup() function to initialize new components



  - Call initChainConfigs() at startup
  - Call loadSettings() to restore blockchain and rotation preferences
  - Replace display.init() and display.setRotation(0) with initDisplay()
  - Ensure activeChain is set before any API calls
  - _Requirements: 1.5, 4.6_

- [ ] 12. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ]* 13. Manual testing and validation
  - Test provisioning with Algorand selection
  - Test provisioning with Cardano selection
  - Test display rotation toggle
  - Verify settings persist across device restart
  - Verify QR codes are scannable in both orientations
  - Test complete payment flow on both blockchains
  - _Requirements: All_
