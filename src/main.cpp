#define DEBUG

#define E22_30    //EBYTE E22_30 series
#define FREQUENCY_850			//E22 base frequency
#define MY_CRC8_POLY 0xAB     //used for 8bit crc
#define MY_CRC16_POLY 0xABAB    //used for 16 bit crc

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "LoRa_E22.h"
#include "CRC.h"
#include "CRC8.h"
#include "CRC16.h" 

#include "MyLora_E22.h"

struct mg_info {
  uint8_t e22_addr_h;
  uint8_t e22_addr_l;
  uint8_t e22_channel = 70;     //920.125 MHz
  uint8_t e22_crypt_h = 0xAB;   //used for encryption and decryption
  uint8_t e22_crypt_l = 0xAB;


  uint16_t current_ig_count = 0;
  uint16_t current_user_count = 0;
}mg;

enum QUARANTINE_BAND_STATUS {
  NOT_INIT  = 0,
  PRESENT   = 1,
  OPENED    = 2,
  MISSING   = 3,
  EMERGENCY = 4,
  MOVED     = 5
};

enum E22_MESSAGE_TYPE {
  REQUEST_E22_ADDR        = 0,
  REQUEST_GOOGLESHEET_ROW = 1,
  FULL_UPDATE_INFO        = 2,
  UPDATE_INFO             = 3,

  RETURN_E22_ADDR         = 4,
  RETURN_GOOGLESHEET_ROW  = 5,

  ACK                     = 6
};

enum E22_ACK_PAYLOAD {
  ACK_ONLY    = 0,
  ACK_PAYLOAD = 1
};

enum CRC_LENGTH {
  CRC_8   = 0,
  CRC_16  = 1,
};

struct e22_message_header{  //12bytes
  uint8_t message_type;
  uint8_t sender_e22_addr_h;
  uint8_t sender_e22_addr_l;
  uint8_t ack_payload;
  uint8_t sender_sequence;
  uint8_t receiver_sequence;
  uint16_t crc_poly;
  uint8_t crc_length;
  uint8_t padding;    //padding for similar alignment between arduino and esp8266
  uint8_t padding1;    //padding for similar alignment between arduino and esp8266
  uint8_t padding2;    //padding for similar alignment between arduino and esp8266
};

struct ig_sync_message{   //16bytes
  e22_message_header ig_header;

  uint8_t padding;    //padding for similar alignment between arduino and esp8266
  uint8_t padding1;    //padding for similar alignment between arduino and esp8266
  uint8_t padding2;    //padding for similar alignment between arduino and esp8266

  uint8_t crc;
};

struct ig_ack_message{    //16bytes
  e22_message_header ig_ack_header;

  uint8_t padding;    //padding for similar alignment between arduino and esp8266
  uint8_t padding1;    //padding for similar alignment between arduino and esp8266
  uint8_t padding2;    //padding for similar alignment between arduino and esp8266

  uint8_t crc;
};

struct ig_full_message{     //40bytes
  e22_message_header ig_header;
  uint32_t qb_addr;
  uint16_t mg_mem_addr;
  uint8_t padding;    //padding for similar alignment between arduino and esp8266
  uint8_t padding1;    //padding for similar alignment between arduino and esp8266
  float initial_latitude;
  float initial_longitude;
  char ic[16];      //included padding
  char hp_num[12];    //included padding
  uint8_t status;
  uint8_t padding2;    //padding for similar alignment between arduino and esp8266

  uint16_t crc;
};

struct ig_update_message{    //28bytes
  e22_message_header ig_header;
  uint16_t mg_mem_addr;
  uint8_t padding;    //padding for similar alignment between arduino and esp8266
  uint8_t padding1;    //padding for similar alignment between arduino and esp8266
  float current_latitude;
  float current_longitude;
  uint8_t status;
  uint8_t padding2;    //padding for similar alignment between arduino and esp8266

  uint16_t crc;
};

struct mg_sync_message{
  e22_message_header mg_header;

  uint8_t ig_e22_addr_h;
  uint8_t ig_e22_addr_l;

  uint8_t padding;     //padding for similar alignment between arduino and esp8266

  uint8_t crc;
};

struct mg_sync_message_qb_mem{
  e22_message_header mg_header;

  uint16_t qb_mem_addr;

  uint8_t padding;    //padding for similar alignment between arduino and esp8266

  uint8_t crc;
};

union incoming_message {
  ig_sync_message* ig_sync_msg;
  ig_ack_message* ig_ack_msg;
  ig_full_message* ig_full_msg;
  ig_update_message* ig_update_msg;
}incoming_msg_ptr;

void update_to_Google_sheet(uint16_t mem_addr, char ic[], char hp[], String initial_location, String current_location, \
uint16_t ig_addr, uint32_t qb_addr, uint8_t status);

MyLoRa_E22 e22(D4, D5, D3, D7, D6);    // Arduino RX <-- e22 TX, Arduino TX --> e22 RX AUX M0 M1

//----------------------------------------SSID and Password of your WiFi router.
const char* ssid = "Wan-TIME2.4Ghz"; //--> Your wifi name or SSID.
const char* password = "zickwan-4896"; //--> Your wifi password.
//----------------------------------------

//----------------------------------------Host & httpsPort
const char* host = "script.google.com";
const int httpsPort = 443;
//----------------------------------------

WiFiClientSecure client; //--> Create a WiFiClientSecure object.

String GAS_ID = "AKfycbzAmVwQeaWYjW43XO8NY4WNtMEyMAm0WLyc4OQq9AKWqkp07a_PJhGo5b-j0Wn-FGBZow"; //--> spreadsheet script ID

String Modifying_link = "https://script.google.com/macros/s/AKfycbzAmVwQeaWYjW43XO8NY4WNtMEyMAm0WLyc4OQq9AKWqkp07a_PJhGo5b-j0Wn-FGBZow/exec";  //spreadsheet modifying link

//============================================================================== void setup
void setup() {
  pinMode(A0, INPUT);     //used for randomSeed 
  // put your setup code here, to run once:
  Serial.begin(9600);
  while(!Serial);
  
  WiFi.begin(ssid, password); //--> Connect to your WiFi router
  Serial.println("");

  //----------------------------------------Wait for connection
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  //----------------------------------------If successfully connected to the wifi router, the IP Address that will be visited is displayed in the serial monitor
  Serial.println("");
  Serial.print("Successfully connected to : ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  //----------------------------------------

  client.setInsecure();

  e22.begin();

  mg.e22_addr_h = random(0, 0xFE);
  mg.e22_addr_l = random(1, 0xFF);

  	#ifdef DEBUG
		Serial.print(F("MG E22 Addr = "));
		Serial.print(mg.e22_addr_h, 16);
		Serial.println(mg.e22_addr_l, 16);
	#endif

  if(!e22.set_e22_configuration(mg.e22_addr_h, mg.e22_addr_l, mg.e22_channel, true, POWER_21, 
	AIR_DATA_RATE_000_03, mg.e22_crypt_h, mg.e22_crypt_l, false)) {
		#ifdef DEBUG
			Serial.println(F("Setting E22 failed"));
			Serial.println(F("check connection"));
		#endif
		while(1){
			delay(0);		//use delay(0) to stay in loop without crash
		}
	}
}
//==============================================================================
//============================================================================== void loop
void loop() {
//   #ifdef DEBUG
// 	Serial.println(F("Main loop"));
// 	delay(100);
//   #endif
  delay(0);		//use delay(50) to stay in loop without crash
  if(e22.available()>1) {
    ResponseContainer rc = e22.receiveMessage();
    incoming_msg_ptr.ig_full_msg = reinterpret_cast<ig_full_message*>(rc.data.begin());
    switch (incoming_msg_ptr.ig_full_msg->ig_header.message_type)
    {
      case REQUEST_E22_ADDR:
      {
        uint8_t incoming_crc = incoming_msg_ptr.ig_sync_msg->crc;
        uint8_t crc_check = crc8(reinterpret_cast<uint8_t*>(incoming_msg_ptr.ig_sync_msg), sizeof(ig_sync_message)-1, \
                              incoming_msg_ptr.ig_sync_msg->ig_header.crc_poly);
        if(incoming_crc == crc_check) {
          #ifdef DEBUG
            Serial.println(F("Received REQUEST_E22_ADDR message"));
          #endif
        }
        else {
          #ifdef DEBUG
            Serial.println(F("Wrong CRC"));
          #endif
          break;
        }

        #ifdef DEBUG
            Serial.println(F("Checking format..."));
		#endif

        if((incoming_msg_ptr.ig_sync_msg->ig_header.ack_payload != ACK_PAYLOAD)
          || (incoming_msg_ptr.ig_sync_msg->ig_header.sender_sequence != 0) 
          || (incoming_msg_ptr.ig_sync_msg->ig_header.receiver_sequence != 0)
          || (incoming_msg_ptr.ig_sync_msg->ig_header.crc_length != CRC_8)) {
		#ifdef DEBUG
			Serial.println(F("Wrong format"));
		#endif
		break;
		}

		#ifdef DEBUG
            Serial.println(F("Correct format"));
		#endif

        uint8_t ig_addr_h = incoming_msg_ptr.ig_sync_msg->ig_header.sender_e22_addr_h;
        uint8_t ig_addr_l = incoming_msg_ptr.ig_sync_msg->ig_header.sender_e22_addr_l;
		
        mg_sync_message mg_to_ig_sync;

        mg_to_ig_sync.ig_e22_addr_h = mg.current_ig_count & 0xFF00;
        mg_to_ig_sync.ig_e22_addr_l = mg.current_ig_count & 0x00FF;
        
        mg_to_ig_sync.mg_header.message_type = RETURN_E22_ADDR;
        mg_to_ig_sync.mg_header.ack_payload = ACK_PAYLOAD;
        mg_to_ig_sync.mg_header.sender_e22_addr_h = mg.e22_addr_h;
        mg_to_ig_sync.mg_header.sender_e22_addr_l = mg.e22_addr_l;
        mg_to_ig_sync.mg_header.sender_sequence = 0;
        mg_to_ig_sync.mg_header.receiver_sequence = 1;
        mg_to_ig_sync.mg_header.crc_length = CRC_8;
        mg_to_ig_sync.mg_header.crc_poly = MY_CRC8_POLY;

        mg_to_ig_sync.crc = crc8(reinterpret_cast<uint8_t*>(&mg_to_ig_sync), sizeof(mg_sync_message)-1, mg_to_ig_sync.mg_header.crc_poly);
        
        char* msg_str = reinterpret_cast<char*>(&mg_to_ig_sync);

        uint8_t retry = 0;
        unsigned long max_wait_millis = 10000;    //max wait for 10s
        unsigned long start_millis;
        ResponseStatus rs;
        unsigned long current_millis;
        uint8_t crc;
		
        Retry:
        if(retry>10) {
          #ifdef DEBUG
          Serial.println(F("Sending IG_ADDR failed"));
          #endif
          break;
        }
		randomSeed(analogRead(A0));
		delay(random(1000));		//add random delay to have lower collision rate
		#ifdef DEBUG
			Serial.print(F("Sending IG_ADDR to: "));
			Serial.print(ig_addr_h, 16);
			Serial.println(ig_addr_l, 16);
		#endif
		
        start_millis = millis();
        rs = e22.sendFixedMessage(ig_addr_h, ig_addr_l, mg.e22_channel, msg_str, sizeof(mg_sync_message));		///debugging here
        if(rs.code!=1) {
          retry++;
          goto Retry;
        }

		#ifdef DEBUG
			Serial.println(F("Waiting response"));
		#endif
        while(e22.available()<=0) {
          current_millis = millis();
          if(current_millis - start_millis >= max_wait_millis) {
            retry++;
            goto Retry;
          }
        }

		#ifdef DEBUG
          Serial.println(F("Received response"));
		#endif

        ResponseStructContainer rsc_ack = e22.receiveMessage(sizeof(ig_ack_message));
        if (rsc_ack.status.code!=1){
          retry++;
          goto Retry;
        }
        incoming_msg_ptr.ig_ack_msg = reinterpret_cast<ig_ack_message*>(rsc_ack.data);
        
        #ifdef DEBUG
          Serial.println(F("Checking Ack message"));
		    #endif

        crc = crc8(reinterpret_cast<uint8_t*>(incoming_msg_ptr.ig_ack_msg), sizeof(ig_ack_message)-1, \
            incoming_msg_ptr.ig_ack_msg->ig_ack_header.crc_poly);
        if(crc != incoming_msg_ptr.ig_ack_msg->crc){
          #ifdef DEBUG
            Serial.println(F("Wrong Ack message CRC"));
          #endif
          retry++;
          goto Retry;
        }
        if ((incoming_msg_ptr.ig_ack_msg->ig_ack_header.message_type != ACK) || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.ack_payload != ACK_ONLY)\
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.sender_e22_addr_h != uint8_t((mg.current_ig_count&0xFF00)>>8)) \
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.sender_e22_addr_l != uint8_t(mg.current_ig_count&0x00FF)) \
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.sender_sequence != mg_to_ig_sync.mg_header.receiver_sequence) \
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.receiver_sequence != (mg_to_ig_sync.mg_header.sender_sequence ^ 0x01)) \
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.crc_length != CRC_8) \
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.crc_poly != MY_CRC8_POLY)) {
          #ifdef DEBUG
            Serial.println(F("Wrong Ack message format"));
          #endif
          retry++;
          goto Retry;
        }

        //correct ack received
        mg.current_ig_count ++;
        #ifdef DEBUG
          Serial.println(F("Sending IG_ADDR completed"));
        #endif
        break;
      }

      case REQUEST_GOOGLESHEET_ROW:
      {
        uint8_t incoming_crc = incoming_msg_ptr.ig_sync_msg->crc;
        uint8_t crc_check = crc8(reinterpret_cast<uint8_t*>(incoming_msg_ptr.ig_sync_msg), sizeof(ig_sync_message)-1, \
                              incoming_msg_ptr.ig_sync_msg->ig_header.crc_poly);
        if(incoming_crc == crc_check) {
          #ifdef DEBUG
            Serial.println(F("Received REQUEST_GOOGLESHEET_ROW message"));
          #endif
        }
        else {
          #ifdef DEBUG
            Serial.println(F("Wrong CRC"));
          #endif
          break;
        }
        
        if((incoming_msg_ptr.ig_sync_msg->ig_header.ack_payload != ACK_PAYLOAD)
          || (incoming_msg_ptr.ig_sync_msg->ig_header.sender_sequence != 0) 
          || (incoming_msg_ptr.ig_sync_msg->ig_header.receiver_sequence != 0)
          || (incoming_msg_ptr.ig_sync_msg->ig_header.crc_length != CRC_8)) {
            #ifdef DEBUG
              Serial.println(F("Wrong format"));
            #endif
            break;
          }

        uint8_t ig_addr_h = incoming_msg_ptr.ig_sync_msg->ig_header.sender_e22_addr_h;
        uint8_t ig_addr_l = incoming_msg_ptr.ig_sync_msg->ig_header.sender_e22_addr_l;

        mg_sync_message_qb_mem mg_to_ig_qb_mem_addr;

        mg_to_ig_qb_mem_addr.qb_mem_addr = mg.current_user_count;
        
        mg_to_ig_qb_mem_addr.mg_header.message_type = RETURN_GOOGLESHEET_ROW;
        mg_to_ig_qb_mem_addr.mg_header.ack_payload = ACK_PAYLOAD;
        mg_to_ig_qb_mem_addr.mg_header.sender_e22_addr_h = mg.e22_addr_h;
        mg_to_ig_qb_mem_addr.mg_header.sender_e22_addr_l = mg.e22_addr_l;
        mg_to_ig_qb_mem_addr.mg_header.sender_sequence = 0;
        mg_to_ig_qb_mem_addr.mg_header.receiver_sequence = 1;
        mg_to_ig_qb_mem_addr.mg_header.crc_length = CRC_8;
        mg_to_ig_qb_mem_addr.mg_header.crc_poly = MY_CRC8_POLY;

        mg_to_ig_qb_mem_addr.crc = crc8(reinterpret_cast<uint8_t*>(&mg_to_ig_qb_mem_addr), sizeof(mg_sync_message_qb_mem)-1, mg_to_ig_qb_mem_addr.mg_header.crc_poly);
        
        char* msg_str = reinterpret_cast<char*>(&mg_to_ig_qb_mem_addr);

        uint8_t retry = 0;
        unsigned long max_wait_millis = 10000;    //max wait for 10s
        unsigned long start_millis;
        ResponseStatus rs;
        unsigned long current_millis;
        ResponseStructContainer rsc;
        uint8_t crc;

        Retry_send_mem_addr:
        if(retry>10) {
          #ifdef DEBUG
          Serial.println(F("Sending QB_MEM_ADDR failed"));
          #endif
          break;
        }
        start_millis = millis();
        rs = e22.sendFixedMessage(ig_addr_h, ig_addr_l, mg.e22_channel, msg_str, sizeof(mg_sync_message_qb_mem));
        if(rs.code!=1) {
          retry++;
          goto Retry_send_mem_addr;
        }
        while(!(e22.available()>1)) {
          current_millis = millis();
          if(current_millis - start_millis >= max_wait_millis) {
            retry++;
            goto Retry_send_mem_addr;
          }
        }

        rsc = e22.receiveMessage(sizeof(ig_ack_message));
        if (rsc.status.code!=1){
          retry++;
          goto Retry_send_mem_addr;
        }
        incoming_msg_ptr.ig_ack_msg = reinterpret_cast<ig_ack_message*>(rsc.data);
        
        crc = crc8(reinterpret_cast<uint8_t*>(incoming_msg_ptr.ig_ack_msg), sizeof(ig_ack_message)-1, \
            incoming_msg_ptr.ig_ack_msg->ig_ack_header.crc_poly);
        if(crc != incoming_msg_ptr.ig_ack_msg->crc){
          retry++;
          goto Retry_send_mem_addr;
        }
        if ((incoming_msg_ptr.ig_ack_msg->ig_ack_header.message_type != ACK) || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.ack_payload != ACK_ONLY)\
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.sender_e22_addr_h != (mg.current_ig_count&0xFF00)) \
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.sender_e22_addr_l != (mg.current_ig_count&0x00FF)) \
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.sender_sequence != mg_to_ig_qb_mem_addr.mg_header.receiver_sequence)\
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.receiver_sequence != (mg_to_ig_qb_mem_addr.mg_header.sender_sequence ^1))\
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.crc_length != CRC_8) \
          || (incoming_msg_ptr.ig_ack_msg->ig_ack_header.crc_poly != MY_CRC8_POLY)) {
          retry++;
          goto Retry_send_mem_addr;
        }

        //correct ack received
        mg.current_user_count ++;
        #ifdef DEBUG
          Serial.println(F("Sending QB_MEM_ADDR completed"));
        #endif
        break;
      }

      case FULL_UPDATE_INFO:
      {
        uint8_t incoming_crc = incoming_msg_ptr.ig_full_msg->crc;
        uint8_t crc_check = crc16(reinterpret_cast<uint8_t*>(incoming_msg_ptr.ig_full_msg), sizeof(ig_full_message)-2, \
                              incoming_msg_ptr.ig_full_msg->ig_header.crc_poly);
        if(incoming_crc == crc_check) {
          #ifdef DEBUG
            Serial.println(F("Received FULL_UPDATE_INFO message"));
          #endif
        }
        else {
          #ifdef DEBUG
            Serial.println(F("Wrong CRC"));
          #endif
          break;
        }
        
        if((incoming_msg_ptr.ig_full_msg->ig_header.ack_payload != ACK_PAYLOAD)
          || (incoming_msg_ptr.ig_full_msg->ig_header.sender_sequence != 0) 
          || (incoming_msg_ptr.ig_full_msg->ig_header.receiver_sequence != 0)
          || (incoming_msg_ptr.ig_full_msg->ig_header.crc_length != CRC_16)) {
          #ifdef DEBUG
            Serial.println(F("Wrong format"));
          #endif
          break;
        }
        //correct format
        uint16_t ig_addr_h = incoming_msg_ptr.ig_full_msg->ig_header.sender_e22_addr_h;
        uint8_t ig_addr_l = incoming_msg_ptr.ig_full_msg->ig_header.sender_e22_addr_l;
        uint16_t ig_addr = ig_addr_h<<8;
        ig_addr = ig_addr | ig_addr_l;

        struct mg_message{
          e22_message_header mg_header;

          uint8_t crc;
        }mg_ack_msg;

        mg_ack_msg.mg_header.message_type = ACK;
        mg_ack_msg.mg_header.sender_e22_addr_h = mg.e22_addr_h;
        mg_ack_msg.mg_header.sender_e22_addr_l = mg.e22_addr_l;
        mg_ack_msg.mg_header.ack_payload = ACK_ONLY;
        mg_ack_msg.mg_header.sender_sequence = 0;
        mg_ack_msg.mg_header.receiver_sequence = 1;
        mg_ack_msg.mg_header.crc_length = CRC_8;
        mg_ack_msg.mg_header.crc_poly = MY_CRC8_POLY;

        mg_ack_msg.crc = crc8(reinterpret_cast<uint8_t*>(&mg_ack_msg), sizeof(mg_message)-1, mg_ack_msg.mg_header.crc_poly);
        
        char* msg_str = reinterpret_cast<char*>(&mg_ack_msg);


        for(int i = 0; i<10; i++) {
          e22.sendFixedMessage(ig_addr_h, ig_addr_l, mg.e22_channel, msg_str, sizeof(mg_message));
          delay(500);
          randomSeed(analogRead(A0));
          delay(random(200));
        }

        String initial_location = String(incoming_msg_ptr.ig_full_msg->initial_latitude, 6) \
            + "," + String(incoming_msg_ptr.ig_full_msg->initial_longitude, 6);

        update_to_Google_sheet(incoming_msg_ptr.ig_full_msg->mg_mem_addr+2, incoming_msg_ptr.ig_full_msg->ic,\
          incoming_msg_ptr.ig_full_msg->hp_num, initial_location, "", \
          ig_addr, incoming_msg_ptr.ig_full_msg->qb_addr, incoming_msg_ptr.ig_full_msg->status);

        #ifdef DEBUG
          Serial.println(F("Update full message to Google Sheet completed"));
        #endif

        break;
      }

      case UPDATE_INFO:
      {
        uint8_t incoming_crc = incoming_msg_ptr.ig_update_msg->crc;
        uint8_t crc_check = crc16(reinterpret_cast<uint8_t*>(incoming_msg_ptr.ig_update_msg), sizeof(ig_update_message)-2, \
                              incoming_msg_ptr.ig_update_msg->ig_header.crc_poly);
        if(incoming_crc == crc_check) {
          #ifdef DEBUG
            Serial.println(F("Received UPDATE_INFO message"));
          #endif
        }
        else {
          #ifdef DEBUG
            Serial.println(F("Wrong CRC"));
          #endif
          break;
        }
        
        if((incoming_msg_ptr.ig_update_msg->ig_header.ack_payload != ACK_PAYLOAD)
          || (incoming_msg_ptr.ig_update_msg->ig_header.sender_sequence != 0) 
          || (incoming_msg_ptr.ig_update_msg->ig_header.receiver_sequence != 0)
          || (incoming_msg_ptr.ig_update_msg->ig_header.crc_length != CRC_16)) {
          #ifdef DEBUG
            Serial.println(F("Wrong format"));
          #endif
          break;
        }
        //correct format
        uint16_t ig_addr_h = incoming_msg_ptr.ig_update_msg->ig_header.sender_e22_addr_h;
        uint8_t ig_addr_l = incoming_msg_ptr.ig_update_msg->ig_header.sender_e22_addr_l;
        uint16_t ig_addr = ig_addr_h<<8;
        ig_addr = ig_addr | ig_addr_l;

        struct mg_message{
          e22_message_header mg_header;

          uint8_t crc;
        }mg_ack_msg;

        mg_ack_msg.mg_header.message_type = ACK;
        mg_ack_msg.mg_header.sender_e22_addr_h = mg.e22_addr_h;
        mg_ack_msg.mg_header.sender_e22_addr_l = mg.e22_addr_l;
        mg_ack_msg.mg_header.ack_payload = ACK_ONLY;
        mg_ack_msg.mg_header.sender_sequence = 0;
        mg_ack_msg.mg_header.receiver_sequence = 1;
        mg_ack_msg.mg_header.crc_length = CRC_8;
        mg_ack_msg.mg_header.crc_poly = MY_CRC8_POLY;

        mg_ack_msg.crc = crc8(reinterpret_cast<uint8_t*>(&mg_ack_msg), sizeof(mg_message)-1, mg_ack_msg.mg_header.crc_poly);
        
        char* msg_str = reinterpret_cast<char*>(&mg_ack_msg);


        for(int i = 0; i<3; i++) {
          e22.sendFixedMessage(ig_addr_h, ig_addr_l, mg.e22_channel, msg_str, sizeof(mg_message));
          delay(500);
          randomSeed(analogRead(A0));
          delay(random(200));
        }

        String current_location = String(incoming_msg_ptr.ig_update_msg->current_latitude, 6) \
            + "," + String(incoming_msg_ptr.ig_update_msg->current_longitude, 6);

        update_to_Google_sheet(incoming_msg_ptr.ig_update_msg->mg_mem_addr, 0, 0,\
        "", current_location, 0, 0, \
          incoming_msg_ptr.ig_update_msg->status);

        #ifdef DEBUG
          Serial.println(F("Update info to Google Sheet completed"));
        #endif

        break;
      }
      
      default:
        break;
    }
    
  }
}

//==============================================================================
//============================================================================== void sendData
// Subroutine for sending data to Google Sheets
void update_to_Google_sheet(uint16_t mem_addr, char ic[], char hp[], String initial_location, String current_location, \
uint16_t ig_addr, uint32_t qb_addr, uint8_t status) {
  Serial.println("==========");
  Serial.print("connecting to ");
  Serial.println(host);
  
  String m_addr_str = "";
  String ic_str = "";
  String hp_str = "";
  String il_str = "";
  String cl_str = "";
  String ig_addr_str = "";
  String status_str = "";
  String qb_addr_str = "";

  if(mem_addr){
    m_addr_str = "M_addr=" + String(mem_addr,10);
  }
  if(ic) {
    if(mem_addr) {
      ic_str = "&value1=" + String(ic);  
    }
    else {
      ic_str = "value1=" + String(ic);
    }
  }
  if(hp) {
    hp_str = "&value2=" + String(hp);
  }
  if(initial_location != "") {
    il_str = "&value3=" + String(initial_location);
  }
  if(current_location != "") {
    cl_str = "&value4=" + String(current_location);
  }
  if(ig_addr) {
    ig_addr_str = "&value5=" + String(ig_addr, 16);
  }
  if(qb_addr) {
    qb_addr_str = "&value6=" + String(qb_addr, 16);
  }
  switch(status) {
    case NOT_INIT:
    {
      status_str = "&value7=NOT_INIT";
      break;
    }
    case PRESENT:
    {
      status_str = "&value7=PRESENT";
      break;
    }
    case OPENED:
    {
      status_str = "&value7=OPENED";
      break;
    }
    case MISSING:
    {
      status_str = "&value7=MISSING";
      break;
    }
    case EMERGENCY:
    {
      status_str = "&value7=EMERGENCY";
      break;
    }
    case MOVED:
    {
      status_str = "&value7=MOVED";
      break;
    }
    default:
    {
      break;
    }
  }

  //----------------------------------------Connect to Google host
  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }
  //----------------------------------------

  //----------------------------------------Processing data and sending data
  //String url = "/macros/s/" + GAS_ID + "/exec?" + 

  String url = Modifying_link + "?" + m_addr_str + ic_str + hp_str + \
    il_str + cl_str + ig_addr_str + qb_addr_str + status_str;

  Serial.print("requesting URL: ");
  Serial.println(url);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
         "Host: " + host + "\r\n" +
         "User-Agent: BuildFailureDetectorESP8266\r\n" +
         "Connection: close\r\n\r\n");

  Serial.println("request sent");
  //----------------------------------------

  //wait to be disconnected
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }
  Serial.println("closing connection");
  Serial.println("==========");
  Serial.println();
  //----------------------------------------
} 
//==============================================================================

