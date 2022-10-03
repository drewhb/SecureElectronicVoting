//Mark Vinciguerra, Drew Brownback, Timin Kanani Group_11

//established wifi connection in menuconfig under Example Connection Configuration




#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "addr_from_stdin.h"
#include "driver/rmt.h"
#include "soc/rmt_reg.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "timer_group_struct.h"
#include "vec.h"
#include <sys/socket.h>

// RMT definitions
#define RMT_TX_CHANNEL    1     // RMT channel for transmitter
#define RMT_TX_GPIO_NUM   25    // GPIO number for transmitter signal -- A1
#define RMT_CLK_DIV       100   // RMT counter clock divider
#define RMT_TICK_10_US    (80000000/RMT_CLK_DIV/100000)   // RMT counter value for 10 us.(Source clock is APB clock)
#define rmt_item32_tIMEOUT_US   9500     // RMT receiver timeout value(us)

// UART definitions
#define UART_TX_GPIO_NUM 26 // A0
#define UART_RX_GPIO_NUM 34 // A2
#define BUF_SIZE (1024)

// Hardware interrupt definitions
#define GPIO_INPUT_IO_1       4
#define GPIO_INPUT_IO_2       4
#define ESP_INTR_FLAG_DEFAULT 0
#define GPIO_INPUT_PIN_SEL    1ULL<<GPIO_INPUT_IO_1
#define GPIO_INPUT_PIN_SEL_2    1ULL<<GPIO_INPUT_IO_2

// LED Output pins definitions
#define BLUEPIN   14
#define GREENPIN  32
#define REDPIN    15
#define ONBOARD   13

#define TIMER_DIVIDER         16    //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // to seconds
#define TIMER_INTERVAL_2_SEC  (2)
#define TIMER_INTERVAL_10_SEC (10)
#define TEST_WITH_RELOAD      1     // Testing will be done with auto reload

// Default ID/color
#define ID 3
#define COLOR 'R'
#define NUM_ESPS 3

// UDP definitions

#define IP_2 "192.168.1.130"
#define IP_1 "192.168.1.128"
#define IP_0 "192.168.1.110"

int IDS[NUM_ESPS] = {0,1,2};
int max_id;
int sock;
struct sockaddr_in dest_addr;


#define PORT 80
#define NODE_JS_IP_ADDR "192.168.1.141"
#define NODE_PORT 3000

static const char *TAG = "test";

char HOST_IP_ADDR[15];


const int isPollLeader = 1; // CHANGE THIS FOR POLL LEADER

char rx_buffer[128];

// Variables for my ID, minVal and status plus string fragments
char start = 0x1B;
char myID = (char) ID;
char myColor = (char) COLOR;
char vote[10];
int len_out = 5;
int time_to_send = 0;

// Mutex (for resources), and Queues (for button)
SemaphoreHandle_t mux = NULL;
static xQueueHandle gpio_evt_queue = NULL;
static xQueueHandle timer_queue;

// A simple structure to pass "events" to main task
typedef struct {
    int flag;     // flag for enabling stuff in timer task
} timer_event_t;

// System tags
static const char *TAG_SYSTEM = "system";       // For debug logs

// Button interrupt handler -- add to queue
static void IRAM_ATTR gpio_isr_handler(void* arg){
  uint32_t gpio_num = (uint32_t) arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

int button_state = 0;
static void IRAM_ATTR gpio_isr_handler_two(void* arg){
    button_state = 0;
}

// ISR handler
void IRAM_ATTR timer_group0_isr(void *para) {

    // Prepare basic event data, aka set flag
    timer_event_t evt;
    evt.flag = 1;

    // Yellow is shorter
    if (myColor == 'G') {
      timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, TIMER_INTERVAL_2_SEC * TIMER_SCALE);
    }
    else {
      timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, TIMER_INTERVAL_10_SEC * TIMER_SCALE);
    }

    // Clear the interrupt, Timer 0 in group 0
    TIMERG0.int_clr.t0 = 1;

    // After the alarm triggers, we need to re-enable it to trigger it next time
    TIMERG0.hw_timer[TIMER_0].config.alarm_en = TIMER_ALARM_EN;

    // Send the event data back to the main program task
    xQueueSendFromISR(timer_queue, &evt, NULL);
}

// Utilities ///////////////////////////////////////////////////////////////////

// Checksum
char genCheckSum(char *p, int len) {
  char temp = 0;
  for (int i = 0; i < len; i++){
    temp = temp^p[i];
  }
  // printf("%X\n",temp);

  return temp;
}
bool checkCheckSum(uint8_t *p, int len) {
  char temp = (char) 0;
  bool isValid;
  for (int i = 0; i < len-1; i++){
    temp = temp^p[i];
  }
  // printf("Check: %02X ", temp);
  if (temp == p[len-1]) {
    isValid = true; }
  else {
    isValid = false; }
  return isValid;
}

// Init Functions //////////////////////////////////////////////////////////////
// RMT tx init
static void rmt_tx_init() {
    rmt_config_t rmt_tx;
    rmt_tx.channel = RMT_TX_CHANNEL;
    rmt_tx.gpio_num = RMT_TX_GPIO_NUM;
    rmt_tx.mem_block_num = 1;
    rmt_tx.clk_div = RMT_CLK_DIV;
    rmt_tx.tx_config.loop_en = false;
    rmt_tx.tx_config.carrier_duty_percent = 50;
    // Carrier Frequency of the IR receiver
    rmt_tx.tx_config.carrier_freq_hz = 38000;
    rmt_tx.tx_config.carrier_level = 1;
    rmt_tx.tx_config.carrier_en = 1;
    // Never idle -> aka continuous TX of 38kHz pulses
    rmt_tx.tx_config.idle_level = 1;
    rmt_tx.tx_config.idle_output_en = true;
    rmt_tx.rmt_mode = 0;
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);
}

// Configure UART
static void uart_init() {
  // Basic configs
  uart_config_t uart_config = {
      .baud_rate = 1200, // Slow BAUD rate
      .data_bits = UART_DATA_8_BITS,
      .parity    = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };
  uart_param_config(UART_NUM_1, &uart_config);

  // Set UART pins using UART0 default pins
  uart_set_pin(UART_NUM_1, UART_TX_GPIO_NUM, UART_RX_GPIO_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  // Reverse receive logic line
  uart_set_line_inverse(UART_NUM_1,UART_SIGNAL_RXD_INV);

  // Install UART driver
  uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);
}

// GPIO init for LEDs
static void led_init() {
    esp_rom_gpio_pad_select_gpio(BLUEPIN);
    esp_rom_gpio_pad_select_gpio(GREENPIN);
    esp_rom_gpio_pad_select_gpio(REDPIN);
    esp_rom_gpio_pad_select_gpio(ONBOARD);
    gpio_set_direction(BLUEPIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREENPIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(REDPIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ONBOARD, GPIO_MODE_OUTPUT);
}

// Configure timer
static void alarm_init() {
    // Select and initialize basic parameters of the timer
    timer_config_t config;
    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = TEST_WITH_RELOAD;
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    // Timer's counter will initially start from value below
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);

    // Configure the alarm value and the interrupt on alarm
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, TIMER_INTERVAL_10_SEC * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_group0_isr,
        (void *) TIMER_0, ESP_INTR_FLAG_IRAM, NULL);

    // Start timer
    timer_start(TIMER_GROUP_0, TIMER_0);
}

// Button interrupt init
static void button_init() {
    gpio_config_t io_conf;
    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    //bit mask of the pins, use GPIO4 here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    gpio_intr_enable(GPIO_INPUT_IO_1 );
    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_1, gpio_isr_handler, (void*) GPIO_INPUT_IO_1);
    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
}

static void button_2_init() {
    gpio_config_t io_conf_2;
    //interrupt of rising edge
    io_conf_2.intr_type = GPIO_INTR_POSEDGE;
    //bit mask of the pins, use GPIO4 here
    io_conf_2.pin_bit_mask = GPIO_INPUT_PIN_SEL_2;
    //set as input mode
    io_conf_2.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf_2.pull_up_en = 1;
    gpio_config(&io_conf_2);

    gpio_intr_enable(GPIO_INPUT_IO_2);
    gpio_isr_handler_add(GPIO_INPUT_IO_2, gpio_isr_handler_two, (void*) GPIO_INPUT_IO_2);
}

////////////////////////////////////////////////////////////////////////////////

// Tasks ///////////////////////////////////////////////////////////////////////
// Button task -- rotate through myColor 
void button_task(){
  uint32_t io_num;
  while(1) {
    if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
      xSemaphoreTake(mux, portMAX_DELAY);
      if (myColor == 'R') {
        myColor = 'G';
      }
      else if (myColor == 'G') {
        myColor = 'Y';
      }
      else if (myColor == 'Y') {
        myColor = 'R';
      }
      xSemaphoreGive(mux);
      printf("Button pressed.\n");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

}

// button 2 listener
void button_2(){
  while(1) {
    if (button_state == 1) {
      //printf("Button 2 pressed.\n");
      button_state = 0;
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// Send task -- sends payload | Start | myColor | Start | myID | 
void send_task(){
  while(1) {
    char *data_out = (char *) malloc(len_out);
    xSemaphoreTake(mux, portMAX_DELAY);
    data_out[0] = start;
    //printf("%c\n",start);
    data_out[1] =  (char) myColor;
    data_out[2] = max_id;
    //printf("My ID is : %d",myID);
    data_out[3] = genCheckSum(data_out,len_out-1);
   

    uart_write_bytes(UART_NUM_1, data_out, len_out);
    xSemaphoreGive(mux);

    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

// Receives task -- looks for Start byte then stores received values
void recv_task(){
  // Buffer for input data
  uint8_t *data_in = (uint8_t *) malloc(BUF_SIZE);
  while (1) {
    int len_in = uart_read_bytes(UART_NUM_1, data_in, BUF_SIZE, 20 / portTICK_RATE_MS);
    //printf("%c\n",data_in[1]);
    //printf("%d\n",len_in);
    //printf("My ID is : %c",data_in[2]);
    if (len_in >0) {
      //printf("%d\n",len_in);
      //printf("messaged received: %c\n",data_in[1]);
      //printf("%c\n",start);
      if (data_in[0] == start) {
        printf("data in 0 == start\n");
        if (true) {
          ESP_LOG_BUFFER_HEXDUMP(TAG_SYSTEM, data_in, len_out, ESP_LOG_INFO);
            myColor = data_in[1];
            //sprintf(vote, "%d", data_in[2]);
            vote[0] = data_in[1];
            //vote[0] = (char) data_in[2]; //vote that will be sent by UDP to the poll leader
            //vector_add(&int_vec, data_in[2]);
            printf("testing recv\n");
            printf("data in %d\n",data_in[2]);//data is 3, how can you turn that into a char
            printf("vote disp %c\n",vote[0]);
            time_to_send = 1;
        }
      }
    }
    else{
      //printf("Nothing received.\n");
    }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
  free(data_in);
}

void udp_task(void *pvParameters) {
  //char rx_buffer[128];
  char addr_str[128];
  int ip_protocol = IPPROTO_IP;
  int addr_family = AF_INET;
//Moved this out of while loop

  while(1) {

  bool sock_valid = false;
   while (!sock_valid) {
    if (max_id == 0){
      strcpy(HOST_IP_ADDR,IP_0);
    } else if( max_id == 1){
      strcpy(HOST_IP_ADDR,IP_1);
    } else if( max_id == 2){
      strcpy(HOST_IP_ADDR,IP_2);
    }

    
    dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

    sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    

    if (sock < 0) {

        ESP_LOGE(TAG, "Unable to create socket, changing host: errno %d", errno);
        max_id = IDS[max_id-1];
        //break;
    } else{
      sock_valid = true;
    }
  }


    ESP_LOGI(TAG, "Socket created, sending to %s:%d", HOST_IP_ADDR, PORT);

    while (1)
    {
        if(time_to_send) {
          printf("testing send\n");
          int err = sendto(sock, vote, strlen(vote), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
          //printf("%c\n",vote[0]);
          if (err < 0) {
              ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
              break;
          }
          ESP_LOGI(TAG, "Message sent");
          time_to_send = 0;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  
    }
  }
}


// LED task to light LED based on traffic state
void led_task(){
  while(1) {
    switch((int)myColor){
      case 'R' : // Red
        gpio_set_level(GREENPIN, 0);
        gpio_set_level(REDPIN, 1);
        gpio_set_level(BLUEPIN, 0);
        // printf("Current state: %c\n",status);
        break;
      case 'Y' : // Yellow
        gpio_set_level(GREENPIN, 0);
        gpio_set_level(REDPIN, 0);
        gpio_set_level(BLUEPIN, 1);
        // printf("Current state: %c\n",status);
        break;
      case 'G' : // Green
        gpio_set_level(GREENPIN, 1);
        gpio_set_level(REDPIN, 0);
        gpio_set_level(BLUEPIN, 0);
        // printf("Current state: %c\n",status);
        break;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// LED task to blink onboard LED based on ID
void id_task(){
  while(1) {
    for (int i = 0; i < (int) myID; i++) {
      gpio_set_level(ONBOARD,1);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      gpio_set_level(ONBOARD,0);
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}


// Timer task -- R (10 seconds), G (10 seconds), Y (2 seconds)
static void timer_evt_task(void *arg) {
    while (1) {
        // Create dummy structure to store structure from queue
        timer_event_t evt;

        // Transfer from queue
        xQueueReceive(timer_queue, &evt, portMAX_DELAY);

        // Do something if triggered!
        if (evt.flag == 1) {
            printf("Action!\n");
            if (myColor == 'R') {
              myColor = 'G';
            }
            else if (myColor == 'G') {
              myColor = 'Y';
            }
            else if (myColor == 'Y') {
              myColor = 'R';
            }
        }
    }
}


void poll_leader_task(void *pvParameters)
{
  if (isPollLeader == 1)
  {

    static const char *pTAG = "POLL LEADER";
    ESP_LOGI(pTAG, "running poll leader");
    char addr_str[128];
    int ip_protocol = IPPROTO_IP;
    int addr_family = AF_INET;



    struct sockaddr_in ip_addr;
    ip_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    ip_addr.sin_family = AF_INET;
    ip_addr.sin_port = htons(PORT);

    while (1)
    {
      struct sockaddr_in dest_addr;
      dest_addr.sin_addr.s_addr = inet_addr(NODE_JS_IP_ADDR);
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = htons(NODE_PORT);

      int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
      if (sock < 0)
      {
        ESP_LOGE(pTAG, "Unable to create socket: errno %d", errno);
        break;
      }
      

      int err = bind(sock, (struct sockaddr *)&ip_addr, sizeof(ip_addr));
      if (err < 0) 
      {
        ESP_LOGE(pTAG, "Socket unable to bind: errno %d", errno);
        break;
      }
      ESP_LOGI(pTAG, "ip address: %s\n", HOST_IP_ADDR);
      ESP_LOGI(pTAG, "Socket bound, port %d", PORT);

      while (1)
      { 
        printf("Waiting for votes\n");
        // printf(" hahahahhaha\n\n");
        struct sockaddr_in source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
        printf("len: %d\n",len);
        if (len <= 0)
        {
          ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
          break;
        } 
        // send after every voting result is received?
        else
        {
          rx_buffer[len] = 0; // NULL last value
          ESP_LOGI(pTAG, "Received %d bytes", len);
          ESP_LOGI(pTAG, "%s", rx_buffer);
          //ESP_LOGI(pTAG, "ID: %c", HOST_IP_ADDR);
          if (rx_buffer[0] != '\0')
          {
            ESP_LOGI(pTAG, "Received vote");


            int err = sendto(sock, rx_buffer, strlen(rx_buffer), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            printf("Voting results: %s\n",rx_buffer);
            if (err < 0) {
                ESP_LOGE(pTAG, "Error occurred during sending: errno %d", errno);
                break;
            }
            ESP_LOGI(pTAG, "Voting results sent");
          }
        }
      }
    }
  }
  vTaskDelay(2000 / portTICK_PERIOD_MS);
}

void app_main() {

    // Mutex for current values when sending
    mux = xSemaphoreCreateMutex();

    // Create a FIFO queue for timer-based events
    timer_queue = xQueueCreate(10, sizeof(timer_event_t));

    // Create task to handle timer-based events
    xTaskCreate(timer_evt_task, "timer_evt_task", 2048, NULL, 5, NULL);

    // Initialize all the things
    rmt_tx_init();
    uart_init();
    led_init();
    alarm_init();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    max_id = IDS[0];
    for (int i = 0; i < NUM_ESPS; i++){
      if (IDS[i] > max_id){
              max_id = IDS[i];
      }
    }

    // Create tasks for receive, send, set gpio, and button
   // if (isPollLeader == 1)
      xTaskCreate(poll_leader_task, "poll_leader_task", 4096, NULL, 5, NULL);
   // else
   // {
      xTaskCreate(recv_task, "uart_rx_task", 1024*4, NULL, configMAX_PRIORITIES, NULL);
      xTaskCreate(send_task, "uart_tx_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
      xTaskCreate(led_task, "set_traffic_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
      xTaskCreate(id_task, "set_id_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
      xTaskCreate(udp_task, "udp_task", 4096, NULL, configMAX_PRIORITIES, NULL);
   // }
}
