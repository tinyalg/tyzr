#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "M5Unified.h"

#ifdef CONFIG_TYZR_DEBUG_GPIO
#define DEBUG_TIMER_GPIO CONFIG_TYZR_DEBUG_TIMER_GPIO_NUM
#define DEBUG_TIMER_GPIO_SET_LEVEL(level) ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)DEBUG_TIMER_GPIO, (uint32_t)(level)))
#define DEBUG_MAIN_GPIO CONFIG_TYZR_DEBUG_MAIN_GPIO_NUM
#define DEBUG_MAIN_GPIO_SET_LEVEL(level) ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)DEBUG_MAIN_GPIO, (uint32_t)(level)))
#define DEBUG_DISP_GPIO CONFIG_TYZR_DEBUG_DISP_GPIO_NUM
#define DEBUG_DISP_GPIO_SET_LEVEL(level) ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)DEBUG_DISP_GPIO, (uint32_t)(level)))
#else
#define DEBUG_TIMER_GPIO_SET_LEVEL(level) ((void)0) // No-op
#define DEBUG_MAIN_GPIO_SET_LEVEL(level) ((void)0) // No-op
#define DEBUG_DISP_GPIO_SET_LEVEL(level) ((void)0) // No-op
#endif

static const char* TAG = "Tyzr";

constexpr int idleTimeout = 60 * CONFIG_TYZR_IDLE_TIMEOUT;
const int timeWorkPhase = 60 * CONFIG_TYZR_TIME_WORK_PHASE;
const int timeBreakPhase = 60 * CONFIG_TYZR_TIME_BREAK_PHASE;
const int displayBrightness = CONFIG_TYZR_DISPLAY_BRIGHTNESS;
constexpr int screenRotation = CONFIG_TYZR_SCREEN_ROTATION;
constexpr int speakerVolume = CONFIG_TYZR_SPEAKER_VOLUME;
constexpr int digitsPositionX = CONFIG_TYZR_DIGITS_POSITION_X;
constexpr int digitsPositionY = CONFIG_TYZR_DIGITS_POSITION_Y;
constexpr int digitsScalingFactor = CONFIG_TYZR_DIGITS_SCALING_FACTOR;
#ifdef CONFIG_TYZR_DISABLE_ALARM_AFTER_BREAK
constexpr int disableAlarmAfterBreak = true;
#else
constexpr int disableAlarmAfterBreak = false;
#endif
RTC_DATA_ATTR volatile atomic_int timerCount; // Save remaining time in RTC memory
RTC_DATA_ATTR volatile atomic_int idleCounter = 0; // Tracks inactivity
RTC_DATA_ATTR bool isWorkPhase = true; // Save phase (work/break) in RTC memory
RTC_DATA_ATTR bool timerRunning = false; // Save running state in RTC memory

volatile bool prepareForSleep = false; // Signal for tasks to terminate

SemaphoreHandle_t displaySemaphore = nullptr;

// Timer handle
esp_timer_handle_t periodic_timer = nullptr;

// Timer callback
void onTimer(void *args) {
    DEBUG_TIMER_GPIO_SET_LEVEL(1);
    int currentCount = atomic_load(&timerCount); // Atomic read
    if (timerRunning && currentCount > 0) {
        atomic_fetch_sub(&timerCount, 1); // Atomic decrement
        
        // Signal the display update task.
        xSemaphoreGive(displaySemaphore);
    }
    DEBUG_TIMER_GPIO_SET_LEVEL(0);
}

// Configures GPIO pins used by the application, such as debug pins and the SPK HAT.
void setupGpio() {
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;

    uint64_t pin_bit_mask = 0;
#ifdef CONFIG_TYZR_DEBUG_GPIO
    pin_bit_mask |= 1ULL<<CONFIG_TYZR_DEBUG_TIMER_GPIO_NUM;
    pin_bit_mask |= 1ULL<<CONFIG_TYZR_DEBUG_MAIN_GPIO_NUM;
    pin_bit_mask |= 1ULL<<CONFIG_TYZR_DEBUG_DISP_GPIO_NUM;
#endif
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = pin_bit_mask;
    //disable pull-down mode
    io_conf.pull_down_en = (gpio_pulldown_t)0;
    //disable pull-up mode
    io_conf.pull_up_en = (gpio_pullup_t)0;

    if (pin_bit_mask != 0) {
        //configure GPIO with the given settings
        gpio_config(&io_conf);
    }
}

// Configures RTC GPIO pins used by the SPK HAT.
void setupRtcGpio() {
#ifdef CONFIG_TYZR_USE_SPK_HAT
#define SPK_HAT_SD_PIN ((gpio_num_t)0)
    // Initialize RTC GPIO
    ESP_ERROR_CHECK(rtc_gpio_init(SPK_HAT_SD_PIN));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(SPK_HAT_SD_PIN, RTC_GPIO_MODE_OUTPUT_ONLY));
    ESP_LOGI(TAG, "Initialized G0 for use with SPK HAT.");
#endif
}

// Updates the LCD to display the current timer count and mode (work/break).
void updateDisplay() {
    DEBUG_DISP_GPIO_SET_LEVEL(1);

    M5.Lcd.setTextFont(7);
    M5.Lcd.setCursor(digitsPositionX, digitsPositionY);
    M5.Lcd.setTextSize(digitsScalingFactor);

    // Set text color based on the current phase
    if (isWorkPhase) {
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK); // White text for work mode
    } else {
        M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK); // Green text for break mode
    }

    int currentCount = atomic_load(&timerCount); // Atomic read
    M5.Lcd.printf("%02d:%02d", currentCount / 60, currentCount % 60);

    DEBUG_DISP_GPIO_SET_LEVEL(0);
}

// Task to update the display whenever triggered by the semaphore.
void displayUpdateTask(void *args) {
    while (true) {
        // Wait for a semaphore signal
        if (xSemaphoreTake(displaySemaphore, portMAX_DELAY)) {
            if (prepareForSleep) {
                break; // Exit the loop if preparing for sleep
            }
            updateDisplay();
        }
    }

    // Clean up resources before exiting the task
    if (displaySemaphore != NULL) {
        vSemaphoreDelete(displaySemaphore);
        displaySemaphore = NULL;
    }
    ESP_LOGI(TAG, "displayUpdateTask() exiting...");
    vTaskDelete(NULL); // Self-terminate
}

// Plays an alarm sound using the speaker to signal the end of a phase.
void playAlarm() {
    M5.Speaker.tone(1000, 500); // 1000Hz for 500ms
    vTaskDelay(pdMS_TO_TICKS(500));
    M5.Speaker.tone(1500, 500);
    M5.Speaker.tone(0); // Stop the tone
}

// Enters deep sleep mode, preparing the device for low-power operation.
void enterDeepSleep() {
    // Stop and delete the timer
    if (periodic_timer != NULL) {
        ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
        ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
    }
    
    prepareForSleep = true; // Signal tasks to terminate
    xSemaphoreGive(displaySemaphore);

    M5.Lcd.sleep();        // Put the LCD to sleep
    M5.Speaker.end();
#ifdef CONFIG_TYZR_USE_SPK_HAT
    ESP_ERROR_CHECK(rtc_gpio_set_level(SPK_HAT_SD_PIN, (uint32_t)0)); // Disable amplifier
#endif
    
    ESP_LOGI(TAG, "Entering deep sleep mode.");
    esp_sleep_enable_ext0_wakeup((gpio_num_t)CONFIG_TYZR_WAKEUP_SOURCE_GPIO_NUM, 0); // Wake up on button press (BtnB)
    esp_deep_sleep_start();
}

// Handles button events and timer logic, such as start/stop, reset, and deep sleep after timeout.
void handleEvents() {
    DEBUG_MAIN_GPIO_SET_LEVEL(1);
    M5.update();

    // Button A toggles the timer on/off
    if (M5.BtnA.wasPressed()) {
        timerRunning = !timerRunning;
        atomic_store(&idleCounter, 0); // Reset idle counter

        if (!timerRunning) {
            vTaskDelay(pdMS_TO_TICKS(1000 * CONFIG_TYZR_IDLE_TIMEOUT_WHEN_TIMER_STOPPED));
            enterDeepSleep(); // Enter deep sleep when the timer stops
        }
    }

    // Button B resets the timer
    if (M5.BtnB.wasPressed()) {
        timerRunning = false;
        atomic_store(&idleCounter, 0); // Reset idle counter
        atomic_store(&timerCount, isWorkPhase ? timeWorkPhase : timeBreakPhase);
        xSemaphoreGive(displaySemaphore); // Signal the display update task
        timerRunning = true;
    }

    // Handle phase change and alarm
    int currentCount = atomic_load(&timerCount); // Atomic read
    if (timerRunning && currentCount == 0) {
        if (isWorkPhase || !disableAlarmAfterBreak) {
            playAlarm();
        }
        isWorkPhase = !isWorkPhase; // Switch phase
        atomic_store(&timerCount, isWorkPhase ? timeWorkPhase : timeBreakPhase);
        xSemaphoreGive(displaySemaphore); // Signal the display update task
    }

    // Enter deep sleep after prolonged inactivity
    int currentIdleCounter = atomic_load(&idleCounter); // Atomic read
    if (currentIdleCounter > idleTimeout) {
        enterDeepSleep();
    }

    DEBUG_MAIN_GPIO_SET_LEVEL(0);
}

// Main application entry point, setting up peripherals, tasks, and timer functionality.
extern "C" void app_main() {
    prepareForSleep = false;
    setupGpio();

    auto cfg = M5.config();
    cfg.internal_imu = false; // Disable unused IMU
    cfg.internal_rtc = false; // Disable unused RTC
#ifdef CONFIG_TYZR_USE_SPK_HAT
    cfg.external_spk = true;
#endif
    M5.begin(cfg);
    M5.Lcd.setRotation(screenRotation);

    // Handle wakeup from deep sleep or fresh boot
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI(TAG, "Woke up from deep sleep (button press)");
        M5.Lcd.wakeup();
        updateDisplay();
    } else {
        ESP_LOGI(TAG, "Fresh boot");
        setupRtcGpio();
        atomic_init(&timerCount, timeWorkPhase); // Initialize timer
        atomic_init(&idleCounter, 0);
        M5.Lcd.setBrightness(displayBrightness);
        updateDisplay();
    }

#ifdef CONFIG_TYZR_USE_SPK_HAT
    ESP_ERROR_CHECK(rtc_gpio_set_level(SPK_HAT_SD_PIN, (uint32_t)1)); // Enable amplifier
#endif
    M5.Speaker.setVolume(speakerVolume);

    displaySemaphore = xSemaphoreCreateBinary(); // Create the semaphore for the display task
    if (displaySemaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    // Create the display update task
    xTaskCreate(displayUpdateTask, "Display Update Task", 2048, NULL, 1, NULL);

    // Set up and start the periodic timer
    const esp_timer_create_args_t timer_args = {
        .callback = &onTimer,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "Pomodoro Timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&timer_args, &periodic_timer);
    esp_timer_start_periodic(periodic_timer, 1000000); // Run every 1s

    // Main event loop
    while (true) {
        handleEvents();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
