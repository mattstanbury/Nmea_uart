/* NMEA Parser example, that decode data stream from GPS receiver

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nmea_parser.h"

//Wifi Access Point includes
#include <string.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

//Webserver extra #includes
#include <esp_http_server.h> 

//I2C Includes
#include "driver/gpio.h"
#include "math.h"

//pwm icludes
#include "driver/mcpwm.h"

//static const char *TAG = "gps_demo";

//Global Static variables for passing GPS lat long back to main program
static float longitudex;
static float latitudex;
static float lat_target;
static float long_target;
static float bearing;
static float distance;
static int motorgain = 50;  //overall motor gain that can be trimmed in web page for tuning pull strength 

static const char *TAG = "wifi softAP";

#define TIME_ZONE (+10)   //Sydney Time
#define YEAR_BASE (2000) //date in GPS starts from 2000

//Memory and WiFi Connect #defines
#define EXAMPLE_ESP_WIFI_SSID      "spotlock"
#define EXAMPLE_ESP_WIFI_PASS      "password"
#define EXAMPLE_ESP_WIFI_CHANNEL   1
#define EXAMPLE_MAX_STA_CONN       4

//Wifi Setup Access Point
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}
void wifi_init_softap(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }


    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

//Webserver Hard Coded HTML Web Pages
char html_index[] = "<!DOCTYPE html>"
                    "<html>"
                    "<head>"
                        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                    "</head>"    
                        "<body>"
                            "<h2 style=\"background-color:DodgerBlue; text-align:center;\">SPOT LOCK WEB PAGE</h2>"
                            "<p>Unit Position:</p>"
                            "<p>";
                            //insert live variables
char html_index_2[] =       "</p>"
                            "<p>Target Position:</p/>"
                            "<p>";
                            //insert live variables
char html_index_3[] =       "</p>"
                            "<p align=\"center\">______________________________________________________________________________</p>"
                            "<p><button type=\"button\" onclick= \"window.location.href='pg'\">+ Gain</button>       </p>";
                            //insert live variables
char html_index_4[] =       "<p>         <button type=\"button\" onclick= \"window.location.href='mg'\">- Gain</button></p>"
                            "<p align=\"center\">______________________________________________________________________________</p>"
                            "<p align=\"center\">"
                            "<button type=\"button\" onclick= \"window.location.href='N10'\" style=\"margin-left:auto;margin-right:auto;display:block;margin-top:22\%;margin-bottom:0\%\">North 10m</button>"
                            "</p>"
                            "<p align=\"center\">"
                            "<button type=\"button\" onclick= \"window.location.href='N2'\" style=\"margin-left:auto;margin-right:auto;display:block;margin-top:22\%;margin-bottom:0\%\">North 2m</button>"
                            "</p>"
                            "<p align=\"center\">"
                            "<button type=\"button\" onclick= \"window.location.href='W10'\">West 10m</button>"
                            "<button type=\"button\" onclick= \"window.location.href='W2'\">West 2m</button>"
                            "                                              "
                            "<button type=\"button\" onclick= \"window.location.href='E2'\" >East 2m</button>"
                            "<button type=\"button\" onclick= \"window.location.href='E10'\" >East 10m</button>"
                            "</p>"
                            "<p align=\"center\">"
                            "<button type=\"button\" onclick= \"window.location.href='S2'\" style=\"margin-left:auto;margin-right:auto;display:block;margin-top:22\%;margin-bottom:0\%\">South 2m</button>"
                            "</p>"
                            "<p align=\"center\">"
                            "<button type=\"button\" onclick= \"window.location.href='S10'\" style=\"margin-left:auto;margin-right:auto;display:block;margin-top:22\%;margin-bottom:0\%\">South 10m</button>"
                            "</p>"
                        "</body>"
                    "</html>";                    

//Webserver Code
esp_err_t send_page(httpd_req_t *req)
{
    int numchars;
    char response_data[sizeof(html_index) + sizeof(html_index_2) + sizeof(html_index_3) + sizeof(html_index_4) + 150]; //Create "response_data" which is an array of chars, use the content to drive the array size
    memset(response_data, 0, sizeof(response_data)); //set all of response_data to "0" chars
    numchars = sprintf(response_data, html_index);  //Stores "html_index" in response_data, records the number of chars in numchars
    numchars = numchars + sprintf(response_data + numchars, "Lat %fN, Long %fE", latitudex, longitudex); //Uses numchars to append to response_data and updates numchars
    numchars = numchars + sprintf(response_data + numchars, html_index_2);
    numchars = numchars + sprintf(response_data + numchars, "Lat %fN, Long %fE</p><p> Distance: %f  Bearing: %f", lat_target, long_target, distance, bearing);
    numchars = numchars + sprintf(response_data + numchars, html_index_3);
    numchars = numchars + sprintf(response_data + numchars, "Motor Gain:  %d   ", motorgain);
    numchars = numchars + sprintf(response_data + numchars, html_index_4);
    numchars = httpd_resp_send(req, response_data, HTTPD_RESP_USE_STRLEN);
    return numchars;
}
esp_err_t index_handler(httpd_req_t *req)
{
    return send_page(req);
}
esp_err_t minusgain_handler(httpd_req_t *req)
{   
    if (motorgain > 0){
        motorgain = motorgain -5; 
    }  
    return send_page(req);
}
esp_err_t plusgain_handler(httpd_req_t *req)
{
    if (motorgain < 100){
        motorgain = motorgain +5; 
    }  
    return send_page(req);
}
esp_err_t N10_handler(httpd_req_t *req)
{
    lat_target = lat_target + 0.0001; //North 10m  
    return send_page(req);
}
esp_err_t N2_handler(httpd_req_t *req)
{
    lat_target = lat_target + 0.00002; //North 2m  
    return send_page(req);
}
esp_err_t W10_handler(httpd_req_t *req)
{
    long_target = long_target - 0.0001;
    return send_page(req);
}
esp_err_t W2_handler(httpd_req_t *req)
{
    long_target = long_target - 0.00002;
    return send_page(req);
}
esp_err_t E2_handler(httpd_req_t *req)
{
    long_target = long_target + 0.00002;
    return send_page(req);
}
esp_err_t E10_handler(httpd_req_t *req)
{
    long_target = long_target + 0.0001;
    return send_page(req);
}
esp_err_t S2_handler(httpd_req_t *req)
{
    lat_target = lat_target - 0.00002;
    return send_page(req);
}
esp_err_t S10_handler(httpd_req_t *req)
{
    lat_target = lat_target - 0.0001;
    return send_page(req);
}
httpd_uri_t uri_index = { // "ip/"
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL};
httpd_uri_t uri_minusgain = { // "ip/mg"
    .uri = "/mg",
    .method = HTTP_GET,
    .handler = minusgain_handler,
    .user_ctx = NULL};
httpd_uri_t uri_plusgain = { // "ip/pg"
    .uri = "/pg",
    .method = HTTP_GET,
    .handler = plusgain_handler,
    .user_ctx = NULL};
httpd_uri_t uri_N10 = { 
    .uri = "/N10",
    .method = HTTP_GET,
    .handler = N10_handler,
    .user_ctx = NULL};
httpd_uri_t uri_N2 = { 
    .uri = "/N2",
    .method = HTTP_GET,
    .handler = N2_handler,
    .user_ctx = NULL};
httpd_uri_t uri_W10 = {
    .uri = "/W10",
    .method = HTTP_GET,
    .handler = W10_handler,
    .user_ctx = NULL};
httpd_uri_t uri_W2 = {
    .uri = "/W2",
    .method = HTTP_GET,
    .handler = W2_handler,
    .user_ctx = NULL};
httpd_uri_t uri_E2 = {
    .uri = "/E2",
    .method = HTTP_GET,
    .handler = E2_handler,
    .user_ctx = NULL};
httpd_uri_t uri_E10 = {
    .uri = "/E10",
    .method = HTTP_GET,
    .handler = E10_handler,
    .user_ctx = NULL};
httpd_uri_t uri_S2 = {
    .uri = "/S2",
    .method = HTTP_GET,
    .handler = S2_handler,
    .user_ctx = NULL};
httpd_uri_t uri_S10 = {
    .uri = "/S10",
    .method = HTTP_GET,
    .handler = S10_handler,
    .user_ctx = NULL};
httpd_handle_t setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_index);
        httpd_register_uri_handler(server, &uri_minusgain);
        httpd_register_uri_handler(server, &uri_plusgain);
        httpd_register_uri_handler(server, &uri_N10);
        httpd_register_uri_handler(server, &uri_N2);
        httpd_register_uri_handler(server, &uri_W10);
        httpd_register_uri_handler(server, &uri_W2);
        httpd_register_uri_handler(server, &uri_E2);
        httpd_register_uri_handler(server, &uri_E10);
        httpd_register_uri_handler(server, &uri_S2);
        httpd_register_uri_handler(server, &uri_S10);
    }

    return server;
}

//GPS Event Handler
/**
 * @brief GPS Event Handler
 *
 * @param event_handler_arg handler specific arguments
 * @param event_base event base, here is fixed to ESP_NMEA_EVENT
 * @param event_id event id
 * @param event_data event specific arguments
 */
static void gps_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    gps_t* gps = NULL;

    switch (event_id) {
    case GPS_UPDATE:
        gps = (gps_t *)event_data;
        /* print information parsed from GPS statements */
        /* Correct hours
        if ((gps->tim.hour + TIME_ZONE) > 24){
            gps->tim.hour = gps->tim.hour -24;
        }*/
        /*ESP_LOGI(TAG, "%d/%d/%d %d:%d:%d => \r\n"
                 "\t\t\t\t\t\tlatitude   = %.05f°N\r\n"
                 "\t\t\t\t\t\tlongitude = %.05f°E\r\n"
                 "\t\t\t\t\t\taltitude   = %.02fm\r\n"
                 "\t\t\t\t\t\tspeed      = %fm/s",
                 gps->date.year + YEAR_BASE, gps->date.month, gps->date.day,
                 gps->tim.hour + TIME_ZONE, gps->tim.minute, gps->tim.second,
                 gps->latitude, gps->longitude, gps->altitude, gps->speed);*/
        //printf("GPS data received\n");
        longitudex = gps->longitude;  //for export to main program
        latitudex = gps->latitude;    //for export to main program
        break;
    case GPS_UNKNOWN:
        /* print unknown statements */
        //ESP_LOGW(TAG, "Unknown statement:%s", (char *)event_data);
        break;
    default:
        break;
    }
}

//I2C Sensor Functions
void I2Cstart(){
    uint8_t mrincyclestt;
    uint8_t cycletimestt = 100;
    //Send Start condition
    /*gpio_set_direction(21, GPIO_MODE_INPUT);
    gpio_set_direction(22, GPIO_MODE_INPUT);
    printf("SCL = %d \n", gpio_get_level(22));
    printf("SDA = %d \n", gpio_get_level(21));*/

    gpio_set_level(21, 1);
    gpio_set_level(22, 1);
    gpio_set_direction(21, GPIO_MODE_OUTPUT);
    gpio_set_direction(22, GPIO_MODE_OUTPUT);
    gpio_set_level(21, 0);
    mrincyclestt= cycletimestt;
    while (mrincyclestt > 0) 
        {mrincyclestt -= 1;}
    gpio_set_level(22, 0);   
    mrincyclestt= cycletimestt; 
    while (mrincyclestt > 0) 
        {mrincyclestt -= 1;}
}
void I2Crestart(){
    uint8_t mrincyclestt;
    uint8_t cycletimestt = 100;
    //Send Start condition - THIS for the restart needed in the middle of the read
    /*gpio_set_direction(21, GPIO_MODE_INPUT);
    gpio_set_direction(22, GPIO_MODE_INPUT);
    printf("SCL = %d \n", gpio_get_level(22));
    printf("SDA = %d \n", gpio_get_level(21));*/
    
    gpio_set_direction(21, GPIO_MODE_OUTPUT);
    gpio_set_direction(22, GPIO_MODE_OUTPUT);


    mrincyclestt= 255;   //long wait
    while (mrincyclestt > 0) 
        {mrincyclestt -= 1;}
    // get back to SCL high and SDA high without causing and stop
    gpio_set_level(21, 1);   //data high
    mrincyclestt= cycletimestt;
    while (mrincyclestt > 0) 
        {mrincyclestt -= 1;}
    gpio_set_level(22, 1);   //clock high
     mrincyclestt= 255;   //long wait
    while (mrincyclestt > 0) 
        {mrincyclestt -= 1;}
    
    gpio_set_level(21, 0);  //normal I2C start
    mrincyclestt= cycletimestt;
    while (mrincyclestt > 0) 
        {mrincyclestt -= 1;}
    gpio_set_level(22, 0);   
    mrincyclestt= cycletimestt; 
    while (mrincyclestt > 0) 
        {mrincyclestt -= 1;}
}
void I2Cstop(){
    uint8_t mrincyclestp;
    uint8_t cycletimestp = 100;
    //Send Master Stop Condition   
    gpio_set_level(21, 0); 
    gpio_set_level(22, 1);
    mrincyclestp = cycletimestp; 
    while (mrincyclestp > 0) 
        {mrincyclestp -= 1;}
    gpio_set_level(21, 1); //SDA high while SCL is high
    mrincyclestp= 255; 
     while (mrincyclestp > 0) 
        {mrincyclestp -= 1;}   
}
uint8_t I2Ccheck_sack(){
    uint8_t slave_ackch = 0;
    uint8_t mrincycle;
    uint8_t cycletime = 100;
    //TEST FOR ACK FROM SLAVE (SDA GOES LOW)   
    //Speed up sda to high, weak pull up too slow? 
    gpio_set_level(21, 1);
    //RELEASE SDA
    gpio_set_direction(21, GPIO_MODE_INPUT);
    gpio_set_level(22, 1);
    mrincycle= cycletime; 
    while (mrincycle > 0) 
        {mrincycle -= 1;}
    if (gpio_get_level(21)==0){
        slave_ackch = 1;}
    gpio_set_level(22, 0);
    mrincycle= cycletime; 
    while (mrincycle > 0) 
        {mrincycle -= 1;}
    gpio_set_direction(21, GPIO_MODE_OUTPUT);  
    return slave_ackch;
} 
void I2Cmack(){
    uint8_t mrincycle;
    uint8_t cycletime = 100;
    gpio_set_direction(21, GPIO_MODE_OUTPUT); 
    gpio_set_direction(22, GPIO_MODE_OUTPUT);   
    gpio_set_level(21, 0);
    gpio_set_level(22, 1);
    mrincycle= cycletime; 
    while (mrincycle > 0) 
        {mrincycle -= 1;}
    gpio_set_level(22, 0);
    mrincycle= cycletime; 
    while (mrincycle > 0) 
        {mrincycle -= 1;}  
}
void I2Cmnack(){
    uint8_t mrincycle;
    uint8_t cycletime = 100;
    gpio_set_direction(21, GPIO_MODE_OUTPUT); 
    gpio_set_direction(22, GPIO_MODE_OUTPUT);   
    gpio_set_level(21, 1);
    gpio_set_level(22, 1);
    mrincycle= cycletime; 
    while (mrincycle > 0) 
        {mrincycle -= 1;}
    gpio_set_level(22, 0);
    mrincycle= cycletime; 
    while (mrincycle > 0) 
        {mrincycle -= 1;}  
}
void I2Cout(uint8_t bitcount, uint8_t i2cdata){
    uint8_t counteri2cout, incyclei2cout;
    gpio_set_direction(21, GPIO_MODE_OUTPUT);
    gpio_set_direction(22, GPIO_MODE_OUTPUT);
    for (counteri2cout = 0; counteri2cout < bitcount; counteri2cout++){ 
        gpio_set_level(21, ((i2cdata >> (bitcount - counteri2cout - 1)) & 1));   
        //printf("%d" , ((i2cdata >> (bitcount - counteri2cout - 1)) & 1 ));
        gpio_set_level(22, 1); //scl
        incyclei2cout = 100; 
        while (incyclei2cout > 0) 
            {incyclei2cout -= 1;}
        gpio_set_level(22, 0);
        incyclei2cout= 100; 
        while (incyclei2cout > 0) 
            {incyclei2cout -= 1;}
    
        }
    //printf("\n");
}
uint8_t I2Cin(uint8_t bitcount){
    uint8_t counteri2cin, incyclei2cin, i2cdatain = 0;
    gpio_set_direction(21, GPIO_MODE_INPUT);
    gpio_set_direction(22, GPIO_MODE_OUTPUT);
    //Small pause for INPUT to OUTPUT change
    incyclei2cin = 100; 
    while (incyclei2cin > 0) 
        {incyclei2cin -= 1;}
    for (counteri2cin = 0; counteri2cin < bitcount; counteri2cin++){ 
        gpio_set_level(22, 1); //scl
        incyclei2cin = 20; 
        while (incyclei2cin > 0) 
            {incyclei2cin -= 1;}
        i2cdatain = i2cdatain * 2 + gpio_get_level(21);
        gpio_set_level(22, 0);
        incyclei2cin= 20; 
        while (incyclei2cin > 0) 
            {incyclei2cin -= 1;}
        }
    gpio_set_direction(21, GPIO_MODE_OUTPUT);   
    return i2cdatain;
}
uint8_t I2C_Setup_Mag(uint8_t slave_addr)
{
    uint8_t slave_ack = 0;

    //setup pins
    gpio_reset_pin(21); //data SDA
    gpio_pullup_en(21);
    gpio_reset_pin(22); //clock SCL
    gpio_pullup_en(22);

    /*//Write to 0BH, value 0x139 , reset factory values, 8 gouse, set and reset off
    //Send Start condition
    I2Cstart();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(7, slave_addr);
    //send Write bit (0)
    I2Cout(1, 0);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //Write to 29H, value 0x06 , axis signs 
    I2Cout(8, 11);  //29H
    //TEST FOR ACK FROM Magnetometer   
    slave_ack += I2Ccheck_sack();
    I2Cout(8, 139);  //  data  139 soft reset 8 gouse reset and set off
    //TEST FOR ACK FROM Magnetometer 
    slave_ack += I2Ccheck_sack();
    //Send Master Stop Condition   
    I2Cstop();*/

    //Write to 29H, value 0x06 , axis signs 
    //Send Start condition
    I2Cstart();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(7, slave_addr);
    //send Write bit (0)
    I2Cout(1, 0);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //Write to 29H, value 0x06 , axis signs 
    I2Cout(8, 41);  //29H
    //TEST FOR ACK FROM Magnetometer   
    slave_ack += I2Ccheck_sack();
    I2Cout(8, 6);  // 0x06 data
    //TEST FOR ACK FROM Magnetometer 
    slave_ack += I2Ccheck_sack();
    //Send Master Stop Condition   
    I2Cstop();

    //Write to 0BH, Range and reset mode
    //Send Start condition
    I2Cstart();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(7, slave_addr);
    //send Write bit (0)
    I2Cout(1, 0);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //Write to 0BH, 
    I2Cout(8, 8);  //0BH
    //TEST FOR ACK FROM Magnetometer   
    slave_ack += I2Ccheck_sack();
    I2Cout(8, 11);  //  data 15 = 2 gause range, set and reset off, 11 = 8 gause range, set and reset off
    //TEST FOR ACK FROM Magnetometer 
    slave_ack += I2Ccheck_sack();
    //Send Master Stop Condition   
    I2Cstop();

    //Write to 0AH , filter, rates and operating mode
    //Send Start condition
    I2Cstart();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(7, slave_addr);
    //send Write bit (0)
    I2Cout(1, 0);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //Write to 0AH, value 0x06 , axis signs 
    I2Cout(8, 10);  //0AH
    //TEST FOR ACK FROM Magnetometer   
    slave_ack += I2Ccheck_sack();
    I2Cout(8, 131);  // data 1000 0011 131 continuous mode, 129 normal mode
    //TEST FOR ACK FROM Magnetometer 
    slave_ack += I2Ccheck_sack();
    //Send Master Stop Condition   
    I2Cstop();


    //printf("Slave_ack = %d\n", slave_ack);
    if(slave_ack == 9){
        slave_ack = 1;
    }
    else {
        slave_ack = 0;
    }
    return slave_ack;
}
uint8_t I2C_fetch_data(uint8_t slave_addr, int32_t* xmag_addr, int32_t* ymag_addr)
{
    uint8_t slave_ack = 0;
    uint32_t data = 0;
     //Read from 02H, x-axis sign and MSBs 
    //Send Start condition
    I2Cstart();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(7, slave_addr);
    //send Write bit (0)
    I2Cout(1, 0);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(8, 2);  //address of the data to read
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //pause and send restart without sending stop...from datasheet
    I2Crestart();
    I2Cout(7, slave_addr);
    //send read bit (1)
    I2Cout(1, 1);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    data = I2Cin(8);
    //send i2c master NACK
    I2Cmnack();
    //send stop condition
     I2Cstop();
    data = data * 256;  //move up 8 MSBs
    //Read from 01H, x-axis LSBs 
    //Send Start condition
    I2Cstart();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(7, slave_addr);
    //send Write bit (0)
    I2Cout(1, 0);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(8, 1);  //address of the data to read
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //pause and send restart without sending stop...from datasheet
    I2Crestart();
    I2Cout(7, slave_addr);
    //send read bit (1)
    I2Cout(1, 1);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    data = data + I2Cin(8);  //add LSBs to shifted MSBs
    //send i2c master NACK
    I2Cmnack();
    //send stop condition
    I2Cstop();
    // Make data available in main routine
    *xmag_addr = data;

 
 //Read from 06H, y-axis sign and MSBs actual z relative to hardware 
    //Send Start condition
    I2Cstart();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(7, slave_addr);
    //send Write bit (0)
    I2Cout(1, 0);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(8, 6);  //address of the data to read
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //pause and send restart without sending stop...from datasheet
    I2Crestart();
    I2Cout(7, slave_addr);
    //send read bit (1)
    I2Cout(1, 1);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    data = I2Cin(8);
     //send i2c master NACK
    I2Cmnack();
    //send stop condition
     I2Cstop();
    data = data * 256;  //move up 8 MSBs
    //Read from 04H, y-axis LSBs 
    //Send Start condition
    I2Cstart();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(7, slave_addr);
    //send Write bit (0)
    I2Cout(1, 0);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //CLOCK OUT ADDRESS FOR Magnetometer 7 bits
    I2Cout(8, 5);  //address of the data to read
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    //pause and send restart without sending stop...from datasheet
    I2Crestart();
    I2Cout(7, slave_addr);
    //send read bit (1)
    I2Cout(1, 1);
    //TEST FOR ACK FROM Magnetometer
    slave_ack += I2Ccheck_sack();
    data = data + I2Cin(8);  //add LSBs to shifted MSBs
    //send i2c master NACK
    I2Cmnack();
    //send stop condition
    I2Cstop();
    // Make data available in main routine
    *ymag_addr = data;



    if(slave_ack == 12){
        slave_ack = 1;
    }
    else {
        slave_ack = 0;
    }
    return slave_ack;
}

//Return the heading from the magnetometer
void Get_Heading(uint8_t slave_addr, float_t* heading, int32_t* xmagmax, int32_t* xmagmin,int32_t* ymagmax,int32_t* ymagmin){
        int32_t xmag;
        int32_t ymag;
        int32_t xmidpoint;
        int32_t ymidpoint;
        int32_t xhalfspan;
        int32_t yhalfspan;
    
        //Send Measurement Request only continue with good read data
        if (I2C_fetch_data(slave_addr, &xmag, &ymag) ==1){ 
            //strip of MSB which is sign bit, 2's complement conversion
            if (xmag & (32768) ) {
                xmag = xmag - 65536;
            }
            if (ymag & (32768)) {
                ymag = ymag - 65536;
            }

            //correct x axis reversed in hardware config (using z as y)
            xmag = xmag * -1;

            //look for new limit data 
            if (xmag < *xmagmin){
                *xmagmin = xmag;
            }
            if (xmag > *xmagmax){
                *xmagmax = xmag;
            }
           if (ymag < *ymagmin){
                *ymagmin = ymag;
            }
           if (ymag > *ymagmax){
                *ymagmax = ymag;
            }
           
            //adjust xmag, ymag to be relative to the limit data   between -100 and +100%
            xhalfspan = (*xmagmax - *xmagmin)/2;
            xmidpoint = *xmagmax- xhalfspan;
            xmag = (xmag - xmidpoint)*100;
            xmag = xmag/xhalfspan;
            yhalfspan = (*ymagmax - *ymagmin)/2;
            ymidpoint = *ymagmax- yhalfspan;
            ymag = (ymag - ymidpoint)*100;
            ymag = ymag/yhalfspan;
            // sort out heading in each quadrant
            if (ymag != 0){
                *heading = (1000*xmag)/ymag;
                *heading = *heading / 1000;
                //printf("raw: %d / %d= %f   ", xmag, ymag, *heading);
            }
            if (xmag >= 0) {
                if (ymag > 0){
                    *heading = atan(*heading);
                }
                if (ymag < 0){
                    *heading = 3.14159 + atan(*heading);
                }
                if (ymag == 0){
                    *heading = 3.14159/2;
                }
            }
            if (xmag < 0) {
                if (ymag > 0){
                    *heading = 2*3.14159 + atan(*heading);
                }
                if (ymag < 0){
                    *heading = 3.14159 + atan(*heading);
                }
                if (ymag == 0){
                    *heading = 1.5*3.14159;
                }
            }
            //change from radians to degrees
            *heading = *heading * 180;
            *heading = *heading / 3.14159;     
            return;
        }
}

//Init pwm motor putputs
void init_pwm(){
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, 12); // port motor pin 12
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, 13); // starbord motor pin 13
    mcpwm_config_t pwm_config = {
        .frequency = 100, // frequency = 100Hz
        .cmpr_a = 0,     // duty cycle of PWMxA = 0
        .cmpr_b = 0,    // duty cycle of PWMxB = 0
        .counter_mode = MCPWM_UP_COUNTER,
        .duty_mode = MCPWM_DUTY_MODE_0,
    };
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
}

void app_main(void)
{   
    //Wifi Access Point Start
    wifi_init_softap();
    //Webserver Start
    setup_server();
    //initialize GPS related variables and operations
    uint8_t gps_active = 0;
    float lat_offset, long_offset;
    /* NMEA parser configuration */
    nmea_parser_config_t config = NMEA_PARSER_CONFIG_DEFAULT();
    /* init NMEA parser library */
    nmea_parser_handle_t nmea_hdl = nmea_parser_init(&config);
    /* register event handler for NMEA parser library */
    nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
    //Initialize heading variables that must be retained between function calls
    int32_t xmagmax = 20;
    int32_t ymagmax =20;
    int32_t xmagmin = 20;
    int32_t ymagmin =20;
    float_t heading = 0;
    float_t coursecorrection;
    //Initialise Magnetometer, address 1CH, 0x28, 001 1100
    uint8_t slave_addr = 28;
    if (I2C_Setup_Mag(slave_addr) == 1){
        printf("Magnetometer setup Good \n");
    }

    while(1) {  // PROGRAM LOOP FOR REPEAT READS OF SENSORs
        //Check if gps is active, store first reported position (for development only,  start stop of machine later)
        if (gps_active == 0){//test for activation
            if (latitudex !=0){
                //gps has become active for the first time, store target coords at current position
                lat_target = latitudex;
                long_target = longitudex;
                gps_active = 1;
            }
        }    
        if (gps_active == 1){ //calculate current bearing and distance to target
            lat_offset = lat_target - latitudex;  //makes go N to target a positive when south of the equator
            long_offset = long_target - longitudex; //makes go E to target a positive when east of Grenwich          
            // sort out bearing in each quadrant
            if (lat_offset != 0){
                bearing = (1000*long_offset)/lat_offset;
                bearing = bearing / 1000;
            }
            if (long_offset  >= 0) {
                if (lat_offset > 0){
                    bearing = atan(bearing);
                }
                if (lat_offset < 0){
                    bearing = 3.14159 + atan(bearing);
                }
                if (lat_offset == 0){
                    bearing = 3.14159/2;
                }
            }
            if (long_offset < 0) {
                if (lat_offset > 0){
                    bearing = 2*3.14159 + atan(bearing);
                }
                if (lat_offset < 0){
                    bearing = 3.14159 + atan(bearing);
                }
                if (lat_offset == 0){
                    bearing = 1.5*3.14159;
                }
            }
            //change from radians to degrees
            bearing = bearing * 180;
            bearing = bearing / 3.14159;    
            //distance
            distance = lat_offset*lat_offset + long_offset*long_offset;
            distance = sqrt(distance);
            distance = distance * 111204;  //changes pythag angles to metres         
        }
        Get_Heading(slave_addr, &heading, &xmagmax, &xmagmin, &ymagmax, &ymagmin);

        //Create course correction, angle through which unit must turn, +ve is to starbord, -180 < coursecorrection < 180
        coursecorrection = bearing - heading;
        if (coursecorrection >= 180){  //take shortest option to port
            coursecorrection = (bearing - heading) - 360;
        }
        else {
            if (coursecorrection < -180 ){  //take shortest option to starbord
                coursecorrection = 360 + (bearing - heading);
            }
        }

        //Check and record historical drift speed and direction, account for overshot
            //build drift history
            //detect overshot
        //detect excursion
        
        //printf("Heading = %f, lat = %.05f°N, long = %.05f°E, Bearing = %f, Dist = %f, CC = %f\n", heading, latitudex, longitudex, bearing, distance, coursecorrection);
        //Calculate output power response, use PD contorol loop to always drift short of target, prevent overshot and spinning
        //Update motor commands
        mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 50);  //set to 50% dummy value
        mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, 30);  //set to 30% dummy value

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}