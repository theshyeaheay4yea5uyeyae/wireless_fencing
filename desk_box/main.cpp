#include "mbed.h"
#include "RF24.h"
#undef sprintf
#include "Adafruit_SSD1306.h"
#include <cstring>
using namespace std::chrono;

// Timing tool for debugging
Timer timer;

// LED and buzzer configurations
DigitalOut led_Green(PA_5);
DigitalOut led_Green_w(PB_4);
DigitalOut led_Red(PA_3);
DigitalOut led_Red_w(PA_2);
DigitalOut buzzer(PB_3);


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

// Lock times for different weapons in ms
const int lock_times[] = { 300, 170, 45 };  // Foil, Saber, Epee

// Enumerations for better code readability
enum Mode { Foil, Saber, Epee };
Mode weapon_mode = Foil;
enum Stage { Select_Weapon, Standard, Waiting  };
Stage current_stage = Select_Weapon;

// Lookup tables for display
const char* weaponNames[] = { "Foil", "Saber", "Epee" };


// Initialize the RF24 module with the right configuration
void start_rf() {
    radio.begin();
    radio.setPayloadSize(3);
    radio.setChannel(100);
    radio.openWritingPipe(desk_address);
    radio.openReadingPipe(1, red_pocket_address);
    radio.openReadingPipe(2, green_pocket_address);
    radio.startListening();
}

// Sends an RF message corresponding to the currently selected weapon.
void weapon_rf() {

    radio.stopListening();
    char message[3] = {};
	if (weapon_mode == Foil) {
			strcpy(message, "fff");  
		} else if(weapon_mode == Saber) {
			strcpy(message, "sss");
		} else {
			strcpy(message, "eee");
		}

	bool sent = 0;
	while (sent == 0) {
		sent = radio.write(&message, sizeof(message));
	}
	radio.startListening();
}

// Display messages on OLED
void displayMessage(const char* msg = nullptr) {
    gOled2.clearDisplay();
    gOled2.setTextCursor(0, 0);
    if (msg) gOled2.printf("%s", msg);
    gOled2.display();
}

// Sends an RF message to pocket boxes to restart the game.
void restart_rf() {

    radio.stopListening();
    char message[3] = {};
	strcpy(message, "rrr");

	bool sent = 0;
	while (sent == 0) {
		sent = radio.write(&message, sizeof(message));
	}
	radio.startListening();
}

// Handle confirmation button press
void confirm() {
    wait_us(100000);  // debounce
    if (!btn_confirm) {
        if (current_stage == Select_Weapon) {
            weapon_rf();
            displayMessage("Game is running\r");
            current_stage = Standard;
        }
        else if (current_stage == Waiting) {
            restart_rf();
            displayMessage("Game is running\r");
            current_stage = Standard;
        }
    }
}

// Handle selection button press
void select() {
    wait_us(100000);  // debounce
    if (!btn_select) {
        if (current_stage == Select_Weapon) {
            // Cycle through the weapon modes (Foil -> Saber -> Epee -> Foil ...)
            switch (weapon_mode) {
                case Foil:
                    weapon_mode = Saber;
                    break;
                case Saber:
                    weapon_mode = Epee;
                    break;
                case Epee:
                    weapon_mode = Foil;
                    break;
            }
        }
    }
}

int main() {

    btn_confirm.fall(&confirm);  // Attach interrupt handlers
    btn_select.fall(&select);
    printf("Hello\r");

    led_Green = 0;
    led_Green_w = 0;
    led_Red = 0;
    led_Red_w = 0;
    buzzer = 0;

    start_rf();
    
    // Main loop, driven by current stage
    while (1) {

        // Weapon selection logic
        if (current_stage == Select_Weapon) {
            char buffer[50];
            sprintf(buffer, "Weapon selecting: \r\n %s\r", weaponNames[weapon_mode]);
            displayMessage(buffer);

        // Main logic
        } else if (current_stage == Standard) {
            ThisThread::sleep_for(1);  // delay in ms

            // If radio data is available
            if (radio.available()) {
                char receivedPayloads[5] = {0};

                // Introduce a lock time delay based on weapon mode
                ThisThread::sleep_for(lock_times[weapon_mode]);

                // Keep reading data as long as it's available
                while (radio.available()) {
                    radio.read(receivedPayloads, sizeof(receivedPayloads));
                    
                    if (strcmp(receivedPayloads, "gvh") == 0) {
                        led_Green = 1;
                    } else if (strcmp(receivedPayloads, "gih") == 0) {
                        led_Green_w = 1;
                    } else if (strcmp(receivedPayloads, "rvh") == 0) {
                        led_Red = 1;
                    } else if (strcmp(receivedPayloads, "rih") == 0) {
                        led_Red_w = 1;
                    }                   
                }
                // turn on the respective LED for 5s and buzzer for 2.5s 
                buzzer = 1;
                ThisThread::sleep_for(2500);
                buzzer = 0;
                ThisThread::sleep_for(2500);
                led_Green = 0;
                led_Green_w = 0;
                led_Red = 0;
                led_Red_w = 0;

                current_stage = Waiting;
            }
        } // Waiting stage after hit detection
        else if (current_stage == Waiting) {
            
            displayMessage("\nPress confirmation button to continue \r");
        }
    }
}