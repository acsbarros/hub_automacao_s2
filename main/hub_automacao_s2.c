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


static const char *TAG = "ARQUITETURA_MULTI_CANAL";

// 1. Definição do Hardware Físico (Mapeamento de Pinos)
#define NUM_CANAIS 4
const uint8_t pinos_saida[NUM_CANAIS] = {12, 13, 14, 21}; // GPIOs do ESP32-S2

// 2. Estrutura de Mensagem para o FreeRTOS
typedef struct {
    uint8_t gpio_num;
    bool estado;
} comando_hw_t;

// Fila para comunicar a Nuvem com o Hardware
QueueHandle_t fila_comandos = NULL;

/* 
 * CAMADA DE REDE: Único Callback para todos os pinos.
 * Como ele sabe qual pino acionar? Lendo o ponteiro priv_data!
 */
esp_err_t alexa_write_callback(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                               const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (strcmp(esp_rmaker_param_get_name(param), ESP_RMAKER_DEF_POWER_NAME) == 0) {
        
        // Extrai o pino associado a este dispositivo virtual
        uint8_t pino_alvo = (uint8_t)(uint32_t)priv_data;
        bool ligar = val.val.b;
        
        ESP_LOGI(TAG, "Comando da Alexa -> Pino: %d, Estado: %d", pino_alvo, ligar);
        
        // Embala a mensagem e despacha para a fila
        comando_hw_t msg = { .gpio_num = pino_alvo, .estado = ligar };
        xQueueSend(fila_comandos, &msg, portMAX_DELAY);
        
        // Confirma o recebimento para a AWS
        esp_rmaker_param_update_and_report(param, val);
    }
    return ESP_OK;
}

/* 
 * CAMADA DE CONTROLE: Processa os comandos físicos
 */
void control_loop_task(void *pvParameters)
{
    comando_hw_t comando_recebido;
    
    while (1) {
        // Fica bloqueado aguardando comandos da Nuvem (zero consumo de CPU)
        if (xQueueReceive(fila_comandos, &comando_recebido, portMAX_DELAY) == pdTRUE) {
            ESP_LOGW(TAG, "Acionando Hardware -> GPIO %d para %s", 
                     comando_recebido.gpio_num, comando_recebido.estado ? "HIGH" : "LOW");
                     
            gpio_set_level(comando_recebido.gpio_num, comando_recebido.estado);
        }
    }
}

/* 
 * BOOTSTRAP
 */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    // Inicializa a Fila e a Tarefa do FreeRTOS
    fila_comandos = xQueueCreate(10, sizeof(comando_hw_t));
    xTaskCreate(control_loop_task, "ctrl_loop", 4096, NULL, configMAX_PRIORITIES - 1, NULL);

    // Inicializa os pinos físicos como Saída
    for (int i = 0; i < NUM_CANAIS; i++) {
        gpio_reset_pin(pinos_saida[i]);
        gpio_set_direction(pinos_saida[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pinos_saida[i], 0); // Começa desligado
    }

    app_wifi_init();

    esp_rmaker_config_t rainmaker_cfg = { .enable_time_sync = true };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Hub Multi-Canais", "Automação");

    // Cria os Dispositivos Virtuais dinamicamente
    for (int i = 0; i < NUM_CANAIS; i++) {
        char nome_dispositivo[32];
        sprintf(nome_dispositivo, "Canal %d", i + 1); // Ex: "Canal 1", "Canal 2"...

        // O 'priv_data' é a nossa âncora: passamos o GPIO correspondente para o Callback
        esp_rmaker_device_t *device = esp_rmaker_switch_device_create(nome_dispositivo, 
                                                                     (void *)(uint32_t)pinos_saida[i], 
                                                                     false);
        
        esp_rmaker_device_add_cb(device, alexa_write_callback, NULL);
        esp_rmaker_node_add_device(node, device);
    }

    esp_rmaker_start();
    app_wifi_start(POP_TYPE_MAC); // Usando MAC para produção!
}