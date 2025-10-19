/*
 * SPDX-FileCopyrightText: 2010-2025 Cleber Dilenes
 *
 * SPDX-License-Identifier: CC0-1.0
 * 
 * Descrição: Sistema multitarefa com FreeRTOS no ESP32
 * Funções: geração, recepção e supervisão de dados com WDT, fila e eventos
 * Reset do contador de timeout após reinício ou após tratamento, garantindo previsibilidade.
 * Capacidade da fila para 10 elementos, para que a Task1 não descarte dados com frequência.
 * Divisão em módulos: Task1: Geração de dados // Task2: Recepção de dados // Task3: Supervisão
 * Tratamento de erros
 * Watchdog Timer funcionando (WDT)
 * Comunicação por fila e eventos
 * Impressões detalhadas e formatadas = {Cleber Dilenes - RM: 89056} [FILA] Dado enviado com sucesso!

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
// Configuração do Watchdog Timer (WDT)
#define WDT_TIMEOUT_MS 5000 // Tempo limite de 5 segundos para o WDT

// ==========================================
// Declaração da fila e do grupo de eventos
QueueHandle_t fila = NULL;                // Fila para comunicação entre tasks
EventGroupHandle_t event_supervisor = NULL; // Grupo de eventos para sinalizar o status das tasks

// Bits de status para o EventGroup
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
    int value = 0; // Valor inteiro crescente

    esp_task_wdt_add(NULL); // Adiciona esta task ao WDT

    while(1)
    {
        // Tenta enviar o valor para a fila sem bloqueio
        if(xQueueSend(fila, &value, 0) != pdTRUE)
        {
            // Fila cheia, valor descartado
            printf("{Cleber Dilenes - RM: 89056} [FILA CHEIA] Não foi possível enviar valor %d\n", value);
            xEventGroupSetBits(event_supervisor, BIT_TASK1_FAIL); // Sinaliza falha
        }
        else
        {
            // Valor enviado com sucesso
            printf("{Cleber Dilenes - RM:89056} [FILA OK] Valor %d enviado para a fila\n", value);
            xEventGroupSetBits(event_supervisor, BIT_TASK1_OK); // Sinaliza sucesso
        }

        value++; // Incrementa o valor
        esp_task_wdt_reset(); // Reseta o WDT
        vTaskDelay(pdMS_TO_TICKS(1000)); // Aguarda 1 segundo
    }
}

// ==========================================
// Task2: Recepção de dados
void Task2(void *pv)
{
    int timeout = 0; // Contador para detectar falhas

    esp_task_wdt_add(NULL); // Adiciona a task ao WDT

    while(1)
    {
        int *ptr = malloc(sizeof(int)); // Aloca memória dinamicamente
        if(ptr == NULL)
        {
            // Falha na alocação
            printf("{Cleber Dilenes - RM:89056} [ERROR] Falha ao alocar memória\n");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // Tenta receber um dado da fila
        if(xQueueReceive(fila, ptr, 0) == pdTRUE)
        {
            timeout = 0; // Reseta contador de falhas
            printf("{Cleber Dilenes - RM:89056} [FILA OK] Recebeu valor %d\n", *ptr);
            xEventGroupSetBits(event_supervisor, BIT_TASK2_OK); // Sinaliza sucesso
        }
        else
        {
            timeout++; // Incrementa falha consecutiva

            if(timeout == 10)
            {
                // Primeiro nível de falha (leve)
                printf("{Cleber Dilenes - RM:89056} [TIMEOUT] Recuperação leve - Espera\n");
                xEventGroupSetBits(event_supervisor, BIT_TASK2_TIMEOUT);
            }
            else if(timeout == 20)
            {
                // Segundo nível (reset da fila)
                printf("{Cleber Dilenes - RM:89056} [TIMEOUT] Recuperação moderada - Limpa fila\n");
                xQueueReset(fila); // Limpa a fila
                xEventGroupSetBits(event_supervisor, BIT_TASK2_RESET);
                timeout = 0; // Reinicia o contador
            }
            else if(timeout == 30)
            {
                // Terceiro nível: reinicia o sistema
                printf("{Cleber Dilenes - RM:89056} [TIMEOUT] Recuperação agressiva - Reiniciar o sistema\n");
                xEventGroupSetBits(event_supervisor, BIT_TASK2_RESTART);
                free(ptr);
                vTaskDelay(pdMS_TO_TICKS(100)); // Espera um pouco
                esp_restart(); // Reinicia o ESP32
            }
        }

        free(ptr); // Libera a memória
        esp_task_wdt_reset(); // Reseta o WDT
        vTaskDelay(pdMS_TO_TICKS(500)); // Aguarda meio segundo
    }
}

// ==========================================
// Task3: Supervisão
void Task3(void *pv)
{
    esp_task_wdt_add(NULL); // Adiciona esta task ao WDT

    while(1)
    {
        // Aguarda qualquer evento disparado pelas outras tasks
        EventBits_t bits = xEventGroupWaitBits(
            event_supervisor,
            BIT_TASK1_OK | BIT_TASK1_FAIL |
            BIT_TASK2_OK | BIT_TASK2_TIMEOUT | BIT_TASK2_RESET | BIT_TASK2_RESTART,
            pdTRUE,  // Limpa os bits após leitura
            pdFALSE, // Qualquer bit é suficiente
            pdMS_TO_TICKS(0)
        );

        // Verifica e exibe os eventos recebidos
        if(bits & BIT_TASK1_OK)
            printf("{Cleber Dilenes - RM:89056} [SUPERVISOR] Task1 OK\n");
        if(bits & BIT_TASK1_FAIL)
            printf("{Cleber Dilenes - RM:89056} [SUPERVISOR] Task1 falhou no envio\n");
        if(bits & BIT_TASK2_OK)
            printf("{Cleber Dilenes - RM:89056} [SUPERVISOR] Task2 OK\n");
        if(bits & BIT_TASK2_TIMEOUT)
            printf("{Cleber Dilenes - RM:89056} [SUPERVISOR] Task2 em timeout leve\n");
        if(bits & BIT_TASK2_RESET)
            printf("{Cleber Dilenes - RM:89056} [SUPERVISOR] Task2 resetou a fila\n");
        if(bits & BIT_TASK2_RESTART)
            printf("{Cleber Dilenes - RM:89056} [SUPERVISOR] Task2 reiniciou o sistema\n");

        esp_task_wdt_reset(); // Reseta o WDT
        vTaskDelay(pdMS_TO_TICKS(2000)); // Aguarda 2 segundos
    }
}

// ==========================================
// Task4: Logger do sistema (informações do chip)
void Task4(void *pv)
{
    esp_task_wdt_add(NULL); // Adiciona esta task ao WDT

    while(1)
    {
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info); // Obtém informações do chip

        // Imprime informações de status
        printf("{Cleber Dilenes - RM:89056} [LOGGER] Estado do sistema:\n");
        printf("   - Cores: %d, Revisão: %d\n", chip_info.cores, chip_info.revision);
        printf("   - Heap livre: %ld bytes\n", esp_get_free_heap_size());

        esp_task_wdt_reset(); // Reseta o WDT
        vTaskDelay(pdMS_TO_TICKS(3000)); // Aguarda 3 segundos
    }
}

// ==========================================
// Função principal (app_main)
void app_main(void)
{
    // Configuração do Watchdog Timer global
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS,            // Tempo de timeout (5s)
        .idle_core_mask = (1 << 0) | (1 << 1),    // Monitorar ambos os núcleos
        .trigger_panic = true                     // Disparar panic se travar
    };
    esp_task_wdt_init(&wdt_config); // Inicializa o WDT

    // Criação da fila (10 posições) e EventGroup
    fila = xQueueCreate(10, sizeof(int));
    event_supervisor = xEventGroupCreate();

    // Verifica falha na criação de fila ou grupo de eventos
    if(fila == NULL || event_supervisor == NULL)
    {
        printf("{Cleber Dilenes - RM:89056} [ERROR] Falha na criação da fila ou EventGroup\n");
        esp_restart(); // Reinicia o sistema se falhar
    }

    // Criação das tarefas do sistema
    xTaskCreate(Task1, "Task1", 8192, NULL, 5, NULL);
    xTaskCreate(Task2, "Task2", 8192, NULL, 5, NULL);
    xTaskCreate(Task3, "Task3", 8192, NULL, 5, NULL);
    xTaskCreate(Task4, "Task4", 8192, NULL, 5, NULL);
}
