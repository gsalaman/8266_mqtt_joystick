/******************************************************************************
MQTT joystick on 8266 thing.
******************************************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

const byte SWITCH_PIN = 0;           // Pin to control the light with
const char *ID = "Example_Switch";  // Name of our device, must be unique
const char *TOPIC = "room/light";  // Topic to subcribe to

IPAddress broker(10,0,0,17); // IP address of your MQTT broker eg. 192.168.1.50
WiFiClient wclient;

PubSubClient client(wclient); // Setup MQTT client


typedef enum
{
  STATE_DISCONNECT = 0,
  STATE_LOOKING_FOR_BROKER,
  STATE_REGISTERING_WITH_GAME,
  STATE_ACTIVE
} state_type;

void empty_serial_buffer( void )
{
  char c;

  while (Serial.available())
  {
    c = Serial.read();
  }
}

void print_top_menu( void )
{
  Serial.println("Configuration:");
  Serial.println("1....set WiFi SSID and password");
  Serial.println("2....set MQTT Broker");
  Serial.println("3....set MQTT Client Name");
}

void user_input( void )
{
  char c;

  while (Serial.available())
  {
    c = Serial.read();

    switch (c)
    {
      case '1':
        empty_serial_buffer();
        configure_wifi();
      break;

      case '2':
      break;

      case '3':
      break;

      default:
        print_top_menu();
    }
  }
  
}


#define BUF_SIZE 40
void configure_wifi( void )
{
  char ssid[BUF_SIZE];
  char pass[BUF_SIZE];
  char *curr_char = ssid;

  // disconnect from any currently assigned networks.
  WiFi.disconnect();
  
  Serial.println("Enter SSID");

  // Build up the SSID string until we hit an Enter.
  while (true)
  {
    if (Serial.available())
    {
      // Grab the next character
      *curr_char = Serial.read();
      if (*curr_char == '\n')
      {
        *curr_char = NULL;
        break;
      }
      curr_char++;
      // Need some buffer overflow checking here...
    }
  }
  Serial.print("SSID=");
  Serial.println(ssid);

  Serial.println("Enter Password");
  curr_char = pass;
  
  // Build up the password string until we hit an Enter.
  while (true)
  {
    if (Serial.available())
    {
      // Grab the next character
      *curr_char = Serial.read();
      if (*curr_char == '\n')
      {
        *curr_char = NULL;
        break;
      }
      curr_char++;
      // Need some buffer overflow checking here...
    }
  }
  Serial.print("Pass=");
  Serial.println(pass);

  Serial.print("\nConnecting to network");
  
  WiFi.begin(ssid, pass); // Connect to network

  while (WiFi.status() != WL_CONNECTED) { // Wait for connection
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void init_disconnect_state( void )
{
  Serial.print("\nConnecting to network");
  WiFi.begin(); // Connect to network, using last stored credentials.
}

state_type process_disconnect_state( void )
{
    if (WiFi.status() != WL_CONNECTED) 
    { 
      // Wait for connection
      delay(500);
      Serial.print(".");
      return STATE_DISCONNECT;
    }
    else
    {
      return STATE_LOOKING_FOR_BROKER;
    }
}

// Connect to WiFi network using stored credentials.
void setup_wifi() 
{
  Serial.print("\nConnecting to network");
  
  //WiFi.begin(ssid, password); // Connect to network
  WiFi.begin(); // Connect to network, using last stored credentials.

  while (WiFi.status() != WL_CONNECTED) { // Wait for connection
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// Reconnect to client
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(ID)) {
      Serial.println("connected");
      Serial.print("Publishing to: ");
      Serial.println(TOPIC);
      Serial.println('\n');

    } else {
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() 
{
  Serial.begin(9600); 

  Serial.println("Initializing 8266 MQTT Joystick");

  delay(100);
  init_disconnect_state();

  /*
  configure_wifi();
  pinMode(SWITCH_PIN,INPUT);  // Configure SWITCH_Pin as an input
  digitalWrite(SWITCH_PIN,HIGH);  // enable pull-up resistor (active low)
  delay(100);
  //setup_wifi(); // Connect to network
  client.setServer(broker, 1883);
  */
}

void loop()
{
  static state_type current_state=STATE_DISCONNECT;
  
  // process any user input.  This will be a blocking call to configure the appropriate data.
  user_input();  

  switch (current_state)
  {
    case STATE_DISCONNECT:
      current_state = process_disconnect_state();
    break;

    case STATE_LOOKING_FOR_BROKER:
      Serial.println("You WIN!!!!");
      delay(5000);
    break;

    case STATE_REGISTERING_WITH_GAME:
    break;

    case STATE_ACTIVE:
    break;

    default:
      Serial.println("Unexpected STATE!!!");
  }  // end of switch on current state
    
}


void old_loop() 
{
  if (!client.connected())  // Reconnect if connection is lost
  {
    reconnect();
  }
  client.loop();

  // if the switch is being pressed
  if(digitalRead(SWITCH_PIN) == 0) 
  {
    /*
    state = !state; //toggle state
    if(state == 1) // ON
    {
      client.publish(TOPIC, "on");
      Serial.println((String)TOPIC + " => on");
    }
    else // OFF
    {
      client.publish(TOPIC, "off");
      Serial.println((String)TOPIC + " => off");
    }
    */

    while(digitalRead(SWITCH_PIN) == 0) // Wait for switch to be released
    {
      // Let the ESP handle some behind the scenes stuff if it needs to
      yield(); 
      delay(20);
    }
  }
}
