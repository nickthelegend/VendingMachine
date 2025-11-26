# Design Document

## Overview

This design extends the ESP32 vending machine firmware to support multiple blockchain networks (Algorand and Cardano) with runtime selection, and adds configurable display orientation with 180-degree rotation support. The implementation maintains backward compatibility while adding new configuration options to the provisioning interface.

## Architecture

The system follows a layered architecture:

1. **Configuration Layer**: Manages persistent storage of blockchain selection and display settings
2. **Network Layer**: Handles blockchain-specific API endpoints and Supabase connections
3. **Display Layer**: Manages TFT display with configurable rotation
4. **Web Interface Layer**: Provides user controls for blockchain and display configuration

### Key Design Decisions

- **Single Configuration Page**: Blockchain and display settings are added to the existing provisioning page to minimize user navigation
- **Runtime Configuration**: All settings can be changed without firmware recompilation
- **Persistent Storage**: Uses ESP32 Preferences library to store blockchain and display settings
- **Conditional Compilation**: No conditional compilation needed; all configurations are runtime-based

## Components and Interfaces

### 1. Configuration Manager

**Purpose**: Centralize blockchain and display configuration management

**Data Structure**:
```cpp
struct ChainConfig {
  String name;                    // "algorand" or "cardano"
  String supabaseUrl;
  String supabaseAnonKey;
  String merchantApiBase;         // e.g., "merchant.abcxjntuh.in/api/"
  String paymentUrlBase;          // e.g., "vendchain.abcxjntuh.in/pay/"
};
```

**Global Variables**:
```cpp
ChainConfig algorandConfig;
ChainConfig cardanoConfig;
ChainConfig* activeChain;         // Pointer to active configuration
int displayRotation = 0;          // 0 or 2 (for 0° or 180°)
```

**Functions**:
- `void initChainConfigs()`: Initialize both blockchain configurations
- `void loadSettings()`: Load blockchain selection and display rotation from NVM
- `void saveSettings()`: Persist blockchain selection and display rotation to NVM
- `void setActiveChain(String chainName)`: Switch active blockchain configuration

### 2. Display Manager

**Purpose**: Handle display initialization and rotation management

**Functions**:
- `void initDisplay()`: Initialize display with saved rotation setting
- `void setDisplayRotation(int rotation)`: Apply rotation (0 or 2) to display
- `void applyRotationToAllScreens()`: Ensure rotation is applied consistently

**Modified Functions**:
- `showAPQRCode()`: Apply rotation before rendering
- `showPaymentQRCode()`: Apply rotation before rendering
- `showMessageOnDisplay()`: Apply rotation before rendering

### 3. Web Interface Enhancements

**Purpose**: Add UI controls for blockchain and display configuration

**HTML Additions to Provisioning Page**:
```html
<label>Blockchain Network</label><br>
<select name="chain" required>
  <option value="algorand">Algorand</option>
  <option value="cardano">Cardano</option>
</select><br>

<label>
  <input type="checkbox" name="rotate180" value="1"> Rotate Display 180°
</label><br>
```

**Modified Handlers**:
- `handleSave()`: Parse and save blockchain and rotation settings
- `generateProvisioningPage()`: Include blockchain and rotation controls

### 4. API Endpoint Manager

**Purpose**: Provide blockchain-specific URLs for API calls

**Functions**:
- `String getMerchantApiUrl(String apiKey)`: Return full merchant API URL
- `String getPaymentUrl(String deviceId)`: Return full payment URL
- `String getWebSocketPath()`: Return WebSocket path with correct API key

**Implementation**:
```cpp
String getMerchantApiUrl(String apiKey) {
  return String("https://") + activeChain->merchantApiBase + apiKey;
}

String getPaymentUrl(String deviceId) {
  return String("https://") + activeChain->paymentUrlBase + deviceId;
}
```

### 5. WebSocket Connection Manager

**Purpose**: Establish WebSocket connection using active blockchain's Supabase configuration

**Modified Functions**:
- `setupWebSocket()`: Use `activeChain->supabaseUrl` and `activeChain->supabaseAnonKey`
- Connection string construction uses active chain's credentials

## Data Models

### Blockchain Configuration

```cpp
// Algorand Configuration (existing)
algorandConfig = {
  .name = "algorand",
  .supabaseUrl = "lhnbipgsxrvonbblcekw.supabase.co",
  .supabaseAnonKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  .merchantApiBase = "merchant.abcxjntuh.in/api/",
  .paymentUrlBase = "vendchain.abcxjntuh.in/pay/"
};

// Cardano Configuration (new)
cardanoConfig = {
  .name = "cardano",
  .supabaseUrl = "ifxllaqnfvupxhsxtscs.supabase.co",
  .supabaseAnonKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImlmeGxsYXFuZnZ1cHhoc3h0c2NzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjMxMzgwMDIsImV4cCI6MjA3ODcxNDAwMn0.cSSdVNa2biPFbni5sufmSdTn67CDaefo0I2AdElaMzM",
  .merchantApiBase = "cardano-vending-merchant.vercel.app/api/",
  .paymentUrlBase = "cardano-vending-machine.vercel.app/pay/"
};
```

### Persistent Storage Schema

**Preferences Namespace: "settings"**
- Key: "chain" → Value: "algorand" or "cardano"
- Key: "rotation" → Value: 0 or 2

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Chain configuration consistency
*For any* blockchain selection (Algorand or Cardano), all API calls (merchant verification, payment URL generation, WebSocket connection) should use the endpoints and credentials associated with that blockchain
**Validates: Requirements 1.2, 1.3, 2.1, 2.2, 2.3, 2.4, 3.1, 3.2, 3.3, 3.4**

### Property 2: Settings persistence round-trip
*For any* valid blockchain selection and rotation setting, saving then loading the configuration should produce equivalent values
**Validates: Requirements 1.4, 1.5, 4.5, 4.6**

### Property 3: Display rotation consistency
*For any* display rotation setting (0° or 180°), all display operations (QR codes, status messages, payment screens) should apply the same rotation value
**Validates: Requirements 4.1, 4.3, 4.4, 5.1, 5.2, 5.3, 5.4**

### Property 4: Rotation setting application
*For any* rotation setting change, the display should immediately reflect the new orientation on the currently visible screen
**Validates: Requirements 5.4**

### Property 5: QR code scannability
*For any* QR code content and rotation setting (0° or 180°), the generated QR code should remain scannable by standard QR readers
**Validates: Requirements 5.5**

## Error Handling

### Configuration Errors

1. **Invalid Blockchain Selection**: Default to Algorand if saved value is neither "algorand" nor "cardano"
2. **Invalid Rotation Value**: Default to 0 if saved value is not 0 or 2
3. **Missing Configuration**: Use Algorand and 0° rotation as defaults

### Network Errors

1. **API Endpoint Unreachable**: Display error message on screen and in serial console
2. **WebSocket Connection Failure**: Retry with exponential backoff (existing behavior)
3. **Supabase Authentication Failure**: Log error and attempt reconnection

### Display Errors

1. **Display Initialization Failure**: Log to serial, continue with default rotation
2. **QR Code Generation Failure**: Display error message instead of QR code

## Testing Strategy

### Unit Testing

Unit tests will verify:
- Configuration initialization with correct default values
- Settings persistence and retrieval from NVM
- URL construction for different blockchain selections
- Display rotation value validation

### Property-Based Testing

Property-based tests will verify the correctness properties defined above using a suitable C++ property testing framework or manual validation with varied inputs:

1. **Chain Configuration Consistency Test**: Generate random blockchain selections and verify all API calls use consistent endpoints
2. **Settings Persistence Test**: Generate random valid configurations, save and load them, verify equivalence
3. **Display Rotation Test**: Generate random rotation values and screen types, verify consistent application
4. **QR Code Scannability Test**: Generate random QR content with both rotations, verify scannability

### Integration Testing

Integration tests will verify:
- End-to-end provisioning flow with blockchain selection
- Display rotation changes reflected across all screens
- WebSocket connection establishment with correct Supabase instance
- Payment flow completion on selected blockchain

### Manual Testing Checklist

1. Provision device with Algorand, verify merchant API call uses correct endpoint
2. Provision device with Cardano, verify merchant API call uses correct endpoint
3. Toggle display rotation, verify QR codes rotate correctly
4. Restart device, verify blockchain and rotation settings persist
5. Complete payment on Algorand, verify dispensing works
6. Complete payment on Cardano, verify dispensing works
7. Scan QR codes in both rotation modes, verify scannability

## Implementation Notes

### Backward Compatibility

- Devices with no saved blockchain selection will default to Algorand (existing behavior)
- Devices with no saved rotation will default to 0° (existing behavior)
- Existing API calls will work unchanged when Algorand is selected

### Performance Considerations

- Configuration loading happens once at boot (minimal overhead)
- Display rotation setting is applied before each screen render (negligible overhead)
- No additional network calls introduced

### Security Considerations

- Supabase anonymous keys are embedded in firmware (existing approach)
- No sensitive data transmitted in provisioning interface
- HTTPS used for all API calls (existing approach)

### Future Enhancements

- Support for additional blockchains (Ethereum, Solana, etc.)
- Dynamic blockchain configuration via API
- Display rotation in 90° increments (all four orientations)
- Remote configuration updates via WebSocket
