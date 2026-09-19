#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_standard_params.h"
#include "esp_rmaker_standard_devices.h" // <-- ESTE É O FICHEIRO CRÍTICO QUE FALTAVA

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_standard_params.h"
#include "app_wifi.h" // Biblioteca auxiliar do RainMaker para Wi-Fi

#include "driver/gpio.h"



static const char *TAG = "ARQUITETURA_APP";

// Handle da tarefa de controle para receber notificações do RainMaker
TaskHandle_t control_task_handle = NULL;

// Sinais de comando IPC (Inter-Process Communication)
#define CMD_TURN_ON  0x01
#define CMD_TURN_OFF 0x02

#define LED_GPIO     13


// ============================================================
// LED
// ============================================================
static void init_led(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
    ESP_LOGI(TAG, "LED inicializado no GPIO %d", LED_GPIO);
}



/* 
 * 1. CAMADA DE REDE (Callback da Nuvem/Alexa)
 * Regra: NUNCA execute bloqueios ou delays aqui. Apenas sinalize a Task.
 */
esp_err_t alexa_write_callback(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                               const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (strcmp(esp_rmaker_param_get_name(param), ESP_RMAKER_DEF_POWER_NAME) == 0) {
        bool power_state = val.val.b;
        ESP_LOGI(TAG, "Comando recebido da Nuvem: %s", power_state ? "LIGAR" : "DESLIGAR");
        
        // Dispara o sinal para a malha de controle assumir o hardware
        uint32_t cmd = power_state ? CMD_TURN_ON : CMD_TURN_OFF;
        xTaskNotify(control_task_handle, cmd, eSetValueWithOverwrite);
        
        // Confirma o estado para a AWS/Alexa
        esp_rmaker_param_update_and_report(param, val);
    }
    return ESP_OK;
}

/* 
 * 2. CAMADA DE CONTROLE (RTOS Task)
 * Processa o estado físico, aciona GPIOs, PWM, Controladores PID, etc.
 */
void control_loop_task(void *pvParameters)
{
    uint32_t command;
    while (1) {
        // Aguarda ordens sem consumir ciclos de CPU (bloqueio determinístico)
        if (xTaskNotifyWait(0x00, ULONG_MAX, &command, portMAX_DELAY) == pdTRUE) {
            if (command == CMD_TURN_ON) {
                ESP_LOGW(TAG, "Hardware Real: Acionando Relés/Motores...");
                 gpio_set_level(LED_GPIO, 1);
            } else if (command == CMD_TURN_OFF) {
                ESP_LOGW(TAG, "Hardware Real: Desligando sistema em segurança...");
                 gpio_set_level(LED_GPIO, 0);
            }
        }
    }
}

/* 
 * 3. BOOTSTRAP (Main)
 */
void app_main(void)
{
    init_led();
    
    // 1. Inicializa Memória Não Volátil (Obrigatório)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // 2. Cria a Task de Controle ANTES da rede iniciar
    xTaskCreate(control_loop_task, "ctrl_loop", 4096, NULL, configMAX_PRIORITIES - 1, &control_task_handle);

    // 3. Inicializa Conectividade Básica
    app_wifi_init();

    // 4. Configuração do Nó RainMaker (Device Shadow na AWS)
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Hub ESP32-S2", "Automação");

    // 5. Cria o Dispositivo "Interruptor" que a Alexa irá reconhecer
    esp_rmaker_device_t *switch_device = esp_rmaker_switch_device_create("Canal Principal", NULL, false);
    esp_rmaker_device_add_cb(switch_device, alexa_write_callback, NULL);
    esp_rmaker_node_add_device(node, switch_device);

    // 6. Inicia o Serviço de Nuvem e Provisionamento
    esp_rmaker_start();
    app_wifi_start(POP_TYPE_RANDOM);
}