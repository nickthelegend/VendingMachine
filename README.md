# QR Code Display Project

This Arduino project displays QR codes on a TFT display using an ESP32 or ESP8266 microcontroller.

## Hardware Requirements

- ESP32 or ESP8266 development board
- TFT display compatible with TFT_eSPI library
- Connecting wires

## Software Requirements

- Arduino IDE 1.8.0 or later
- ESP32/ESP8266 board support in Arduino IDE

## Library Installation

### 1. Install TFT_eSPI Library

The TFT_eSPI library provides graphics and text functionality for TFT displays.

**Installation via Arduino IDE Library Manager:**
1. Open Arduino IDE
2. Go to **Sketch > Include Library > Manage Libraries**
3. Search for "TFT_eSPI"
4. Install the latest version by Bodmer

**Alternative Installation (via ZIP file):**
1. Download the library from: https://github.com/Bodmer/TFT_eSPI
2. Extract the ZIP file
3. Copy the "TFT_eSPI" folder to your Arduino libraries folder:
   - Windows: `Documents/Arduino/libraries/`
   - macOS: `Documents/Arduino/libraries/`
   - Linux: `Arduino/libraries/`

### 2. Install QRcode_eSPI Library

The QRcode_eSPI library generates QR codes specifically for TFT_eSPI displays.

**Installation via Arduino IDE Library Manager:**
1. Open Arduino IDE
2. Go to **Sketch > Include Library > Manage Libraries**
3. Search for "QRcode_eSPI"
4. Install the latest version

**Alternative Installation (via ZIP file):**
1. Download the library from: https://github.com/bxparks/QRcode_eSPI
2. Extract the ZIP file
3. Copy the "QRcode_eSPI" folder to your Arduino libraries folder

### 3. Built-in Libraries

The SPI library (`SPI.h`) is included with the Arduino IDE and doesn't require separate installation.

## TFT_eSPI Display Configuration

The TFT_eSPI library requires configuration for your specific display. You need to edit the `User_Setup.h` file:

### Step 1: Locate the User_Setup.h file

The file is typically located in:
- `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`

### Step 2: Configure Display Settings

Open `User_Setup.h` and uncomment/modify the appropriate lines for your display. Here are some common configurations:

**For ESP32 with ILI9341 display:**
```cpp
#define ILI9341_DRIVER
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
```

**For ESP32 with ST7735 display:**
```cpp
#define ST7735_DRIVER
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
```

**For ESP8266 with ILI9341 display:**
```cpp
#define ILI9341_DRIVER
#define TFT_MOSI 13  // D7
#define TFT_SCLK 14  // D5
#define TFT_CS   15  // D8
#define TFT_DC   2   // D4
#define TFT_RST  0   // D3
```

### Step 3: Enable SPI and Display

In the `User_Setup.h` file, ensure these lines are uncommented:
```cpp
#define TFT_SPI_PORT 0  // SPI port (varies by board)
#define SPI_FREQUENCY  40000000  // SPI frequency
#define SPI_READ_FREQUENCY  20000000  // Read frequency
```

### Step 4: Verify Configuration

After making changes:
1. Save the `User_Setup.h` file
2. Restart Arduino IDE
3. Compile the sketch to check for any configuration errors

## Usage

1. Connect your TFT display to the ESP32/ESP8266 according to your pin configuration
2. Open `QR_CODE.ino` in Arduino IDE
3. Select your board (ESP32 or ESP8266) in **Tools > Board**
4. Select the appropriate port in **Tools > Port**
5. Upload the sketch

The sketch will display "Hello world." as a QR code on your TFT display.

## Customization

To display different text as QR code, modify line 22 in `QR_CODE.ino`:
```cpp
qrcode.create("Your text here.");
```

## Troubleshooting

### Common Issues:

1. **Display not working:**
   - Check your `User_Setup.h` configuration matches your hardware
   - Verify wiring connections
   - Ensure correct board selection in Arduino IDE

2. **Library compilation errors:**
   - Make sure all libraries are properly installed
   - Check that `User_Setup.h` is correctly configured
   - Restart Arduino IDE after library installation

3. **QR code not displaying:**
   - Verify TFT display initialization is successful
   - Check serial monitor for error messages
   - Uncomment `qrcode.debug();` in setup() for debugging information

## License

This project is open source. Please refer to individual library licenses for more information.