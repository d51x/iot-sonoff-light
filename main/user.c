#include "user.h"
#include "button.h"
#include "http_page_tpl.h"

#include "ipwm.h"

#define USER_HTTP_CLIENT
//#define USER_CONFIG_RELAY_GPIO
//#define USER_CONFIG_LED_GPIO

#ifdef USER_HTTP_CLIENT
    #include "esp_http_client.h"
#endif

static const char *TAG = "USER";

#define SONOFF_LIGHT_RELAY_GPIO 12
#define SONOFF_LIGHT_BUTTON_GPIO 0          // mini: 4; touch 1 gang: 0 
#define SONOFF_LIGHT_BLUE_LED_GPIO 13

relay_handle_t relay_led_h;

#define BUTTON_SHORT_PRESS_DELAY 400
#define BUTTON_SHORT_PRESS_COUNT 3

#define BUTTON_LONG_PRESS_COUNT 2
#define BUTTON_LONG_PRESS_1_DELAY 1
#if BUTTON_LONG_PRESS_COUNT > 1
#define BUTTON_LONG_PRESS_2_DELAY 2
#endif

//#define BUTTON_PRESS_AND_HOLD_COUNT 1
#ifdef BUTTON_PRESS_AND_HOLD_COUNT
#define BUTTON_PRESS_AND_HOLD_DELAY 5
#endif

#define USER_GET_PARAM1_TAG "sl"

#define USER_PARAM_SONOFF_LIGHT_SECTION USER_GET_PARAM1_TAG


#define DUTY_DAY 0
#define DUTY_NIGHT 190
#define DUTY_OFF 255
#define TIME_START_NIGHT 1320 // minutes of day
#define TIME_STOP_NIGHT 420 // minutes of day

const char *html_page_config_buttons ICACHE_RODATA_ATTR = "<h4>Button options:</h4>";

//#ifdef USER_CONFIG_RELAY_GPIO
//const char *html_page_config_gpio_relay ICACHE_RODATA_ATTR = "Relay GPIO";
//const char *html_page_config_gpio_relay_name ICACHE_RODATA_ATTR = "relpin";



const char *html_page_config_gpio_relay_save_title ICACHE_RODATA_ATTR = "Save light state";
const char *html_page_config_gpio_relay_save_name ICACHE_RODATA_ATTR = "rsv";
const char *html_page_config_gpio_relay_state_name ICACHE_RODATA_ATTR = "rst";
//#endif

const char *html_page_config_blue_led_config_title ICACHE_RODATA_ATTR = "<h4>Blue led:</h4>";
const char *html_page_config_duty_night_title ICACHE_RODATA_ATTR = "Duty night";
const char *html_page_config_duty_night_name ICACHE_RODATA_ATTR = "dtn";
const char *html_page_config_duty_day_title ICACHE_RODATA_ATTR = "Duty day";
const char *html_page_config_duty_day_name ICACHE_RODATA_ATTR = "dtd";
const char *html_page_config_night_start_title ICACHE_RODATA_ATTR = "Dark start (minutes)";
const char *html_page_config_night_start_name ICACHE_RODATA_ATTR = "drks";
const char *html_page_config_night_stop_title ICACHE_RODATA_ATTR = "Dark finish (minutes)";
const char *html_page_config_night_stop_name ICACHE_RODATA_ATTR = "drkf";

const char *html_page_config_rcdata_title ICACHE_RODATA_ATTR = "RC Codes[4] (mqtt)";
const char *html_page_config_rcdata_name ICACHE_RODATA_ATTR = "rcdata";

const char *html_page_config_rctopic_title ICACHE_RODATA_ATTR = "RC Topic (mqtt)";
const char *html_page_config_rctopic_name ICACHE_RODATA_ATTR = "rctopic";

const char *html_page_config_gpio_button ICACHE_RODATA_ATTR = "Button GPIO";
const char *html_page_config_gpio_button_name ICACHE_RODATA_ATTR = "bpin";

// #ifdef USER_CONFIG_LED_GPIO
// const char *html_page_config_gpio_led ICACHE_RODATA_ATTR = "Led GPIO";
// const char *html_page_config_gpio_led_name ICACHE_RODATA_ATTR = "ledpin";
// #endif

const char *html_page_config_button_value_name ICACHE_RODATA_ATTR = "sv%d";
const char *html_page_config_button_delay_name ICACHE_RODATA_ATTR = "sd%d";

#define USER_SELECT "sel%d"
const char *html_page_config_select_start ICACHE_RODATA_ATTR =  "<select name=\""USER_SELECT"\">";

#define BUTTON_VALUE_INT_SIZE 4    
#define BUTTON_VALUE_STRING_SIZE 64

#define html_page_config_select_end html_select_end

#define html_page_config_item html_select_item

void button_press_handler(void *args);

uint8_t relay_gpio = SONOFF_LIGHT_RELAY_GPIO;
relay_state_t relay_state = RELAY_STATE_CLOSE;
uint8_t relay_save = 0;
uint8_t button_gpio = SONOFF_LIGHT_BUTTON_GPIO;
uint32_t blue_led_gpio = SONOFF_LIGHT_BLUE_LED_GPIO;

uint8_t duty_night = DUTY_NIGHT;
uint8_t duty_day = DUTY_DAY;
uint16_t dark_start = TIME_START_NIGHT;
uint16_t dark_stop = TIME_STOP_NIGHT;

#define RC_TOPIC_LENGTH 32
char rctopic[RC_TOPIC_LENGTH];

#define RC_DATA_COUNT 4
#define RC_DATA_LEN 10
typedef struct {
    char rccode[RC_DATA_LEN];
} rcdata_t;

uint8_t rccode_first[RC_DATA_COUNT];
rcdata_t rcdata[RC_DATA_COUNT];

typedef enum {
      USR_BUTTON_SHORT_PRESS_1
    
    #if BUTTON_SHORT_PRESS_COUNT > 1
    , USR_BUTTON_SHORT_PRESS_2
    #endif
    
    #if BUTTON_SHORT_PRESS_COUNT > 2
    , USR_BUTTON_SHORT_PRESS_3
    #endif

    , USR_BUTTON_LONG_PRESS_1

    #if BUTTON_LONG_PRESS_COUNT > 1
    , USR_BUTTON_LONG_PRESS_2
    #endif

    #ifdef BUTTON_PRESS_AND_HOLD_COUNT
    , USR_BUTTON_HOLD
    #endif

    , USR_BUTTON_MAX
} button_press_type_e;

const char *button_types[USR_BUTTON_MAX] ICACHE_RODATA_ATTR = 
{
    "Short1"
    #if BUTTON_SHORT_PRESS_COUNT > 1
    , "Short2"
    #endif
    #if BUTTON_SHORT_PRESS_COUNT > 2
    , "Short3"
    #endif

    , "Long1"

    #if BUTTON_LONG_PRESS_COUNT > 1
    , "Long2"
    #endif

    #ifdef BUTTON_PRESS_AND_HOLD_COUNT
    , "Hold"
    #endif
};
typedef enum {
    USR_BUTTON_ACTION_NONE
    , USR_BUTTON_ACTION_LOCAL_GPIO
    
    #ifdef USER_HTTP_CLIENT
    , USR_BUTTON_ACTION_SEND_HTTP_GET
    #endif

    , USR_BUTTON_ACTION_PUBLISH_MQTT     // 0 - выкл, 1 вкл, 2 togle 
    , USR_BUTTON_ACTION_MAX
} button_action_type_e;

const char *button_actions[USR_BUTTON_ACTION_MAX] ICACHE_RODATA_ATTR = 
{
    "None"
    , "Toggle Light"
    
    #ifdef USER_HTTP_CLIENT
    , "Send HTTP"
    #endif

    , "Publish MQTT"
};
typedef struct {
    button_press_type_e type;
    button_action_type_e action;
    char *value;
    uint8_t delay;
} button_press_type_t;

button_press_type_t button_press_config[USR_BUTTON_MAX] = 
{
    {USR_BUTTON_SHORT_PRESS_1, USR_BUTTON_ACTION_NONE, NULL, 0}

    #if BUTTON_SHORT_PRESS_COUNT > 1
    , {USR_BUTTON_SHORT_PRESS_2, USR_BUTTON_ACTION_NONE, NULL, 0}
    #endif

    #if BUTTON_SHORT_PRESS_COUNT > 2
    , {USR_BUTTON_SHORT_PRESS_3, USR_BUTTON_ACTION_NONE, NULL, 0}
    #endif

    , {USR_BUTTON_LONG_PRESS_1, USR_BUTTON_ACTION_NONE, NULL, 0}

    #if BUTTON_LONG_PRESS_COUNT > 1
    , {USR_BUTTON_LONG_PRESS_2, USR_BUTTON_ACTION_NONE, NULL, 0}
    #endif

    #ifdef BUTTON_PRESS_AND_HOLD_COUNT
    , {USR_BUTTON_HOLD, USR_BUTTON_ACTION_NONE, NULL, 0}
    #endif
};


void user_load_nvs();
void user_save_nvs();

void switch_local_gpio()
{
    relay_state = relay_read(relay_h);
    relay_state = !relay_state;
    relay_write(relay_h, relay_state);  
    //relay_write(relay_led_h, !relay_state);   

    if ( relay_save ) {
        ESP_LOGW(TAG, "save relay_state = %d", relay_state);
        nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_relay_state_name, relay_state);
    }
}

void user_setup(void *args)
{
    user_load_nvs();

    #ifdef CONFIG_COMPONENT_RELAY

    ESP_LOGW(TAG, LOG_FMT("relay_save = %d"), relay_save);
    ESP_LOGW(TAG, LOG_FMT("relay_state = %d"), relay_state);

    relay_h = relay_create( "Light", relay_gpio, RELAY_LEVEL_LOW /*RELAY_LEVEL_LOW*/ /* RELAY_LEVEL_HIGH*/ , relay_save);
    relay_write(relay_h,  (relay_save) ? relay_state : RELAY_STATE_CLOSE);    

    ESP_LOGW(TAG, LOG_FMT("relay_h = %p"), relay_h);
    
    //relay_led_h = relay_create( "Led", blue_led_gpio, RELAY_LEVEL_HIGH /*RELAY_LEVEL_LOW*/ /* RELAY_LEVEL_HIGH*/ , false);
    //relay_write(relay_led_h,  (relay_save) ? !relay_state : RELAY_STATE_OPEN);    
    //ESP_LOGW(TAG, LOG_FMT("relay_led_h = %p"), relay_led_h);
    uint32_t *ch = &blue_led_gpio;

    pwm_begin(PWM_FREQ_HZ, 1, ch);
    //pwm_start();
    //relay_handle_t relay2_h = relay_create( "red", 15, RELAY_LEVEL_LOW /*RELAY_LEVEL_LOW*/ /* RELAY_LEVEL_HIGH*/ , false);
    //relay_write(relay2_h,  RELAY_STATE_CLOSE); 

    //relay_handle_t relay3_h = relay_create( "blue", 13, RELAY_LEVEL_LOW /*RELAY_LEVEL_LOW*/ /* RELAY_LEVEL_HIGH*/ , false);
    //relay_write(relay3_h,  RELAY_STATE_CLOSE);

    //button_handle_t btn_g4_h = configure_push_button(GPIO_NUM_4, BUTTON_ACTIVE_HIGH);
    button_handle_t btn_h = configure_push_button(button_gpio, BUTTON_ACTIVE_LOW);
    if (btn_h) 
    {
        // регистрируем коллбек короткого нажатия
        
        button_cb *short_pressed_cb = calloc(BUTTON_SHORT_PRESS_COUNT, sizeof(button_cb));
        button_press_type_t **short_pressed_args_cb = calloc(BUTTON_SHORT_PRESS_COUNT, sizeof(button_press_type_t*));
        // заполним массив указателями на функции
        short_pressed_cb[0] = &button_press_handler;
        short_pressed_args_cb[0] = &button_press_config[USR_BUTTON_SHORT_PRESS_1];

        #if BUTTON_SHORT_PRESS_COUNT > 1 
            short_pressed_cb[1] = &button_press_handler; 
            short_pressed_args_cb[1] = &button_press_config[USR_BUTTON_SHORT_PRESS_2];                    
        #endif

        #if BUTTON_SHORT_PRESS_COUNT > 2    
            short_pressed_cb[2] = &button_press_handler;
            short_pressed_args_cb[2] = &button_press_config[USR_BUTTON_SHORT_PRESS_3];     
        #endif

        // 1..3 коротких нажатий в течение 500 мсек
        button_set_on_presscount_cb(btn_h, BUTTON_SHORT_PRESS_DELAY, BUTTON_SHORT_PRESS_COUNT, short_pressed_cb, short_pressed_args_cb);

        // сработает при отпускании после X сек не зависимо сколько держали по времени
        button_add_on_release_cb(btn_h
                                , button_press_config[USR_BUTTON_LONG_PRESS_1].delay > 0 ? button_press_config[USR_BUTTON_LONG_PRESS_1].delay : BUTTON_LONG_PRESS_1_DELAY
                                , button_press_handler
                                , (void *)&button_press_config[USR_BUTTON_LONG_PRESS_1]);

        #if BUTTON_LONG_PRESS_COUNT > 1
        button_add_on_release_cb(btn_h
                                , button_press_config[USR_BUTTON_LONG_PRESS_2].delay > 0 ? button_press_config[USR_BUTTON_LONG_PRESS_2].delay : BUTTON_LONG_PRESS_2_DELAY
                                , button_press_handler
                                , (void *)&button_press_config[USR_BUTTON_LONG_PRESS_2]);        
        #endif

        // сработает при удержании более X сек
        #ifdef BUTTON_PRESS_AND_HOLD_COUNT
        button_add_on_press_cb(btn_h
                                , button_press_config[USR_BUTTON_HOLD].delay > 0 ? button_press_config[USR_BUTTON_HOLD].delay : BUTTON_PRESS_AND_HOLD_DELAY
                                , button_press_handler
                                , (void *)&button_press_config[USR_BUTTON_HOLD]);
        #endif
    }
    #endif
}

void user_http_init(void *args)
{
   
}

static void rcdata_recv_cb(char *buf, void *args)
{
    //uint8_t value = atoi( buf );
    ESP_LOGW(TAG, "%s: rcdata = %s", __func__, buf);
    for ( uint8_t i = 0; i < RC_DATA_COUNT; i++)
    {
        if ( strcmp(buf, rcdata[i].rccode) == 0 )
        {
            if ( rccode_first[i] == 0 )
            {
                switch_local_gpio();
            }
            rccode_first[i] = 0;
            break; // достаточно одного из 4-х совпадений по коду 
        }
    }
}

// функция вызывается после user_setup и старта mqtt
// в этой функции можно зарегистрировать свои кастомные колбеки на отправку и получение данных
void user_mqtt_init(void *args)
{
    // TODO: check topic length and NULL
    memset(&rccode_first, 1, sizeof(uint8_t) * RC_DATA_COUNT);
    mqtt_add_receive_callback(rctopic, 0, rcdata_recv_cb, NULL);
}

// функция вызывает в основном цикле каждую секунду
void user_loop(uint32_t sec)
{
    uint8_t level = gpio_get_level( ((relay_t *)relay_h)->pin );

    struct tm timeinfo;
    get_timeinfo(&timeinfo);

    uint16_t minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    if (  !level )
    {
        // ESP_LOGW(TAG, "minutes = %d, night start = %d, stop = %d, duty night = %d, day = %d"
        //             , minutes
        //             , dark_start
        //             , dark_stop
        //             , duty_night
        //             , duty_day);
        if ( (minutes >= dark_start || minutes <= dark_stop) && ( timeinfo.tm_year >  (2016 - 1900) ))
        {
            pwm_write(0, duty_night);   
        }
        else    
        {
            pwm_write(0, duty_day);   
        }
    }
    else
    {
        pwm_write(0, DUTY_OFF);   
    }
        
    pwm_start();  
}

#ifdef CONFIG_USER_WEB_PRINT
// функция вывод данные в пользовательском блоке на главной
void user_web_main(httpd_req_t *req)
{
}
#endif 

#ifdef CONFIG_USER_WEB_CONFIG
static void user_print_select(httpd_req_t *req, uint8_t id, button_action_type_e action)
{
    httpd_resp_sendstr_chunk_fmt(req, html_page_config_select_start, id);
    for ( uint8_t i = 0;  i < USR_BUTTON_ACTION_MAX; i++ ) 
    {
        httpd_resp_sendstr_chunk_fmt(req, html_page_config_item
                                                , i
                                                , (action == i) ? html_selected : ""
                                                , button_actions[i]);

    }
    httpd_resp_sendstr_chunk(req, html_page_config_select_end);
}

static void user_print_button_config(httpd_req_t *req, uint8_t id)
{

    char s[10];
    itoa(id, s, 10);

    //httpd_resp_sendstr_chunk(req, html_page_config_btn_cfg_start);

    //httpd_resp_sendstr_chunk(req, "<p>");
    
    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label, button_types[id]);

    user_print_select(req, id, button_press_config[id].action);

    //httpd_resp_sendstr_chunk(req, "</p>");

    if ( button_press_config[id].action > USR_BUTTON_ACTION_LOCAL_GPIO )
    {
        sprintf(s, html_page_config_button_value_name, id+1);
        httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                        , "Value"
                                        , s
                                        , button_press_config[id].value
        ); 
    }

    if ( button_press_config[id].type >= USR_BUTTON_LONG_PRESS_1 )
    {
        sprintf(s, html_page_config_button_delay_name, id+1);
        char d[4];
        itoa(button_press_config[id].delay, d, 10);
        httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                        , "Delay"
                                        , s
                                        , d
        );
    } 
    //httpd_resp_sendstr_chunk(req, html_block_data_end);
}

void user_web_options(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req, html_block_data_form_start);
    
    // print GPIO for RELAY   
    char value[4];

    // #ifdef USER_CONFIG_RELAY_GPIO
    // itoa(relay_gpio, value, 10);    
    // httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
    //                                 , html_page_config_gpio_relay // %s label
    //                                 , html_page_config_gpio_relay_name   // %s name
    //                                 , value   // %d value
    // );
    // #endif

    // print GPIO for Button
    //USER_WEB_PRINT("Hello User1 Options!");
    itoa(button_gpio, value, 10);    
    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                    , html_page_config_gpio_button // %s label
                                    , html_page_config_gpio_button_name   // %s name
                                    , value   // %d value
    );

    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_checkbox
                                , html_page_config_gpio_relay_save_title // %s label
                                , html_page_config_gpio_relay_save_name   // %s name
                                , !relay_save  // %d value
                                , relay_save ? html_checkbox_checked : ""
                                );


    
    httpd_resp_sendstr_chunk(req, html_page_config_blue_led_config_title);
    itoa(duty_night, value, 10);    
    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                    , html_page_config_duty_night_title // %s label
                                    , html_page_config_duty_night_name   // %s name
                                    , value   // %d value
    );

    itoa(duty_day, value, 10);    
    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                    , html_page_config_duty_day_title // %s label
                                    , html_page_config_duty_day_name   // %s name
                                    , value   // %d value
    );

    itoa(dark_start, value, 10);    
    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                    , html_page_config_night_start_title // %s label
                                    , html_page_config_night_start_name   // %s name
                                    , value   // %d value
    );    

    itoa(dark_stop, value, 10);    
    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                    , html_page_config_night_stop_title // %s label
                                    , html_page_config_night_stop_name   // %s name
                                    , value   // %d value
    );

    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                    , html_page_config_rctopic_title // %s label
                                    , html_page_config_rctopic_name   // %s name
                                    , rctopic   // %d value
    );

    // rcdata
    char srcdata[ sizeof(rcdata_t) * RC_DATA_COUNT + RC_DATA_COUNT] = "";
    for (uint8_t i=0; i<RC_DATA_COUNT;i++)
    {
        if ( strlen(rcdata[i].rccode) > 0 )
        {
            strcat(srcdata, rcdata[i].rccode);
            if ( i < RC_DATA_COUNT-1 ) strcat(srcdata, ";");
        }
    }
    
    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
                                    , html_page_config_rcdata_title // %s label
                                    , html_page_config_rcdata_name   // %s name
                                    , srcdata   // %d value
    );

    // print led gpio
    // #ifdef USER_CONFIG_LED_GPIO
    // itoa(blue_led_gpio, value, 10);    
    // httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_item_label_edit
    //                                 , html_page_config_gpio_led // %s label
    //                                 , html_page_config_gpio_led_name   // %s name
    //                                 , value   // %d value
    // );
    // #endif

    //httpd_resp_sendstr_chunk(req, "<hr>");

    // button options
    // Short press:
    // 1st short: type[select] value=text
    // 2nd short: type[select] value=text
    // 3rd short: type[select] value=text
    // type:                    value:
    // 0 - none,                ignored = ""
    // 1 - local gpio,             gpio num
    // 2 - http send,           url      // как узнавать текущее состояние?
    // 3 - mqtt publish,        url      // подписаться на топик
    httpd_resp_sendstr_chunk(req, html_page_config_buttons);
    
    for ( uint8_t i = 0; i < USR_BUTTON_MAX; i++)
    {
        user_print_button_config(req, i);
        //if ( i != USR_BUTTON_MAX-1 )
        //    httpd_resp_sendstr_chunk(req, "<hr>");
    }

    httpd_resp_sendstr_chunk_fmt(req, html_block_data_form_submit, USER_GET_PARAM1_TAG);
    httpd_resp_sendstr_chunk(req, html_block_data_form_end);
    //httpd_resp_sendstr_chunk(req, html_block_data_end);

}


void user_process_param(httpd_req_t *req, void *args)
{
	if ( http_get_has_params(req) != ESP_OK) 
    {
        ESP_LOGW(TAG, "no http-get params");
        return;
    }

	char param[BUTTON_VALUE_STRING_SIZE];
    
    if ( http_get_key_str(req, "st", param, sizeof(param)) != ESP_OK ) 
    {
        ESP_LOGW(TAG, "no get params \"st\" received");
        return;
    }

    if ( strcmp(param, USER_GET_PARAM1_TAG) != 0 )
    {
        ESP_LOGW(TAG, "another get params \"st\" received");
        return;
    }

    //#ifdef USER_CONFIG_RELAY_GPIO
    //http_get_key_uint8_def(req, html_page_config_gpio_relay_name, &relay_gpio, SONOFF_LIGHT_RELAY_GPIO);
    //#endif

    http_get_key_uint8_def(req, html_page_config_gpio_button_name, &button_gpio, SONOFF_LIGHT_BUTTON_GPIO);
    
    relay_save = ( http_get_key_str(req, html_page_config_gpio_relay_save_name, param, 20) == ESP_OK );
    
    http_get_key_uint8_def(req, html_page_config_duty_night_name, &duty_night, DUTY_NIGHT);
    http_get_key_uint8_def(req, html_page_config_duty_day_name, &duty_day, DUTY_DAY);
    http_get_key_uint16_def(req, html_page_config_night_start_name, &dark_start, TIME_START_NIGHT);
    http_get_key_uint16_def(req, html_page_config_night_stop_name, &dark_stop, TIME_STOP_NIGHT);

    memset(rctopic, 0, RC_TOPIC_LENGTH);
    memset(param, 0, RC_TOPIC_LENGTH);
    http_get_key_str(req, html_page_config_rctopic_name, param, RC_TOPIC_LENGTH);

    mqtt_del_receive_callback(rctopic, 0, rcdata_recv_cb, NULL);
    url_decode(param, rctopic);
    mqtt_add_receive_callback(rctopic, 0, rcdata_recv_cb, NULL);

    ESP_LOGW(TAG, "rctopic = %s", rctopic);

    memset(param, 0, RC_DATA_LEN*RC_DATA_COUNT);
    if ( http_get_key_str(req, html_page_config_rcdata_name, param, RC_DATA_LEN*RC_DATA_COUNT) == ESP_OK )
    {
        
        char srcdata[sizeof(rcdata_t)*RC_DATA_COUNT + RC_DATA_COUNT];
        url_decode(param, srcdata);
        ESP_LOGW(TAG, "s_rcdata = %s", srcdata);
        memset(&rcdata, 0, sizeof(rcdata_t)*RC_DATA_COUNT);
        char *e = malloc(1);
        uint8_t k = 0;
        while ( e != NULL )
        {
            e = cut_str_from_str(srcdata, ";");
            ESP_LOGW(TAG, "split rcdata: %d, e = %s, srcdata = %s, len = %d", k, e, srcdata, strlen(srcdata));
            if ( e == NULL && strlen(srcdata) == 0) break;
            strcpy(rcdata[k].rccode, e);
            k++;
        }        
        free(e);
    }

ESP_LOGW(TAG, "relay_save = %d", relay_save);
    //#ifdef USER_CONFIG_LED_GPIO
    //http_get_key_uint8_def(req, html_page_config_gpio_led_name, &blue_led_gpio, SONOFF_LIGHT_BLUE_LED_GPIO);
    //#endif

    // relpin=12&btnpin=4&ledpin=13&sel0=1&selval1=12&sel1=1&selval2=13&sel2=0&selval3=%3Cnull%3E&sel3=1&selval4=12&seldel4=1&sel4=1&selval5=13&seldel5=2&sel5=0&selval6=%3Cnull%3E&seldel6=0&st=usr1
    // sel0=1&
    // selval1=12&
    // sel1=1&
    // selval2=13&
    // sel2=0&
    // selval3=%3Cnull%3E&
    // sel3=1&
    // selval4=12&
    // seldel4=1&
    // sel4=1&
    // selval5=13
    // &seldel5=2&
    // sel5=0&
    // selval6=%3Cnull%3E&
    // seldel6=0&
    // st=usr1
    for ( uint8_t i = 0; i < USR_BUTTON_MAX; i++ ) 
    {
        char param_name[8];
        sprintf(param_name, USER_SELECT, i);
        http_get_key_uint8_def(req, param_name, &button_press_config[i].action, 0);

        ESP_LOGW(TAG, "received select item = %s with value %d", param_name, button_press_config[i].action);

        sprintf(param_name, html_page_config_button_value_name, i+1);
        uint8_t sz = button_press_config[i].action <= USR_BUTTON_ACTION_LOCAL_GPIO ? BUTTON_VALUE_INT_SIZE : BUTTON_VALUE_STRING_SIZE;
        button_press_config[i].value = realloc(button_press_config[i].value, sz);
        
        if ( button_press_config[i].action > USR_BUTTON_ACTION_LOCAL_GPIO /*USR_BUTTON_ACTION_NONE*/) // для LocalGPIO не используем возможность изменить gpio
        {
            char buf[100];
            http_get_key_str(req, param_name, buf, 100);         
            url_decode(buf, button_press_config[i].value);
            ESP_LOGW(TAG, "received item %s with value %s", param_name, button_press_config[i].value);
        }
        else 
        {
            memset(&button_press_config[i].value, 0, BUTTON_VALUE_INT_SIZE);
        }        
            
        if ( button_press_config[i].type >= USR_BUTTON_LONG_PRESS_1 )
        {
            sprintf(param_name, html_page_config_button_delay_name, i+1);   
            http_get_key_uint8_def(req, param_name, &button_press_config[i].delay, 0);

            ESP_LOGW(TAG, "received item %s with value %d", param_name, button_press_config[i].delay);
        }
    }
                
    user_save_nvs();    
}
#endif

void IRAM_ATTR button_press_handler(void *args)
{
    button_press_type_t *btn = (button_press_type_t *)args;
    ESP_LOGW(TAG, LOG_FMT("type: %d, action: %d, value: %s"), btn->type, btn->action, btn->value );

    switch (btn->action)
    {
        case USR_BUTTON_ACTION_LOCAL_GPIO:
        {
            switch_local_gpio();
            break;
        }
        
    #ifdef USER_HTTP_CLIENT
        case USR_BUTTON_ACTION_SEND_HTTP_GET:
        {
                esp_http_client_config_t config = {};
                config.url = strdup( btn->value);
                esp_http_client_handle_t client = esp_http_client_init(&config);
                esp_http_client_perform(client);
                esp_http_client_cleanup(client);
            break;            
        }
    #endif

        case USR_BUTTON_ACTION_PUBLISH_MQTT:
            mqtt_publish_external(btn->value, "2");
            break;     
        default:
            break;       
    }
}

void user_load_nvs()
{
    //#ifdef USER_CONFIG_RELAY_GPIO
    //nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_relay_name, &relay_gpio, SONOFF_LIGHT_RELAY_GPIO);
    //#endif

    nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_button_name, &button_gpio, SONOFF_LIGHT_BUTTON_GPIO);
    nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_relay_save_name, &relay_save, 0);
    
    nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_duty_night_name, &duty_night, DUTY_NIGHT);
    nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_duty_day_name, &duty_day, DUTY_DAY);
    nvs_param_u16_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_night_start_name, &dark_start, TIME_START_NIGHT);
    nvs_param_u16_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_night_stop_name, &dark_stop, TIME_STOP_NIGHT);
    
    nvs_param_str_load(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_rctopic_name, &rctopic);
    ESP_LOGW(TAG, "loaded rctopic = %s", rctopic);

    nvs_param_load(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_rcdata_name, &rcdata);
    
    for ( uint8_t i = 0; i < RC_DATA_COUNT; i++ )
    {
        ESP_LOGW(TAG, "loaded rcdata[%d] = %s", i, rcdata[i].rccode);
    }

    if ( relay_save ) {
        nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_relay_state_name, &relay_state, 0);
    }
    //#ifdef USER_CONFIG_LED_GPIO
    //nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_led_name, &blue_led_gpio, SONOFF_LIGHT_BLUE_LED_GPIO);
    //#endif

    for ( uint8_t i = 0; i < USR_BUTTON_MAX; i++ )
    {
        char param_name[8];
        sprintf(param_name, USER_SELECT, i);
        nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, param_name, &button_press_config[i].action, 0);
        ESP_LOGW(TAG, "loaded %s  = %d", param_name, button_press_config[i].action);

        uint8_t sz = button_press_config[i].action <= USR_BUTTON_ACTION_LOCAL_GPIO ? BUTTON_VALUE_INT_SIZE : BUTTON_VALUE_STRING_SIZE;
        button_press_config[i].value = realloc(button_press_config[i].value, sz);

        if ( button_press_config[i].action > USR_BUTTON_ACTION_LOCAL_GPIO /*USR_BUTTON_ACTION_NONE*/ )
        {
            sprintf(param_name, html_page_config_button_value_name, i+1);
            nvs_param_str_load(USER_PARAM_SONOFF_LIGHT_SECTION, param_name, button_press_config[i].value);

            ESP_LOGW(TAG, "loaded %s  = %s", param_name, button_press_config[i].value);

            if ( button_press_config[i].type >= USR_BUTTON_LONG_PRESS_1 )
            {
                sprintf(param_name, html_page_config_button_delay_name, i+1);
                nvs_param_u8_load_def(USER_PARAM_SONOFF_LIGHT_SECTION, param_name, &button_press_config[i].delay, 0);
                ESP_LOGW(TAG, "loaded %s  = %d", param_name, button_press_config[i].delay);
            }  
        }
        else
        {
            memset(&button_press_config[i].value, 0, BUTTON_VALUE_INT_SIZE);
        }

    }
}

void user_save_nvs()
{
    ESP_LOGW(TAG, LOG_FMT());
    //#ifdef USER_CONFIG_RELAY_GPIO
    //nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_relay_name, relay_gpio);
    //#endif

    ESP_LOGW(TAG, LOG_FMT("save %s"), html_page_config_gpio_button_name);
    nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_button_name, button_gpio);

    ESP_LOGW(TAG, LOG_FMT("save %s"), html_page_config_gpio_relay_save_name);
    nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_relay_save_name, relay_save);
    
    nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_duty_night_name, duty_night);
    nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_duty_day_name, duty_day);
    nvs_param_u16_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_night_start_name, dark_start);
    nvs_param_u16_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_night_stop_name, dark_stop);
        
    esp_err_t err = nvs_param_str_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_rctopic_name, rctopic);
    ESP_LOGW(TAG, "save rctopic (%s), err = %d", rctopic, err);
    nvs_param_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_rcdata_name, rcdata, sizeof(rcdata_t) * RC_DATA_COUNT);
    //#ifdef USER_CONFIG_LED_GPIO
    //nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, html_page_config_gpio_led_name, blue_led_gpio);
    //#endif

    for ( uint8_t i = 0; i < USR_BUTTON_MAX; i++ )
    {
        char param_name[8];
        sprintf(param_name, USER_SELECT, i);

        ESP_LOGW(TAG, LOG_FMT("save %s"), param_name);
        nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, param_name, button_press_config[i].action);

        if ( button_press_config[i].action > USR_BUTTON_ACTION_LOCAL_GPIO )
        {
            sprintf(param_name, html_page_config_button_value_name, i+1);
            ESP_LOGW(TAG, LOG_FMT("save %s"), param_name);
            nvs_param_str_save(USER_PARAM_SONOFF_LIGHT_SECTION, param_name, button_press_config[i].value);
        }

        //seldel0
        if ( button_press_config[i].type >= USR_BUTTON_LONG_PRESS_1 )
        {
            sprintf(param_name, html_page_config_button_delay_name, i+1);
            ESP_LOGW(TAG, LOG_FMT("save %s"), param_name);
            nvs_param_u8_save(USER_PARAM_SONOFF_LIGHT_SECTION, param_name, button_press_config[i].delay);
        }
                
    }
}
