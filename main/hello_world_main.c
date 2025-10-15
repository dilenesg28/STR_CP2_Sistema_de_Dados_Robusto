/*
 * SPDX-FileCopyrightText: 2010-2025 Cleber Dilenes
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_chip_info.h"

// ==========================================
// Configurações WDT
#define WDT_TIMEOUT_MS 5000

// ==========================================
// Fila e EventGroup
QueueHandle_t fila = NULL;
EventGroupHandle_t event_supervisor = NULL;

// Definição dos bits de eventos
#define BIT_TASK1_OK       (1 << 0)
#define BIT_TASK1_FAIL     (1 << 1)
#define BIT_TASK2_OK       (1 << 2)
#define BIT_TASK2_TIMEOUT  (1 << 3)
#define BIT_TASK2_RESET    (1 << 4)
#define BIT_TASK2_RESTART  (1 << 5)

// ==========================================
// Task1: Geração de dados
void Task1(void *pv)
{
    int value = 0;

    // Adiciona esta task ao WDT
    esp_task_wdt_add(NULL);

    while(1)
    {
        if(xQueueSend(fila, &value, 0) != pdTRUE)
        {
            printf("{Cleber Dilenes -RM: 89056} [FILA CHEIA] Não foi possível enviar valor %d\n", value);
            xEventGroupSetBits(event_supervisor, BIT_TASK1_FAIL);
        }
        else
        {
            printf("{Cleber Dilenes -RM: 89056} [FILA OK] Valor %d enviado para a fila\n", value);
            xEventGroupSetBits(event_supervisor, BIT_TASK1_OK);
        }
        value++;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ==========================================
// Task2: Recepção de dados
void Task2(void *pv)
{
    int timeout = 0;

    esp_task_wdt_add(NULL);

    while(1)
    {
        int *ptr = malloc(sizeof(int));
        if(ptr == NULL)
        {
            printf("{Cleber Dilenes -RM: 89056} [ERROR] Falha ao alocar memória\n");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if(xQueueReceive(fila, ptr, 0) == pdTRUE)
        {
            timeout = 0;
            printf("{Cleber Dilenes -RM: 89056} [FILA OK] Recebeu valor %d\n", *ptr);
            xEventGroupSetBits(event_supervisor, BIT_TASK2_OK);
        }
        else
        {
            timeout++;
            if(timeout == 10)
            {
                printf("{Cleber Dilenes -RM: 89056} [TIMEOUT] Recuperação leve - Espera\n");
                xEventGroupSetBits(event_supervisor, BIT_TASK2_TIMEOUT);
            }
            else if(timeout == 20)
            {
                printf("{Cleber Dilenes -RM: 89056} [TIMEOUT] Recuperação moderada - Limpa fila\n");
                xQueueReset(fila);
                xEventGroupSetBits(event_supervisor, BIT_TASK2_RESET);
            }
            else if(timeout == 30)
            {
                printf("{Cleber Dilenes -RM: 89056} [TIMEOUT] Recuperação agressiva - Reiniciar o sistema\n");
                xEventGroupSetBits(event_supervisor, BIT_TASK2_RESTART);
                free(ptr);
                esp_restart();
            }
        }

        free(ptr);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ==========================================
// Task3: Supervisão
void Task3(void *pv)
{
    esp_task_wdt_add(NULL);

    while(1)
    {
        EventBits_t bits = xEventGroupWaitBits(
            event_supervisor,
            BIT_TASK1_OK | BIT_TASK1_FAIL |
            BIT_TASK2_OK | BIT_TASK2_TIMEOUT | BIT_TASK2_RESET | BIT_TASK2_RESTART,
            pdTRUE,  // limpa os bits lidos
            pdFALSE, // espera por qualquer bit
            pdMS_TO_TICKS(0)
        );

        if(bits & BIT_TASK1_OK)
            printf("{Cleber Dilenes -RM: 89056} [SUPERVISOR] Task1 OK\n");
        if(bits & BIT_TASK1_FAIL)
            printf("{Cleber Dilenes -RM: 89056} [SUPERVISOR] Task1 falhou no envio\n");
        if(bits & BIT_TASK2_OK)
            printf("{Cleber Dilenes -RM: 89056} [SUPERVISOR] Task2 OK\n");
        if(bits & BIT_TASK2_TIMEOUT)
            printf("{Cleber Dilenes -RM: 89056} [SUPERVISOR] Task2 em timeout leve\n");
        if(bits & BIT_TASK2_RESET)
            printf("{Cleber Dilenes -RM: 89056} [SUPERVISOR] Task2 resetou a fila\n");
        if(bits & BIT_TASK2_RESTART)
            printf("{Cleber Dilenes -RM: 89056} [SUPERVISOR] Task2 reiniciou o sistema\n");

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ==========================================
// Task4: Logger de sistema
void Task4(void *pv)
{
    esp_task_wdt_add(NULL);

    while(1)
    {
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);

        printf("{Cleber Dilenes -RM: 89056} [LOGGER] Estado do sistema:\n");
        printf("   - Cores: %d, Revisão: %d\n", chip_info.cores, chip_info.revision);
        printf("   - Heap livre: %ld bytes\n", esp_get_free_heap_size());

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// ==========================================
// Função principal
void app_main(void)
{
    // Configuração WDT
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);

    // Criar fila e EventGroup
    fila = xQueueCreate(1, sizeof(int));
    event_supervisor = xEventGroupCreate();

    if(fila == NULL || event_supervisor == NULL)
    {
        printf("{Cleber Dilenes -RM: 89056} [ERROR] Falha na criação da fila ou EventGroup\n");
        esp_restart();
    }

    // Criar tasks
    xTaskCreate(Task1, "Task1", 8192, NULL, 5, NULL);
    xTaskCreate(Task2, "Task2", 8192, NULL, 5, NULL);
    xTaskCreate(Task3, "Task3", 8192, NULL, 5, NULL);
    xTaskCreate(Task4, "Task4", 8192, NULL, 5, NULL);
}
