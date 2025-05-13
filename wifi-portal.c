#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "hardware/gpio.h"

// Define pins for external LEDs and buttons
#define LED_BLUE_PIN    12    // External blue LED
#define LED_GREEN_PIN   11    // External green LED
#define BUTTON_B_PIN    6     // Button B

// Estrutura para armazenar as credenciais Wi-Fi
typedef struct {
    char ssid[32];
    char password[64];
    bool received;
} wifi_config_t;

wifi_config_t new_wifi_config = {0};

// LED control variables
volatile bool led_blink_enabled = true;
volatile uint8_t led_mode = 0;  // 0=off, 1=AP mode (blue LED), 2=WiFi mode (green LED)
volatile bool led_state = false;
volatile uint32_t last_led_time = 0;

// Variáveis para debounce de botões
volatile absolute_time_t last_button_time;
#define DEBOUNCE_DELAY_MS 200

// Blink intervals for different modes
#define BLINK_INTERVAL_AP 500    // AP mode: blink every 500ms
#define BLINK_INTERVAL_WIFI 200  // WiFi connected: blink every 200ms

// Página HTML do formulário de configuração (with LED control button)
const char* setup_html = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "    <meta charset='UTF-8'>"
    "    <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "    <title>Configuração Wi-Fi - Pico W</title>"
    "    <style>"
    "        body { "
    "            font-family: -apple-system, BlinkMacSystemFont, sans-serif; "
    "            max-width: 500px; "
    "            margin: 40px auto; "
    "            padding: 20px; "
    "            background-color: #f5f5f5; "
    "        }"
    "        .container { "
    "            background: white; "
    "            border-radius: 10px; "
    "            padding: 30px; "
    "            box-shadow: 0 2px 10px rgba(0,0,0,0.1); "
    "        }"
    "        h1 { "
    "            text-align: center; "
    "            color: #333; "
    "            margin-bottom: 30px; "
    "        }"
    "        .form-group { "
    "            margin-bottom: 20px; "
    "        }"
    "        label { "
    "            display: block; "
    "            margin-bottom: 5px; "
    "            color: #555; "
    "            font-weight: bold; "
    "        }"
    "        input[type='text'], input[type='password'] { "
    "            width: 100%; "
    "            padding: 10px; "
    "            border: 1px solid #ddd; "
    "            border-radius: 5px; "
    "            box-sizing: border-box; "
    "            font-size: 16px; "
    "        }"
    "        button { "
    "            width: 100%; "
    "            padding: 12px; "
    "            background-color: #007AFF; "
    "            color: white; "
    "            border: none; "
    "            border-radius: 5px; "
    "            font-size: 16px; "
    "            cursor: pointer; "
    "            transition: background-color 0.3s; "
    "            margin-bottom: 10px; "
    "        }"
    "        button:hover { "
    "            background-color: #0051D5; "
    "        }"
    "        button.secondary { "
    "            background-color: #FF3B30; "
    "        }"
    "        button.secondary:hover { "
    "            background-color: #D50000; "
    "        }"
    "        .info { "
    "            margin-top: 30px; "
    "            text-align: center; "
    "            color: #666; "
    "            font-size: 14px; "
    "        }"
    "    </style>"
    "</head>"
    "<body>"
    "    <div class='container'>"
    "        <h1>Configuração Wi-Fi</h1>"
    "        <form method='POST' action='/save'>"
    "            <div class='form-group'>"
    "                <label for='ssid'>Nome da Rede (SSID):</label>"
    "                <input type='text' id='ssid' name='ssid' required>"
    "            </div>"
    "            <div class='form-group'>"
    "                <label for='password'>Senha:</label>"
    "                <input type='password' id='password' name='password' required>"
    "            </div>"
    "            <button type='submit'>Conectar</button>"
    "        </form>"
    "        <form method='POST' action='/led_off'>"
    "            <button type='submit' class='secondary'>Desligar LED</button>"
    "        </form>"
    "        <div class='info'>"
    "            Após enviar, o dispositivo tentará se conectar<br>"
    "            à rede especificada."
    "        </div>"
    "    </div>"
    "</body>"
    "</html>";

// Página de confirmação após receber os dados
const char* success_html = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "    <meta charset='UTF-8'>"
    "    <title>Configuração Enviada</title>"
    "    <style>"
    "        body { "
    "            font-family: sans-serif; "
    "            text-align: center; "
    "            margin-top: 100px; "
    "        }"
    "        .success { "
    "            color: #4CAF50; "
    "            font-size: 24px; "
    "            margin-bottom: 20px; "
    "        }"
    "    </style>"
    "</head>"
    "<body>"
    "    <div class='success'>✓ Configuração Recebida!</div>"
    "    <p>O dispositivo agora tentará se conectar à sua rede.</p>"
    "    <p>Esta página será fechada automaticamente...</p>"
    "    <script>"
    "        setTimeout(function() { window.close(); }, 5000);"
    "    </script>"
    "</body>"
    "</html>";

// Página de confirmação para LED desligado
const char* led_off_html = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "    <meta charset='UTF-8'>"
    "    <title>LED Desligado</title>"
    "    <style>"
    "        body { "
    "            font-family: sans-serif; "
    "            text-align: center; "
    "            margin-top: 100px; "
    "        }"
    "        .success { "
    "            color: #4CAF50; "
    "            font-size: 24px; "
    "            margin-bottom: 20px; "
    "        }"
    "    </style>"
    "</head>"
    "<body>"
    "    <div class='success'>✓ LED Desligado!</div>"
    "    <p>O LED foi desligado com sucesso.</p>"
    "    <p><a href='/'>Voltar para configuração</a></p>"
    "</body>"
    "</html>";

/**
 * Função de callback para interrupções nos GPIOs
 * Esta função é chamada automaticamente quando um botão é pressionado
 */
void gpio_callback(uint gpio, uint32_t events) {
    absolute_time_t now = get_absolute_time();
    
    // Verifica se é o botão B (com debounce)
    if (gpio == BUTTON_B_PIN) {
        if (absolute_time_diff_us(last_button_time, now) < DEBOUNCE_DELAY_MS * 1000) {
            return;  // Ignora o evento se estiver dentro do período de debounce
        }
        
        last_button_time = now;
        
        // Se o LED estiver habilitado, desliga-o de acordo com o modo atual
        if (led_blink_enabled) {
            led_blink_enabled = false;
            
            if (led_mode == 1) {
                // Desliga o LED azul (modo AP)
                gpio_put(LED_BLUE_PIN, 0);
                printf("LED AZUL desligado pelo botão B (interrupção)\n");
            } else if (led_mode == 2) {
                // Desliga o LED verde (modo WiFi conectado)
                gpio_put(LED_GREEN_PIN, 0);
                printf("LED VERDE desligado pelo botão B (interrupção)\n");
            }
        }
    }
}

/**
 * Função para atualizar o estado dos LEDs externos com base no modo atual
 */
void update_leds() {
    // Se o LED não estiver habilitado para piscar, não faz nada
    if (!led_blink_enabled) {
        return;
    }

    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    uint32_t interval = (led_mode == 1) ? BLINK_INTERVAL_AP : BLINK_INTERVAL_WIFI;
    
    // Verifica se é hora de alternar o estado do LED
    if (current_time - last_led_time >= interval) {
        led_state = !led_state;
        
        // Atualiza o LED apropriado com base no modo atual
        if (led_mode == 1) {
            // Modo AP - Pisca o LED AZUL
            gpio_put(LED_BLUE_PIN, led_state);
        } else if (led_mode == 2) {
            // Modo WiFi Conectado - Pisca o LED VERDE
            gpio_put(LED_GREEN_PIN, led_state);
        }
        
        last_led_time = current_time;
    }
}

// Função para fazer parse dos dados do formulário
void parse_form_data(char* data, wifi_config_t* config) {
    char* token;
    char* temp_data = strdup(data);  // Cria uma cópia para parsing
    
    // Separa por '&' para obter cada campo
    token = strtok(temp_data, "&");
    
    while (token != NULL) {
        char* equal_pos = strchr(token, '=');
        if (equal_pos != NULL) {
            *equal_pos = '\0';  // Divide em nome e valor
            char* name = token;
            char* value = equal_pos + 1;
            
            // URL decode simples (substitui + por espaço)
            char* pos = value;
            while (*pos) {
                if (*pos == '+') *pos = ' ';
                pos++;
            }
            
            // Armazena os valores na estrutura
            if (strcmp(name, "ssid") == 0) {
                strcpy(config->ssid, value);
            } else if (strcmp(name, "password") == 0) {
                strcpy(config->password, value);
            }
        }
        token = strtok(NULL, "&");
    }
    
    free(temp_data);
    config->received = true;
}

// Callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    static bool client_connected = false;
    
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }
    
    // Confirma o recebimento dos dados
    tcp_recved(tpcb, p->len);
    
    // Converte os dados recebidos em string
    char request[1024];
    strncpy(request, (char*)p->payload, p->tot_len < 1024 ? p->tot_len : 1023);
    request[p->tot_len < 1024 ? p->tot_len : 1023] = '\0';
    
    printf("=== Requisição Recebida ===\n%s\n", request);
    
    // Se for a primeira conexão, ativa o LED AZUL no modo AP
    if (!client_connected) {
        client_connected = true;
        led_mode = 1; // Modo AP - LED AZUL
        led_blink_enabled = true;
        
        // Certifica-se de que o LED verde está desligado
        gpio_put(LED_GREEN_PIN, 0);
        
        printf("Cliente conectado ao AP! LED AZUL piscando.\n");
    }
    
    char response[4096];
    
    // Verifica se é uma requisição POST para /save
    if (strncmp(request, "POST /save", 10) == 0) {
        printf("Processando dados do formulário...\n");
        
        // Encontra o corpo da requisição (após duas quebras de linha)
        char* body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;  // Pula as quebras de linha
            parse_form_data(body, &new_wifi_config);
            
            printf("SSID recebido: %s\n", new_wifi_config.ssid);
            printf("Senha recebida: %s\n", new_wifi_config.password);
            
            // Responde com página de sucesso
            sprintf(response,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                strlen(success_html),
                success_html
            );
        }
    }
    // Verifica se é uma requisição para desligar o LED
    else if (strncmp(request, "POST /led_off", 13) == 0) {
        printf("Desligando LEDs via web...\n");
        led_blink_enabled = false;
        
        // Desliga ambos os LEDs
        gpio_put(LED_BLUE_PIN, 0);
        gpio_put(LED_GREEN_PIN, 0);
        
        // Responde com página de confirmação
        sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(led_off_html),
            led_off_html
        );
    }
    else {
        // Responde com o formulário HTML
        sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(setup_html),
            setup_html
        );
    }
    
    // Envia a resposta
    tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    
    // Libera o buffer
    pbuf_free(p);
    
    // Fecha a conexão
    tcp_close(tpcb);
    
    return ERR_OK;
}

// Callback para aceitar novas conexões
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    printf("Nova conexão HTTP estabelecida!\n");
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Função para iniciar o servidor HTTP
struct tcp_pcb* start_http_server(void) {
    struct tcp_pcb *server_pcb = tcp_new();
    if (!server_pcb) {
        printf("Erro ao criar servidor TCP\n");
        return NULL;
    }
    
    // Vincula à porta 80 (HTTP)
    if (tcp_bind(server_pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao vincular porta 80\n");
        tcp_close(server_pcb);
        return NULL;
    }
    
    // Coloca em modo de escuta
    server_pcb = tcp_listen(server_pcb);
    
    // Define o callback para aceitar conexões
    tcp_accept(server_pcb, tcp_server_accept);
    
    return server_pcb;
}

int main() {
    stdio_init_all();
    sleep_ms(3000);  // Aguarda a estabilização do sistema
    
    printf("\n=== PORTAL DE CONFIGURAÇÃO WI-FI ===\n");
    printf("Iniciando sistema...\n");
    
    // Configuração dos LEDs externos
    gpio_init(LED_BLUE_PIN);
    gpio_init(LED_GREEN_PIN);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
    
    // Inicialmente todos os LEDs estão desligados
    gpio_put(LED_BLUE_PIN, 0);
    gpio_put(LED_GREEN_PIN, 0);
    
    // Inicialização da variável de tempo para debounce
    last_button_time = get_absolute_time();
    
    // Configuração do botão B com pull-up e interrupção
    gpio_init(BUTTON_B_PIN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_B_PIN);
    
    // Configura a interrupção para o botão B (na borda de descida - quando pressionado)
    gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    
    // Passo 1: Inicialização do Wi-Fi
    if (cyw43_arch_init()) {
        printf("❌ Erro ao inicializar Wi-Fi\n");
        return -1;
    }
    printf("✓ Sistema Wi-Fi inicializado\n");
    
    // LED onboard inicialmente desligado
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    led_blink_enabled = false;
    
    // Passo 2: Criação do Access Point
    printf("\n=== Fase 1: Modo Access Point ===\n");
    cyw43_arch_enable_ap_mode("PicoW-Setup", "12345678", CYW43_AUTH_WPA2_AES_PSK);
    
    // Configuração de IP para o AP
    ip4_addr_t ip, netmask, gateway;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 4, 1);
    netif_set_addr(netif_default, &ip, &netmask, &gateway);
    
    printf("✓ Access Point criado!\n");
    printf("SSID: PicoW-Setup\n");
    printf("Senha: 12345678\n");
    printf("IP: %s\n", ip4addr_ntoa(&ip));
    
    // Passo 3: Inicia o servidor HTTP
    printf("\n=== Fase 2: Servidor Web ===\n");
    struct tcp_pcb *server = start_http_server();
    if (!server) {
        printf("❌ Erro ao iniciar servidor HTTP\n");
        return -1;
    }
    printf("✓ Servidor HTTP iniciado na porta 80\n");
    printf("Acesse: http://192.168.4.1\n");
    
    printf("\n=== Sistema Pronto ===\n");
    printf("1. Conecte seu dispositivo ao Wi-Fi: PicoW-Setup\n");
    printf("2. Abra o navegador e acesse: http://192.168.4.1\n");
    printf("3. Insira as credenciais da sua rede\n");
    printf("4. Aguarde a conexão...\n\n");
    
    // Loop principal - Aguarda configuração
    while (!new_wifi_config.received) {
        cyw43_arch_poll();  // Processa eventos de rede
        update_leds();      // Atualiza o estado dos LEDs externos
        sleep_ms(10);
    }
    
    printf("\n=== Credenciais Recebidas! ===\n");
    printf("SSID: %s\n", new_wifi_config.ssid);
    printf("Senha: %s\n", new_wifi_config.password);
    
    // Aguarda um pouco para a página de confirmação ser enviada
    sleep_ms(5000);
    
    // Passo 4: Para o servidor e fecha o AP
    printf("\n=== Fase 3: Mudança de Modo ===\n");
    tcp_close(server);
    
    // Desliga o LED azul
    led_blink_enabled = false;
    gpio_put(LED_BLUE_PIN, 0);
    
    // LED onboard pisca durante a transição
    for (int i = 0; i < 10; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, i % 2);
        sleep_ms(200);
    }
    
    // Desativa modo AP
    cyw43_arch_disable_ap_mode();
    
    // Muda para modo cliente
    cyw43_arch_enable_sta_mode();
    
    printf("✓ Modo AP desativado\n");
    printf("✓ Modo cliente ativado\n");
    
    // Passo 5: Conecta à rede especificada
    printf("\n=== Fase 4: Conexão à Nova Rede ===\n");
    printf("Tentando conectar a: %s\n", new_wifi_config.ssid);
    
    int connection_attempts = 0;
    while (cyw43_arch_wifi_connect_timeout_ms(new_wifi_config.ssid, 
                                            new_wifi_config.password, 
                                            CYW43_AUTH_WPA2_AES_PSK, 
                                            15000)) {
        connection_attempts++;
        printf("Tentativa %d falhou. Tentando novamente...\n", connection_attempts);
        
        // LED onboard pisca rapidamente durante tentativas
        for (int i = 0; i < 20; i++) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, i % 2);
            sleep_ms(50);
        }
        
        if (connection_attempts >= 3) {
            printf("\n❌ Não foi possível conectar após %d tentativas\n", connection_attempts);
            printf("Verifique as credenciais e tente novamente\n");
            
            // LED onboard pisca lentamente indicando erro
            while (true) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                sleep_ms(1000);
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                sleep_ms(1000);
            }
        }
    }
    
    // Sucesso! Conexão estabelecida
    printf("\n✓ CONECTADO COM SUCESSO!\n");
    printf("IP obtido: %s\n", ipaddr_ntoa(&cyw43_state.netif[0].ip_addr));
    
    // Configura LED VERDE para piscar quando conectado
    led_mode = 2; // Modo WiFi - LED VERDE
    led_blink_enabled = true;
    last_led_time = to_ms_since_boot(get_absolute_time());
    
    printf("\n=== Sistema Operacional ===\n");
    printf("O Pico W agora está conectado à sua rede!\n");
    printf("LED VERDE piscando indicando conexão. Pressione o botão B para parar.\n");
    
    // Loop principal - Sistema conectado
    while (true) {
        // Atualiza o estado dos LEDs externos
        update_leds();
        
        //LÓGICA DA ATIVIDADE 2
        
        sleep_ms(10);
    }
    
    return 0;
}