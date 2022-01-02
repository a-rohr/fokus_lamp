/**************************************************************************
 * RFID Reader for the lamp - reading up on the different lamp module values
 * Our lamp module has two RFID tag sides
 * The important area is sector #2, block 8: This contains the code
 * that informs which state the RFID is in.
 * 
 * Study Mode DataBlock: {
        0x01, 0x02, 0x03, 0x04, //  1,  2,   3,  4,
        0x05, 0x06, 0x07, 0x08, //  5,  6,   7,  8,
        0x09, 0x0a, 0xff, 0x0b, //  9, 10, 255, 11,
        0x0c, 0x0d, 0x0e, 0x0f  // 12, 13, 14, 15
    };
 * 
 * Free Time Mode DataBlock: {
       0x01, 0x02, 0x03, 0x04, //  1,  2,   3,  4,
       0x05, 0x06, 0x07, 0x08, //  5,  6,   7,  8,
       0x09, 0x0a, 0xfe, 0x0b, //  9, 10, 254, 11,
       0x0c, 0x0d, 0x0e, 0x0f  // 12, 13, 14, 15
   };
 * 
 */

#include "Arduino.h"
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

/************** RFID AND LED DEFINITION *****************/
#define LED_COUNT 12
#define LED_PIN D1

#define BUTTON_PIN D0

#define RST_PIN         D3           // Configurable, see typical pin layout above
#define SS_PIN          D8          // Configurable, see typical pin layout above

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

// Main key needed to read RFID tags
MFRC522::MIFARE_Key key;

/********************** MQTT VARIABLES **************************/
// WiFi Details
#define ssid "BLABLA"
#define password "alex1996"

// Connection Details for MQTT Broker
#define mqtt_server IPAddress(64, 227, 112, 163)
#define USER "crazy_lamp"
#define PASSWORD "Lamp1234"

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE  (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;
String clientID = "";

// MQTT COMMUNICATION VARIABLES
// Tracks the state of the other lamp for interactivity
// '0': offline 
// '1': neutral mode online
// '2': study mode
// '3': free mode
// '4': game mode invite
char prev_partner_state = '0';
char partner_state = '0';

String publishing_topic = "";

unsigned long time_to_last_msg = 0;
unsigned long time_to_last_msg_threshold = 20000;
unsigned long time_to_last_output = 0;

unsigned long last_mqtt_reconnect = 0;
unsigned long last_wifi_reconnect = 0;

/*********************** BUTTON HANDLING VARIABLES ***********************/
volatile byte button_state = LOW;

// All variables for handling button input events
unsigned long button_press_time = 0;
bool short_press = false;
bool low_press = false;
unsigned long low_press_time = 1000;
bool med_press = false;
unsigned long med_press_time = 3000;
bool long_press = false;
unsigned long long_press_time = 5000;
bool multi_press = false;

int multi_press_count = 0;
unsigned long multi_press_time_threshold = 500;
unsigned long multi_button_press_time = 0;
int multi_press_threshold = 3;

/******************** STATE MACHINE VARIABLES ***************************/
// All variables for state machines
bool neutral_mode = true;
bool enter_neutral = true;
bool off_mode = false;
bool enter_off = false;
bool setting_mode = false;
bool enter_setting = false;
bool scan_mode = false;
bool enter_scan = false;
bool interactive_mode = false;
bool enter_interactive = false;

// Personal modes
bool study_mode = false;
bool enter_study = false;
bool study_timer_on = false;
unsigned long study_mode_start_time;
const unsigned long study_time = 1800000;

bool free_mode = false;
bool enter_free = false;
bool free_timer_on = false;
unsigned long free_mode_start_time;
const unsigned long free_time = 600000;

// Value: 0 [no mode], 1 [study mode], 2 [free mode]
int previous_per_mode = 0; 

// State just switched (to avoid falling button trigger after long press)
bool just_switched = false;

/************* RFID TAG DATA *******************/
// Important reader sections
const byte sector           = 2;
const byte blockRdr         = 8;
const byte trailerBlock     = 11;

// Counter values for the incoming tags
byte count_study = 0;
byte count_free = 0;

// Important data blocks
const byte dataBlock_Study[] = {
        0x01, 0x02, 0x03, 0x04, //  1,  2,   3,  4,
        0x05, 0x06, 0x07, 0x08, //  5,  6,   7,  8,
        0x09, 0x0a, 0xff, 0x0b, //  9, 10, 255, 11,
        0x0c, 0x0d, 0x0e, 0x0f  // 12, 13, 14, 15
    };

const byte dataBlock_Free[] = {
        0x01, 0x02, 0x03, 0x04, //  1,  2,   3,  4,
        0x05, 0x06, 0x07, 0x08, //  5,  6,   7,  8,
        0x09, 0x0a, 0xfe, 0x0b, //  9, 10, 254, 11,
        0x0c, 0x0d, 0x0e, 0x0f  // 12, 13, 14, 15
    };

/************* LAMP BRIGHTNESS TRIGGERS *************/
int lamp_brightness[] = {50,100,150,200,255};
int lamp_brightness_state = 3;
const int brightness_levels = 5;

/*************** STANDARD COLORS ********************/
const uint32_t red = strip.Color(255, 0, 0);
const uint32_t green = strip.Color(0, 255, 0);
const uint32_t blue = strip.Color(0, 0, 255);
const uint32_t purple = strip.Color(150, 10, 150);
const uint32_t yellow = strip.Color(255, 200, 0);
const uint32_t white = strip.Color(255,255,255);

int hsv_map[4] = {47, 240, 120, 0};

/*************** GAME VARIABLES *********************/
unsigned long time_delta = 2000;
unsigned long state_change_time = 0;
int light_state = 6;
int past_state = 0;
int next_state = 0;
int zero_light = 5;
int normal_time = 100;
int short_time = 20;


void setup() {
    // RFID SETUP
    Serial.begin(9600); // Initialize serial communications with the PC
    while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
    SPI.begin();        // Init SPI bus
    mfrc522.PCD_Init(); // Init MFRC522 card
    mfrc522.PCD_SetAntennaGain(MFRC522::PCD_RxGain::RxGain_max);

    // Prepare the key (used both as key A and as key B)
    // using FFFFFFFFFFFFh which is the default at chip delivery from the factory
    for (byte i = 0; i < 6; i++) {
        key.keyByte[i] = 0xFF;
    }

    // LED AND PIN SETUP
    pinMode(BUTTON_PIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
    strip.begin();
    strip.setBrightness(lamp_brightness[lamp_brightness_state]);
    strip.show(); // Initialize all pixels to 'off'
    light_neutral();
    Serial.println(F("Starting Lamp Reader"));

    clientID = "ESP8266Client";
    clientID += String(random(0xffff), HEX);
    // WIFI & MQTT SETUP
    setup_wifi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void loop() {
    // put your main code here, to run repeatedly:
    check_button_inputs();
    run_state_machine(); 
    run_mqtt_state_machine();
}

void check_button_inputs() {
    // Serial.println(digitalRead(BUTTON_PIN));
    if (digitalRead(BUTTON_PIN) != button_state) {
        button_state = digitalRead(BUTTON_PIN);
        if (!just_switched) {
            if (digitalRead(BUTTON_PIN) == HIGH) {
                // Serial.println("Button rising");
                button_hit();
            }
            else {
                // Serial.println("Button falling");
                button_lift();
            }
        }
        else {
            just_switched = false;
        }
    }
  
    if (digitalRead(BUTTON_PIN) == HIGH) {
        if ((millis() - button_press_time) >= long_press_time) {
            Serial.println("Long Button Press");
            long_press = true;
            just_switched = true;
            button_press_time = millis();
            delay(500);
        }   
    }
}

void run_state_machine() {
    if (neutral_mode) {
        // Neutral mode methods
        neutral_modes();
    }
    if (scan_mode) {
        // Scan mode methods
        scan_modes();
    }
    if (setting_mode) {
        // Setting mode methods
        setting_modes();
    }
    if (off_mode) {
        // Off mode methods here
        off_modes();
    }
    if (interactive_mode) {
        // Interactive mode methods here
        interactive_modes();
    }
    set_all_buttons_false();
}

void run_mqtt_state_machine() {
    
    if (WiFi.status() != WL_CONNECTED && ((millis() - last_wifi_reconnect) > 20000)) {
        setup_wifi();
    }
    else if (!client.connected() && ((millis() - last_mqtt_reconnect) > 20000)) {
        reconnect();
    }
    else if((millis() - time_to_last_msg) > time_to_last_msg_threshold) {
        partner_state = 0;
    }
    client.loop();

    output_mqtt_machine();
    input_mqtt_machine();
    // Add publisher every 5 second of current state to mqtt
}

void output_mqtt_machine() {

    if ((millis() - time_to_last_output) > 5000) {
        if (neutral_mode || (scan_mode && !study_mode && !free_mode)  || setting_mode) {
            client.publish(publishing_topic.c_str(), "1");
        }
        else if (study_mode) {
            client.publish(publishing_topic.c_str(), "2");
        }
        else if (free_mode) {
            client.publish(publishing_topic.c_str(), "3");
        }
        else if (interactive_mode) {
            client.publish(publishing_topic.c_str(), "4");
        }
        else {
            client.publish(publishing_topic.c_str(), "0");
        }
        time_to_last_output = millis();
    }
}

void input_mqtt_machine() {
    // Handles the changed states of the partner machines
    if (partner_state != prev_partner_state) {
        if (neutral_mode) {
            if (partner_state == '1' && prev_partner_state == '0') {
                transition_between_light(0,3);
                delay(500);
                light_neutral();
                transition_between_light(0,3);
                delay(500);
                light_neutral();
            }
            else if (partner_state == '2') {
                transition_between_light(0,1);
                delay(500);
                light_neutral();
                transition_between_light(0,1);
                delay(500);
                light_neutral();
            }
            else if (partner_state == '0') {
                transition_light();
                light_neutral();
                transition_light();
                light_neutral();
            }
            else if (partner_state == '3') {
                transition_between_light(0,2);
                delay(500);
                light_neutral();
                transition_between_light(0,2);
                delay(500);
                light_neutral();
            }
            prev_partner_state = partner_state;
        }
        
    }
    
}

void neutral_modes() {
    if (enter_neutral) {
        Serial.println("I'm in neutral mode");
        enter_neutral = false;
        free_mode = false;
        study_mode = false;
        scan_mode = false;
        if (previous_per_mode != 0) {
            transition_between_light(previous_per_mode,0);
        }
        else {
            light_neutral();     
        }

        previous_per_mode = 0;
    }
    if (long_press) {
        exiting_neutral_mode();
        just_switched = true;
        off_mode = true;
        enter_off = true;
        return;       
    }
    if (med_press) {
        exiting_neutral_mode();
        setting_mode = true;
        enter_setting = true;
        return;
    }
    if (low_press) {
        exiting_neutral_mode();
        interactive_mode = true;
        enter_interactive = true;
        return;
    }
    if (short_press) {
        // exiting_neutral_mode();
        scan_mode = true;
        enter_scan =true;
        neutral_mode = false;
        short_press = false;
        return;
    }
}

void exiting_neutral_mode() {
    Serial.println("Exiting neutral mode");
    neutral_mode = false;
    // set_all_buttons_false();
}

void scan_modes() {
    if (enter_scan) {
        Serial.println("I'm in scan mode");
        enter_scan = false;
    }
    else if (multi_press) {
        Serial.println("Exiting scan mode");
        scan_mode = false;
        neutral_mode = true;
        enter_neutral = true;
        if (study_mode) {
            previous_per_mode = 1;  
            study_mode = false;
            
        }
        else if (free_mode) {
            previous_per_mode = 2;
            free_mode = false;
        }
        else {
            // Do nothing here
        } 
        // set_all_buttons_false();
        return;
    }
    personal_modes();
    // Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
    RFID_handle_loop();
}

void RFID_handle_loop() {
    if ( ! mfrc522.PICC_IsNewCardPresent()) {
        // Serial.println("No new card present, therefore default");
        return;  
    }

    // Select one of the cards
    if ( ! mfrc522.PICC_ReadCardSerial()) {
        // Serial.println("Something about selecting a single card and defaulting");
        /*** CURRENTLY NOT USED DUE TO UNDESIRED BEHAVIOUR
        if (previous_per_mode == 1) {
            study_mode = true;
            free_mode = false;
            
            enter_study = true;
            enter_free = false;
            previous_per_mode = 0;
        }
        else if (previous_per_mode == 2) {
            free_mode = true;
            study_mode = false;
            
            enter_free = true;
            enter_study = false;
            previous_per_mode = 0;
        }
        return;  
        *************************************************************/
        return;
    }

    // Read the current RFID card
    read_changed_RFID();

    // Cross check the RFID tag values from sector 2, block 8
    if (count_study == 16) {
        // In here we begin the startup for the study mode
        if (!study_mode) {
            study_mode = true;
            free_mode = false;
       
            enter_study = true;
            enter_free = false;
        }
    } else if (count_free == 16){
        // In here we begin the startup for the free time mode
        if (!free_mode) {
            free_mode = true;
            study_mode = false;
            
            enter_free = true;
            enter_study = false;
        }
    }
    Serial.println();
    
    // Halt PICC
    mfrc522.PICC_HaltA();
    // Stop encryption on PCD
    mfrc522.PCD_StopCrypto1();  
}

void personal_modes() {
    // ROuting for the personal modes
    if (study_mode) {
        study_modes();
    }
    if (free_mode) {
        free_modes();
    }
}

void study_modes() {
    if (enter_study) {
        // exiting_neutral_mode();
        enter_study = false;
        transition_between_light(previous_per_mode,1);
        //light_study();
        study_mode_timer();

        previous_per_mode = 1;
    }
    if ((millis() - study_mode_start_time) > study_time) {
        Serial.println("Study mode done");
        scan_mode = false;
        neutral_mode = true;
        enter_neutral = true;
        previous_per_mode = 1;
        study_mode = false;
    }
}

void study_mode_timer() {
    study_mode_start_time = millis();  
}

void free_modes() {
    if (enter_free) {
        // exiting_neutral_mode();
        enter_free = false;
        transition_between_light(previous_per_mode,2);
        free_mode_timer();

        previous_per_mode = 2;
    }
    if ((millis() - free_mode_start_time) > free_time) {
        Serial.println("Free mode done");
        scan_mode = false;
        neutral_mode = true;
        enter_neutral = true;
        previous_per_mode = 2;
        free_mode = false;
    }
}

void free_mode_timer() {
    free_mode_start_time = millis();   
}

void setting_modes() {
    if (enter_setting) {
        Serial.println("I'm in setting mode");
        enter_setting = false;
        transition_light();
        light_neutral();
    }
    else if (med_press) {
        Serial.println("Exiting setting mode");
        setting_mode = false;
        neutral_mode = true;
        enter_neutral = true;
        transition_light();
        // set_all_buttons_false();
        return;
    }
    else if (short_press) {
        // Increment cycle through different brightness levels
        lamp_brightness_state = (lamp_brightness_state + 1) % brightness_levels;
        strip.setBrightness(lamp_brightness[lamp_brightness_state]);
        strip.show();
        delay(100);
        // set_all_buttons_false();   
    }
    else {
      
    }
}

void off_modes() {
    if (enter_off) {
        Serial.println("I'm in off mode");
        enter_off = false;
        long_press = false;
        light_off();
    }
    else if (long_press) {
        Serial.println("Exiting off mode");
        just_switched = true;
        off_mode = false;
        neutral_mode = true;
        enter_neutral = true;
        // set_all_buttons_false();
        return;
    }
}

void interactive_modes() {
    if (enter_interactive) {
        Serial.println("I'm interactive");
        enter_interactive = false;
        transition_light();
        light_neutral();
        delay(400);
        transition_light();
        light_neutral();
        delay(500);
        load_up_lights(zero_light,normal_time);
        delay(100);
    
        // Set the red pixel for the game to start
        strip.setPixelColor(light_state, blue);
        strip.show();
    }
    else if (low_press) {
        Serial.println("Exiting interactive mode");
        interactive_mode = false;
        neutral_mode = true;
        enter_neutral = true;
        reset_game_values();
        // set_all_buttons_false();
        return;
    }
    else if (short_press) {
        if (light_state == zero_light) {
            // If succesful button match, show green light
            succesful_hit();
            // delay(100);
            light_state = zero_light;
            time_delta = time_delta / 3 * 2;
            Serial.println(time_delta);
            if (time_delta < 100) {
                // Winning condition for the game
                strip.clear();
                strip.show();
                delay(50);
                succesful_hit();
                Serial.println("Game won");
                interactive_mode = false;
                neutral_mode = true;
                enter_neutral = true;
                reset_game_values();
                return;
            }
            load_up_lights(zero_light, short_time);
        }
         else{
            // If bad hit, reset (not the timer) and show red
            bad_hit();
            load_up_lights(zero_light, short_time);
            light_state = zero_light;
        }
    }
    else {
        // Check to see if time delta has expired 
        // Then move the LED one spot
        if (state_change_time == 0) {
            state_change_time = millis();
        }
        if (time_delta < (millis() - state_change_time)) {
            past_state = light_state;
            next_state = (light_state + 1)%12;
            set_new_lights(past_state,next_state, zero_light);
            light_state = next_state;
            state_change_time = millis();
         }
    }
}

void set_all_buttons_false() {
    long_press = false;
    med_press = false;
    low_press = false;
    short_press = false;
    multi_press = false;
}

/************* GAME SPECIFIC METHODS **************/
void set_new_lights(int past_state, int next_state, int zero_light) {
    // Transition to the next lights
    // Important not to overwrite the green LED
    if (next_state != zero_light) {
        strip.setPixelColor(next_state, blue);
    }
    else {
        strip.setPixelColor(next_state, green);
    }
    if (past_state != zero_light) {
        strip.setPixelColor(past_state, purple);
    }
    else {
        strip.setPixelColor(past_state, green);
    }
    strip.show();
}

void load_up_lights(int zero_light, int delay_time) {
    // Load up the lights in a circular pattern
    strip.clear();
    strip.show();
    delay(100);
    for(int i=0; i < 12; i++) {
        if (i == zero_light) {
            strip.setPixelColor(i, green);
        }
        else {
            strip.setPixelColor(i, purple);
        }
        strip.show();
        delay(delay_time);
    }
}

void succesful_hit() {
    // Circular light pattern after succesful hit
    int pos = 6;
    for(int i=0; i < 12; i++) {
        pos = (6 + i)%12;
        strip.setPixelColor(pos, green);
        strip.show();
        delay(100);
    }
}

void bad_hit() {
    // Red light pattern after unsuccesful hit
    int pos = 6;
    for(int i=0; i < 12; i++) {
        pos = (6 + i)%12;
        strip.setPixelColor(pos, red);
        strip.show();
        delay(50);
    }
}

void reset_game_values() {
    time_delta = 2000;
    state_change_time = 0;
    light_state = 6;
    past_state = 0;
    next_state = 0;
    zero_light = 5;
}

/**************** LIGHT TRIGGER METHODS **************/
void transition_light() {
    // Little light sequence to show that the state hase changed
    strip.fill(white);
    strip.show();
    delay(500);
}

void light_off() {
    strip.clear();
    strip.show();  
    delay(200);
}

void light_red() {
    // Red light in general mode
    strip.fill(red);
    strip.show();
    delay(200);
}

void light_neutral() {
    // Neutral light in general mode
    strip.fill(yellow, 0, 12);
    strip.show();
    Serial.println(F("Neutral light on"));
    delay(100);
}

void light_study() {
    // Light during the study mode
    strip.fill(blue, 0, 12);
    strip.show();
    Serial.println(F("Study mode light on"));
    delay(100);    
}

void transition_between_light(int start_light, int end_light) {
    // HSV map maps state numbers to hsv_values of the states
    // 0: neutral   = yellow  Hue 47
    // 1: study     = blue    Hue 240
    // 2: free      = green   Hue 120
    // 3: red       = red     Hue 
    
    int start_hsv = hsv_map[start_light];
    int end_hsv = hsv_map[end_light];
    int range = end_hsv - start_hsv;
    int check1 = 0;
    int check2 = 0;
    int sign_bit = 0;

    if (range < 0) {
        check1 = -1;
    }
    else {
        check1 = 1;
    }

    if (abs(range) > 180) {
        range = 360 - abs(range);
        check2 = -1;
    }
    else {
        range = abs(range);
        check2 = 1;
    }

    sign_bit = check1*check2;
    for(int i = 0; i < range; i++) {
        strip.fill(strip.ColorHSV((int)((start_hsv+sign_bit*(i+1))/360.0*65536)));
        strip.show();
        delay(5);
    }
}

void light_free() {
    // Light during the free time mode
    strip.fill(green, 0, 12);
    strip.show();
    Serial.println(F("Free mode light on"));
    delay(100);  
}

/*************** RFID METHODS *******************/
void read_changed_RFID() {
    // Show some details of the PICC (that is: the tag/card) 
    // Tag values important here
    Serial.print(F("Card UID:"));
    dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.println();
    Serial.print(F("PICC type: "));
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    Serial.println(mfrc522.PICC_GetTypeName(piccType));

    // Initializing the status and buffer area
    MFRC522::StatusCode status;
    byte buffer[18];
    byte size = sizeof(buffer);

    // Authenticate using key A
    Serial.println(F("Authenticating using key A..."));
    status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK) {
        Serial.print(F("PCD_Authenticate() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
        return;
    }

    // Read data from sector 2 block 8
    Serial.print(F("Reading data from block ")); Serial.print(blockRdr);
    Serial.println(F(" ..."));
    status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(blockRdr, buffer, &size);
    if (status != MFRC522::STATUS_OK) {
        Serial.print(F("MIFARE_Read() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
    }
    Serial.print(F("Data in block ")); Serial.print(blockRdr); Serial.println(F(":"));
    dump_byte_array(buffer, 16); Serial.println();

    // Check the sector 2 block 8 data and cross compare with our two RFID tag modes
    // Check the block tags in the above area (dataBlock_Study and dataBlock_Free)
    Serial.println(F("Checking result..."));
    count_study = 0;
    count_free = 0;
    for (byte i = 0; i < 16; i++) {
        // Compare buffer (= what we've read) with dataBlock (= what we've written)
        if (buffer[i] == dataBlock_Study[i])
            count_study++;
        if (buffer[i] == dataBlock_Free[i])
            count_free++;
    }
}

// Helper method for reading RFID tags
void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}

/*************** BUTTON TRIGGERS *****************/
// Method during rising button signal
void button_hit() {
  button_press_time = millis();
}

// Method during falling button signal
void button_lift() {
  if ((millis() - button_press_time) >= med_press_time) {
      Serial.println("Medium Button Press");
      med_press = true;
      // delay(50);
  }
  else if ((millis() - button_press_time) >= low_press_time) {
      Serial.println("Low Button Press");
      low_press = true;
      // delay(50);
  }
  else {
      Serial.println("Short Button Press");
      if ((millis() - multi_button_press_time) > multi_press_time_threshold) {
          multi_press_count = 0;
      }
      short_press = true;
      if (multi_press_count == 0) {
          multi_button_press_time = millis();
      }
      multi_press_count += 1;
      Serial.println(multi_press_count);
      if (multi_press_count == multi_press_threshold) {
          if ((millis() - multi_button_press_time) <= multi_press_time_threshold){
              Serial.println("Multi button press");
              multi_press = true;
              multi_press_count = 0;
          }
          else {
              multi_press_count = 0;
          }
      }
  }
  button_press_time = millis();
}

/************************ NETWORKING CODE ************************/
// Main callback method
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    if (strcmp(publishing_topic.c_str(), topic) != 0) {
        partner_state = (char)payload[0];
        Serial.print(" : ");
        Serial.print(partner_state);
        if (partner_state == '4') {
            // Apply some logic here for the game incoming stream
        }
        time_to_last_msg = millis();
    }
    Serial.println("");
}

// Wifi setup
void setup_wifi() {
    unsigned long time_elapsed = 0;
    delay(10);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
  
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  
    while (WiFi.status() != WL_CONNECTED && (time_elapsed < 1000)) {
        delay(500);
        time_elapsed += 500;
        Serial.print(".");
    }
  
    randomSeed(micros());
  
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    last_wifi_reconnect = millis();
}

// Set up connection to MQTT server
void reconnect() {
  // Loop until we're reconnected
  unsigned long time_elapsed = 0;
  while (!client.connected() && (time_elapsed < 1000)) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    // Attempt to connect
    if (client.connect(clientID.c_str(),USER,PASSWORD)) {
      Serial.println("connected");
      publishing_topic = "lamp_status/" + clientID;
      // Once connected, publish an announcement...
      client.publish(publishing_topic.c_str(), "hello world");
      // ... and resubscribe
      client.subscribe("#");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      last_mqtt_reconnect = millis();
      delay(500);
      time_elapsed += 500;
    }
  }
}