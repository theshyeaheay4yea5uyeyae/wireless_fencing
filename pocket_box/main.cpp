#include "mbed.h"
#include "RF24.h"
#undef sprintf
#include "Adafruit_SSD1306.h"
using namespace std::chrono;
#include <cstring>

// Timing tool for debugging
Timer timer;

// Capacitive touch sensor interface
DigitalInOut touch(PA_0);

// Control pins for the weapon
DigitalOut pin_A(PA_5);
DigitalOut pin_C(PB_4);

// Button inputs for user interface
InterruptIn btn_confirm(PA_7, PullUp);
InterruptIn btn_select(PB_5, PullUp);

// I2C configuration for the OLED display
I2C i2c(PA_10, PA_9);
Adafruit_SSD1306_I2c gOled2(i2c, PB_7);

// RF24 radio module pin configuration
PinName mosi = PA_12;
PinName miso = PA_11;
PinName sck = PA_1;
PinName ce = PB_6;
PinName csn = PB_1;

// Instantiate RF24 object with the pins defined above
RF24 radio(mosi, miso, sck, ce, csn);

// Address definitions for the RF communication. These addresses are for identifying different devices in the RF network.
const uint8_t red_pocket_address[6] = "00001";
const uint8_t green_pocket_address[6] = "00002";
const uint8_t desk_address[6] = "00003";

// Variables for touch detection logic
int touch_counts[10] = {0};
int average = 0;
int triggered_count = 0;
int triggered_limit = 50;
int triggered_sensitivity = 15;

// Contact times for different weapons in ms
const int contact_times[] = { 14, 1, 5 };  // Foil, Saber, Epee

// Enumerations for better code readability
enum Mode { Foil, Saber, Epee };
Mode weapon_mode = Foil;
enum Stage { Start_Connection, Select_Weapon, Calibration, Standard, Triggered, Waiting  };
Stage current_stage = Start_Connection;
enum Player { Green, Red };
Player current_player = Green; // Default choice

// Lookup tables for display
const char* weaponNames[] = { "Foil", "Saber", "Epee" };
const char* playerColor[] = { "Green", "Red" };

// Function to detect touches using capacitive sensing
void detection() {
    for (int i = 0; i < 10; i++) {
        int count = 0;
        touch.input();
        for (int j = 0; j < 1000; j++) {
            if (!touch.read()) count++;
        }
        touch_counts[i] = count;
        touch.output();
        touch.write(0);
    }
}

// Calculate average of the touch counts
int update_average() {
    int sum = 0;
    for (int i : touch_counts) sum += i;
    return sum / 10;
}

// Initialize the RF24 module with the right configuration
void start_rf() {
    radio.begin();
    radio.setPayloadSize(3);
    radio.setChannel(100);
    if (current_player == Green) {
        radio.openWritingPipe(green_pocket_address);
    } else {
        radio.openWritingPipe(red_pocket_address);
    }
    radio.openReadingPipe(1, desk_address);
    radio.startListening();
}

// Handle confirmation button press
void confirm() {
    wait_us(100000);  // debounce
    if (!btn_confirm) {
        if (current_stage == Start_Connection) {
            start_rf();
            current_stage = Select_Weapon;
        }
        else if (current_stage == Calibration || current_stage == Waiting) {
            current_stage = Standard;
        }
    }
}

// Handle selection button press
void select() {
    wait_us(100000);  // debounce
    if (!btn_select) {
        if (current_stage == Start_Connection) {
            current_player = (current_player == Green) ? Red : Green;
        } else if (current_stage == Calibration) {
            triggered_sensitivity = (triggered_sensitivity == 20) ? 10 : triggered_sensitivity + 5;
        }
    }
}

// Display messages on OLED
void displayMessage(const char* msg = nullptr) {
    gOled2.clearDisplay();
    gOled2.setTextCursor(0, 0);
    if (msg) gOled2.printf("%s", msg);
    gOled2.display();
}

int main() {

    btn_confirm.fall(&confirm);  // Attach interrupt handlers
    btn_select.fall(&select);
    printf("Hello\r");
    
    // Main loop, driven by current stage
    while (1) {
        if (current_stage == Start_Connection) {
            // Display the current player's color
            if (current_player == Green) {
                displayMessage("Player: Green");
            } else {
                displayMessage("Player: Red");
            }
        }


        // Weapon selection logic based on radio communication
        if (current_stage == Select_Weapon) {
            char receivedPayload[5] = {0}; // Buffer to store the payload received via radio

            // Check if radio has data available
            if(radio.available()) {
                radio.read(receivedPayload, sizeof(receivedPayload));

                // Determine weapon mode based on the first character of the payload
                switch(receivedPayload[0]) {
                    case 's':
                        weapon_mode = Saber;
                        radio.flush_tx(); // Clear any pending data in the radio's buffer
                        break;
                    case 'f':
                        weapon_mode = Foil;
                        radio.flush_tx();
                        break;
                    case 'e':
                        weapon_mode = Epee;
                        radio.flush_tx();
                        break;
                    default:
                        break;
                }
            }

            // Display a message indicating waiting state
            displayMessage("Waiting selection\n of weapon...");
            continue;
        }

        // Calibration logic to adjust touch sensitivity
        if (current_stage == Calibration) {

            // Adjust pins based on weapon mode
            pin_A = 1;
            pin_C = (weapon_mode == Saber) ? 0 : 1;

            // Detect touches and update average
            detection();
            average = update_average();

            // Adjust triggered limit based on readings
            if (average > triggered_limit) triggered_limit = average - triggered_sensitivity;
            char buffer[100];
            sprintf(buffer, "%s\ntriggered_limit: %d\naverage: %d\r\nsensitivity: %d\n\r", weaponNames[weapon_mode], triggered_limit, average, triggered_sensitivity);
            displayMessage(buffer);

            // Check radio for further instructions
            char receivedPayload[5] = {0};
            if(radio.available()) {
                radio.read(receivedPayload, sizeof(receivedPayload));
                if(receivedPayload[0] == 'b') {
                    current_stage = Standard;
                    radio.flush_tx();
                }
            }
        } 
        // Main weapon logic to detect hits
        else if (current_stage == Standard) {
            detection();
            average = update_average();

            // Check if weapon was triggered based on mode
            bool triggered = (weapon_mode == Foil && average > 50) || 
                             (weapon_mode == Saber && average > triggered_limit) || 
                             (weapon_mode == Epee && average < 50);
            if (triggered) current_stage = Triggered;
        } 
        // Main weapon logic after detect a hit
        else if (current_stage == Triggered) {
            //timer.start(); 
            detection();
            average = update_average();
            // Back to Standard stage if weapon is nolonger triggered
            bool triggered = (weapon_mode == Foil && average > 50) || 
                             (weapon_mode == Saber && average > triggered_limit) || 
                             (weapon_mode == Epee && average < 50);
            if (!triggered) current_stage = Standard;
            //timer.stop();
            //printf("Time taken for loop: %llu us\n", timer.elapsed_time().count());
            //printf("Time taken for loop: %llu ms\n", timer.elapsed_time().count() / 1000);

            // Check if hit is long enough
            if (triggered_count < contact_times[weapon_mode]) {
                triggered_count++;
            } else {
                // If weapon was triggered, perform lockout and send results
                bool valid_hit = (weapon_mode == Foil && average > triggered_limit) || 
                                 (weapon_mode == Saber && average > triggered_limit) || 
                                 (weapon_mode == Epee && average < 50);
                char message[3] = {};
                if (valid_hit == 1) {
                    if (current_player == Green) {
                        strcpy(message, "gvh");  
                    } else {
                        strcpy(message, "rvh");
                    }
                } else {
                    if (current_player == Green) {
                        strcpy(message, "gih");    
                    } else {
                        strcpy(message, "rih");
                    }
                }

                radio.stopListening();
                bool sent = 0;
                while (sent == 0) {
                    sent = radio.write(&message, sizeof(message));
                }
                radio.startListening();
                
                triggered_count = 0;
                current_stage = Waiting;
            }
        } // Waiting stage after hit detection
        else if (current_stage == Waiting) {

            // assuming max size of the payload is 5
            char receivedPayload[5] = {0}; 
            // Check radio for further instructions
            if(radio.available()) {
                radio.read(receivedPayload, sizeof(receivedPayload));
                // Check the first character of the payload
                switch(receivedPayload[0]) {
                    case 'r':
                        current_stage = Standard;
                        radio.flush_tx();// Clear any remaining data in the radio's buffer
                        break;
                    default:
                        break;
                }   
            }
            
            char buffer[50];
            sprintf(buffer, "%s\nFinal average: %d %d\r", weaponNames[weapon_mode], average, triggered_count);
            displayMessage(buffer);
        }
    }
}