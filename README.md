# Tyzr: Move Your Legs, Fuel Your Focus!
#####

> **A**: Why is moving so important?  
> **B**: Because if you don’t, you’ll end up like the plugged-in humans in *The Matrix*—passive and stuck.

With Tyzr, put your extra M5Stack to good use—helping you stay focused, active, and productive.

## Features
- **Pomodoro-inspired 25/5 timer**: Work for 25 minutes, then take a 5-minute break.
- **Encourages healthy habits**: Stay active and boost circulation with gentle reminders.
- **Compatible with M5Stack devices**: Easy to set up on any M5Stack with a display.

## Hardware Required
- Any M5Stack device with a display, buttons, and a buzzer or speaker.
- A PC and a USB cable for flashing.

## Installation
1. Install the ESP-IDF.
   - Follow the instructions in the [Installation](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/index.html#installation) section of the ESP-IDF Getting Started guide.
   - It's highly recommended to complete the [Build Your First Project](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/index.html#build-your-first-project) section before proceeding.

2. Clone this repository:
   ```bash
   git clone https://github.com/tinyalg/tyzr.git
   ```

3. Configure your device:
   ```bash
   idf.py menuconfig
   ```
   - Review the Tyzr app config in `menuconfig`,
   and adjust settings if needed.
   - Make sure to configure the GPIO number for one of your M5Stack's buttons to use it as wakeup source.
   - See the Example Configurations below for suggested settings.

4. Build and flash the firmware:
   ```bash
   idf.py -p PORT flash monitor
   ```

   (Replace `PORT` with your device's serial port.)  

   (To exit the serial monitor, type `Ctrl-]`.)

## Quick Start
- **Start the timer**: Press `BUTTON A` to start the 25-minute work timer.
- **Take a break**: After 25 minutes, a gentle beep reminds you to move for 5 minutes.
- **Stop the timer**: Press `BUTTON A` again to stop the timer and turn off the display.
- **Restart**: Press `BUTTON B` to reset the timer and start immediately.
- **Wake up**: When the timer sleeps, press `BUTTON B` to wake it up.

## Example Configurations

| Setting                       | Default        | M5Stack Gray | M5StickC Plus |
|-------------------------------|----------------|--------------|---------------|
| GPIO number used as wakeup source | 39             | 37 (BUTTON C) | 39 (BUTTON B) |
| Use SPK HAT                   | -              | -            | ✓ (If you have one) |
| Screen Rotation               | 1              | 1            | 1             |
| X Coordinate of the digits    | 0              | 25           | 50            |
| Y Coordinate of the digits    | 0              | 70           | 30            |
| Scaling factor of the digits  | 1              | 2            | 1             |


## Troubleshooting

### No Sound from the Buzzer or Speaker
- **Cause**: Buzzer or speaker not properly connected or configured.  
- **Solution**: 
  1. Ensure the hardware connections are secure.
  2. If using SPK HAT, verify that it is enabled in `menuconfig`.

### The Timer Doesn't Respond After Sleep
- **Cause**: Wakeup GPIO not configured or pressed incorrectly.  
- **Solution**: Verify the GPIO number for the wakeup source in `menuconfig` and ensure it matches your M5Stack model.

#####

Enjoy the balance of productivity and well-being with Tyzr!
