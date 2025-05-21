// compile com: PICO_CYW43_ARCH_POLL=1
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
// Novas bibliotecas para os componentes
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"

// Bibliotecas do display OLED 
#include "lib/ssd1306.h"    // Biblioteca do SSD1306
#include "lib/font.h"       // Fonte para o OLED

// Biblioteca para Matriz RGB 
#include "ws2812.pio.h"

// Configurações de rede - AJUSTE CONFORME SUA REDE
#define PC_IP      "192.168.2.110"   // ← ajuste ao IP do computador
#define PC_PORT    8080              // porta em que o PC escuta
#define PICO_PORT  8081              // porta local do Pico

// Configurações dos pinos para joystick analógico
#define ADC_X_PIN  26  // Pino 26 para eixo X do joystick (ADC0)
#define ADC_Y_PIN  27  // Pino 27 para eixo Y do joystick (ADC1)

// Configurações dos botões da BitDogLab
#define BUTTON_CAPTURE 5   // Botão A para capturar pontos verdes
#define BUTTON_LIGHTS  6   // Botão para ligar/desligar luzes
#define BUTTON_CAMERA  22  // Botão para ligar/desligar câmera

// Configurações de ajuste do joystick
#define DEADZONE       0.15f  // Zona morta para eliminar pequenas flutuações
#define MAX_SPEED      80.0f  // Velocidade máxima (0-100)

// Tempo de debounce para os botões (em ms)
#define DEBOUNCE_TIME  100
// OLED via I2C
const uint8_t SDA = 14;
const uint8_t SCL = 15;
#define I2C_ADDR 0x3C
#define I2C_PORT i2c1
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64

// LEDs RGB (PWM)
#define R_LED_PIN 13
#define G_LED_PIN 11
#define B_LED_PIN 12
#define PWM_WRAP 255

// Matriz WS2812
#define NUM_PIXELS 25
#define WS2812_PIN 7
#define IS_RGBW false

// Estados do rover para exibição
#define ESTADO_NORMAL 0
#define ESTADO_CAPTURANDO 1
#define ESTADO_CONECTANDO 2
#define ESTADO_CONFIGURANDO 3  //Estado para fase de configuração Wi-Fi

// ====== ESTRUTURA PARA CONFIGURAÇÃO WI-FI ======
typedef struct {
    char ssid[32];
    char password[64];
    bool received;
} wifi_config_t;

wifi_config_t new_wifi_config = {0};

// Variáveis globais para comunicação UDP
static struct udp_pcb *pcb;
static ip4_addr_t pc_addr;
static uint32_t last_sent;
static bool link_ok = false;
static uint32_t last_rx = 0;
static bool conexao_ok = false;

// Estado do rover
static int rover_mode = 0;           // 0=Manual (fixo)
static bool lights_on = false;
static bool camera_on = false;
static bool capture_active = false;  // Flag para captura de ponto
static uint32_t capture_time = 0;    // Tempo de início da captura

// Variáveis para debounce de botões
static uint32_t last_btn_capture_time = 0;
static uint32_t last_btn_lights_time = 0;
static uint32_t last_btn_camera_time = 0;
// Display OLED
ssd1306_t display;

// Estado do rover (para exibição)
static int rover_estado = ESTADO_NORMAL;
static uint32_t ultima_captura = 0;
static int pontos_capturados = 0;
static int score_atual = 0;

// Buffer para a matriz de LEDs
bool buffer_leds[NUM_PIXELS] = {false};

// Padrões para matriz de LEDs (5x5)
const bool padrao_normal[5][5] = {
    {false, true, false, true, false},
    {true, false, true, false, true},
    {false, true, false, true, false},
    {true, false, true, false, true},
    {false, true, false, true, false}
};

const bool padrao_captura[5][5] = {
    {false, false, true, false, false},
    {false, true, true, true, false},
    {true, true, true, true, true},
    {false, true, true, true, false},
    {false, false, true, false, false}
};

// ====== DECLARAÇÕES DE PROTÓTIPOS DE FUNÇÕES ======
void inicializar_display(void);
void inicializar_led_rgb(void);
void definir_cor_rgb(uint8_t r, uint8_t g, uint8_t b);
void inicializar_matriz_leds(void);
void atualizar_display(void);
void definir_leds(uint8_t r, uint8_t g, uint8_t b);
void atualizar_buffer_matriz(const bool padrao[5][5]);
void ler_joystick(float *x, float *y);
void gpio_callback(uint gpio, uint32_t events);
static void rx_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
void enviar_hello(void);
void enviar_comandos_rover(float joy_x, float joy_y);
void configurar_gpio(void);
bool setup_wifi_portal(void);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
struct tcp_pcb* start_http_server(void);
void parse_form_data(char* data, wifi_config_t* config);

// ====== PÁGINAS HTML DO PORTAL DE CONFIGURAÇÃO ======
// Página HTML do formulário de configuração
const char* setup_html = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "    <meta charset='UTF-8'>"
    "    <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "    <title>Configuração Wi-Fi - Rover</title>"
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
    "        }"
    "        button:hover { "
    "            background-color: #0051D5; "
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
    "        <h1>Configuração Wi-Fi do Rover</h1>"
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
    "        <div class='info'>"
    "            Após enviar, o rover tentará se conectar<br>"
    "            à rede especificada e iniciará sua operação."
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
    "    <p>O rover agora tentará se conectar à sua rede.</p>"
    "    <p>Esta página será fechada automaticamente...</p>"
    "    <script>"
    "        setTimeout(function() { window.close(); }, 5000);"
    "    </script>"
    "</body>"
    "</html>";

// ====== FUNÇÕES DO PORTAL WI-FI ======
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

void inicializar_display() {
    // Inicialização do I2C 
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(SDA, GPIO_FUNC_I2C);
    gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA);
    gpio_pull_up(SCL);
    
    // Inicialização do display usando a biblioteca ssd1306
    ssd1306_init(&display, SSD1306_WIDTH, SSD1306_HEIGHT, false, I2C_ADDR, I2C_PORT);
    ssd1306_config(&display);
    ssd1306_fill(&display, 0);
    
    // Tela de boas-vindas
    ssd1306_draw_string(&display, "Rover Controller", 10, 5);
    ssd1306_draw_string(&display, "BitDogLab", 25, 25);
    ssd1306_draw_string(&display, "Inicializando...", 10, 45);
    ssd1306_send_data(&display);
}

void inicializar_led_rgb() {
    gpio_set_function(R_LED_PIN, GPIO_FUNC_PWM);
    gpio_set_function(B_LED_PIN, GPIO_FUNC_PWM);
    
    uint slice_r = pwm_gpio_to_slice_num(R_LED_PIN);
    uint slice_b = pwm_gpio_to_slice_num(B_LED_PIN);
    
    pwm_set_wrap(slice_r, PWM_WRAP);
    pwm_set_clkdiv(slice_r, 125.0f);
    pwm_set_enabled(slice_r, true);
    
    if (slice_b != slice_r) {
        pwm_set_wrap(slice_b, PWM_WRAP);
        pwm_set_clkdiv(slice_b, 125.0f);
        pwm_set_enabled(slice_b, true);
    }
    
    // LED Verde como saída digital 
    gpio_init(G_LED_PIN);
    gpio_set_dir(G_LED_PIN, GPIO_OUT);
}

void definir_cor_rgb(uint8_t r, uint8_t g, uint8_t b) {
    pwm_set_chan_level(pwm_gpio_to_slice_num(R_LED_PIN), pwm_gpio_to_channel(R_LED_PIN), r);
    gpio_put(G_LED_PIN, g > 10); // Digital on/off baseado na intensidade
    pwm_set_chan_level(pwm_gpio_to_slice_num(B_LED_PIN), pwm_gpio_to_channel(B_LED_PIN), b);
}

// Função auxiliar para formatar cores para a matriz de LEDs
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 8) | ((uint32_t)g << 16) | (uint32_t)b;
}

// Função auxiliar para enviar um pixel para a matriz
static inline void enviar_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

// Define os LEDs da matriz com base no buffer
void definir_leds(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t cor = urgb_u32(r, g, b);
    for (int i = 0; i < NUM_PIXELS; i++) {
        if (buffer_leds[i])
            enviar_pixel(cor);
        else
            enviar_pixel(0);
    }
    sleep_us(60);
}

// Atualiza o buffer com um padrão específico
void atualizar_buffer_matriz(const bool padrao[5][5]) {
    for (int linha = 0; linha < 5; linha++) {
        for (int coluna = 0; coluna < 5; coluna++) {
            int indice = linha * 5 + coluna;
            buffer_leds[indice] = padrao[linha][coluna];
        }
    }
}

void inicializar_matriz_leds() {
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);
    
    // Efeito de inicialização
    for (int i = 0; i < NUM_PIXELS; i++) {
        buffer_leds[i] = true;
        definir_leds(20, 20, 20);
        sleep_ms(20);
    }
    
    for (int i = 0; i < NUM_PIXELS; i++) {
        buffer_leds[i] = false;
        definir_leds(0, 0, 0);
        sleep_ms(20);
    }
    
    // Define padrão inicial
    atualizar_buffer_matriz(padrao_normal);
    definir_leds(0, 0, 30); // Azul
}

void atualizar_display() {
    // Limpa o display
    ssd1306_fill(&display, 0);
    
    // Estado de Configuração Wi-Fi
    if (rover_estado == ESTADO_CONFIGURANDO) {
        ssd1306_draw_string(&display, "Config Wi-Fi", 20, 0);
        ssd1306_draw_string(&display, "Conecte a:", 0, 16);
        ssd1306_draw_string(&display, "Rover-Setup", 10, 28);
        ssd1306_draw_string(&display, "Senha: roverpass", 0, 40);
        ssd1306_draw_string(&display, "Acesse: 192.168.4.1", 0, 52);
    }
    // Estado Normal
    else {
        // Desenha título
        ssd1306_draw_string(&display, "Rover Controller", 5, 0);
        
        // Status da conexão
        if (!conexao_ok) {
            ssd1306_draw_string(&display, "Status: Esperando", 0, 16);
            ssd1306_draw_string(&display, "Conectando...", 10, 28);
        } else {
            ssd1306_draw_string(&display, "Status: Conectado", 0, 16);
            
            char linha_score[32];
            sprintf(linha_score, "Score: %d", score_atual);
            ssd1306_draw_string(&display, linha_score, 10, 28);
            
            char linha_pontos[32];
            sprintf(linha_pontos, "Pontos: %d", pontos_capturados);
            ssd1306_draw_string(&display, linha_pontos, 10, 40);
        }
        
        // Mostra controles na parte inferior
        ssd1306_draw_string(&display, "A: Captura B: Luzes", 0, 52);
    }
    
    // Atualiza o display
    ssd1306_send_data(&display);
}

// Função para ler os valores do joystick analógico
void ler_joystick(float *x, float *y) {
    // Lê ADC para eixo X
    adc_select_input(0); // ADC0
    uint16_t raw_x = adc_read();
    
    // Lê ADC para eixo Y
    adc_select_input(1); // ADC1
    uint16_t raw_y = adc_read();
    
    // Converte para faixa -1.0 a 1.0
    // Assumindo valor de 12 bits (0-4095), onde 2048 é o centro
    *x = ((float)raw_x - 2048.0f) / 2048.0f;
    *y = ((float)raw_y - 2048.0f) / 2048.0f;
    
    // Inverte o eixo Y se necessário (para corresponder à direção esperada)
    // Descomente esta linha se o movimento estiver invertido
    // *y = -*y;
    
    // Aplica zona morta para eliminar ruído
    if (fabs(*x) < DEADZONE) *x = 0.0f;
    if (fabs(*y) < DEADZONE) *y = 0.0f;
    
    // Limita os valores entre -1.0 e 1.0 (por segurança)
    *x = (*x > 1.0f) ? 1.0f : (*x < -1.0f) ? -1.0f : *x;
    *y = (*y > 1.0f) ? 1.0f : (*y < -1.0f) ? -1.0f : *y;
}

// Callback para interrupções GPIO
void gpio_callback(uint gpio, uint32_t events) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Verifica qual GPIO gerou a interrupção
    if (gpio == BUTTON_CAPTURE) {
        // Debounce para botão de captura
        if (now - last_btn_capture_time > DEBOUNCE_TIME) {
            if (events & GPIO_IRQ_EDGE_FALL) {  // Botão pressionado (falling edge)
                capture_active = true;
                capture_time = now;
                printf("Botão de CAPTURA pressionado!\n");
            }
            last_btn_capture_time = now;
        }
    }
    else if (gpio == BUTTON_LIGHTS) {
        // Debounce para botão de luzes
        if (now - last_btn_lights_time > DEBOUNCE_TIME) {
            if (events & GPIO_IRQ_EDGE_FALL) {  // Botão pressionado (falling edge)
                lights_on = !lights_on;
                printf("Luzes %s\n", lights_on ? "ON" : "OFF");
                // Atualiza LED RGB conforme estado das luzes
                if (lights_on)
                    definir_cor_rgb(255, 255, 150); // Amarelo claro
                else
                    definir_cor_rgb(0, 0, 255); // Azul
            }
            last_btn_lights_time = now;
        }
    }
    else if (gpio == BUTTON_CAMERA) {
        // Debounce para botão de câmera
        if (now - last_btn_camera_time > DEBOUNCE_TIME) {
            if (events & GPIO_IRQ_EDGE_FALL) {  // Botão pressionado (falling edge)
                camera_on = !camera_on;
                printf("Câmera %s\n", camera_on ? "ON" : "OFF");
            }
            last_btn_camera_time = now;
        }
    }
}

// Callback chamado quando recebemos pacotes UDP
static void rx_cb(void *arg, struct udp_pcb *pcb, 
                  struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if (!p) return;
    
    // Atualiza o timestamp da última recepção
    last_rx = to_ms_since_boot(get_absolute_time());
    link_ok = true;
    conexao_ok = true;
    
    // Extrai a mensagem como texto
    char msg[256] = {0};
    if (p->len < sizeof(msg) - 1) {
        memcpy(msg, p->payload, p->len);
        msg[p->len] = 0; // Garante que termine com nulo
    }
    
    printf("RX %d B de %s:%u → %s\n", p->len,
           ipaddr_ntoa(addr), port, msg);
    
    // Verifica se é um ACK (resposta ao HELLO)
    if (strcmp(msg, "ACK") == 0) {
        printf("Recebido ACK - conexão estabelecida!\n");
        // Atualizar estado
        rover_estado = ESTADO_NORMAL;
        atualizar_display();
        return;
    }
    // Processa a mensagem para extrair o score
    if (strstr(msg, "score=") != NULL) {
        char *score_str = strstr(msg, "score=");
        if (score_str) {
            int novo_score = atoi(score_str + 6); // Pula "score="
            
            // Verifica se o score aumentou (capturou ponto)
            if (novo_score > score_atual) {
                rover_estado = ESTADO_CAPTURANDO;
                ultima_captura = to_ms_since_boot(get_absolute_time());
                pontos_capturados++;
                
                // Atualiza matriz de LEDs com padrão de captura
                atualizar_buffer_matriz(padrao_captura);
                definir_leds(0, 255, 0); // Verde brilhante
                
                // LED RGB em verde
                definir_cor_rgb(0, 255, 0);
            }
            
            score_atual = novo_score;
            atualizar_display();
        }
    }
    
    pbuf_free(p);
}

// Envia mensagem HELLO para estabelecer conexão
void enviar_hello() {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 6, PBUF_RAM);
    memcpy(p->payload, "HELLO", 6);
    udp_sendto(pcb, p, &pc_addr, PC_PORT);
    pbuf_free(p);
    printf("HELLO enviado para %s:%d\n", PC_IP, PC_PORT);
}

// Envia comandos do joystick para o simulador
void enviar_comandos_rover(float joy_x, float joy_y) {
    // Transforma os valores do joystick em comandos para o rover
    // Velocidade vem do eixo Y, direção do eixo X
    float speed = joy_y * MAX_SPEED;      // Converte para a faixa desejada (-MAX_SPEED a MAX_SPEED)
    float steering = joy_x * 100.0f;      // Converte para a faixa (-100 a 100)
    
    // Formata a mensagem de controle incluindo o comando de captura
    char cmd[128];
    if (capture_active) {
        snprintf(cmd, sizeof(cmd), 
                "speed=%.1f,steering=%.1f,mode=%d,lights=%s,camera=%s,capture=1",
                speed,              // Velocidade do eixo Y
                steering,           // Direção do eixo X
                rover_mode,         // Modo (fixo em 0 = Manual)
                lights_on ? "on" : "off", 
                camera_on ? "on" : "off");
        
        // Desativa a captura após envio (para não ficar enviando constantemente)
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - capture_time > 500) {  // Mantém ativo por 500ms
            capture_active = false;
            printf("Comando de captura enviado\n");
        }
    } else {
        snprintf(cmd, sizeof(cmd), 
                "speed=%.1f,steering=%.1f,mode=%d,lights=%s,camera=%s",
                speed,              // Velocidade do eixo Y
                steering,           // Direção do eixo X
                rover_mode,         // Modo (fixo em 0 = Manual)
                lights_on ? "on" : "off", 
                camera_on ? "on" : "off");
    }
    
    // Envia o comando
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, strlen(cmd) + 1, PBUF_RAM);
    memcpy(p->payload, cmd, strlen(cmd) + 1);
    udp_sendto(pcb, p, &pc_addr, PC_PORT);
    pbuf_free(p);
    
    // Mostra a mensagem enviada (para depuração)
    static uint32_t last_print = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_print > 500) { // Limita impressão a cada 500ms
        printf("TX: %s\n", cmd);
        last_print = now;
    }
}

// Configura os pinos GPIO para botões e ADC
void configurar_gpio() {
    // Inicializa ADC para joystick
    adc_init();
    adc_gpio_init(ADC_X_PIN);  // Configura pino 26 para ADC (eixo X)
    adc_gpio_init(ADC_Y_PIN);  // Configura pino 27 para ADC (eixo Y)
    
    // Configura botões com pull-up interno
    // (botões devem conectar pino ao GND quando pressionados)
    gpio_init(BUTTON_CAPTURE);
    gpio_set_dir(BUTTON_CAPTURE, GPIO_IN);
    gpio_pull_up(BUTTON_CAPTURE);
    
    gpio_init(BUTTON_LIGHTS);
    gpio_set_dir(BUTTON_LIGHTS, GPIO_IN);
    gpio_pull_up(BUTTON_LIGHTS);
    
    gpio_init(BUTTON_CAMERA);
    gpio_set_dir(BUTTON_CAMERA, GPIO_IN);
    gpio_pull_up(BUTTON_CAMERA);
    
    // Configura interrupções GPIO para os botões
    // Monitora eventos de falling edge (quando o botão é pressionado, o pino vai para LOW)
    gpio_set_irq_enabled_with_callback(BUTTON_CAPTURE, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(BUTTON_LIGHTS, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BUTTON_CAMERA, GPIO_IRQ_EDGE_FALL, true);
    
    printf("GPIOs configurados com interrupções:\n");
    printf("- Captura (A): Pino %d\n", BUTTON_CAPTURE);
    printf("- Luzes: Pino %d\n", BUTTON_LIGHTS);
    printf("- Câmera: Pino %d\n", BUTTON_CAMERA);
    printf("ADCs configurados: X=%d, Y=%d\n", ADC_X_PIN, ADC_Y_PIN);
}

// Função para configurar o Pico W como AP e receber credenciais Wi-Fi
bool setup_wifi_portal(void) {
    printf("\n=== PORTAL DE CONFIGURAÇÃO WI-FI DO ROVER ===\n");
    printf("Iniciando sistema...\n");
    
    // Atualiza o display durante a configuração
    rover_estado = ESTADO_CONFIGURANDO;
    atualizar_display();
    
    // Passo 1: Criação do Access Point
    printf("\n=== Fase 1: Modo Access Point ===\n");
    cyw43_arch_enable_ap_mode("Rover-Setup", "roverpass", CYW43_AUTH_WPA2_AES_PSK);
    
    // Configuração de IP para o AP
    ip4_addr_t ip, netmask, gateway;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 4, 1);
    netif_set_addr(netif_default, &ip, &netmask, &gateway);
    
    printf("✓ Access Point criado!\n");
    printf("SSID: Rover-Setup\n");
    printf("Senha: roverpass\n");
    printf("IP: %s\n", ip4addr_ntoa(&ip));
    
    // Atualiza display com instruções
    ssd1306_fill(&display, 0);
    ssd1306_draw_string(&display, "Portal Wi-Fi Ativo", 5, 0);
    ssd1306_draw_string(&display, "Conecte a:", 0, 16);
    ssd1306_draw_string(&display, "Rover-Setup", 10, 28);
    ssd1306_draw_string(&display, "Senha: roverpass", 0, 40);
    ssd1306_draw_string(&display, "Acesse: 192.168.4.1", 0, 52);
    ssd1306_send_data(&display);
    
    // Passo 2: Inicia o servidor HTTP
    printf("\n=== Fase 2: Servidor Web ===\n");
    struct tcp_pcb *server = start_http_server();
    if (!server) {
        printf("❌ Erro ao iniciar servidor HTTP\n");
        return false;
    }
    printf("✓ Servidor HTTP iniciado na porta 80\n");
    printf("Acesse: http://192.168.4.1\n");
    
    printf("\n=== Sistema Pronto ===\n");
    printf("1. Conecte seu dispositivo ao Wi-Fi: Rover-Setup\n");
    printf("2. Abra o navegador e acesse: http://192.168.4.1\n");
    printf("3. Insira as credenciais da sua rede\n");
    printf("4. Aguarde a conexão...\n\n");
    
    // Matriz de LEDs em modo configuração (azul claro)
    atualizar_buffer_matriz(padrao_normal);
    definir_leds(0, 150, 255);
    
    // Loop principal - Aguarda configuração
    while (!new_wifi_config.received) {
        cyw43_arch_poll();  // Processa eventos de rede
        sleep_ms(10);
    }
    
    printf("\n=== Credenciais Recebidas! ===\n");
    printf("SSID: %s\n", new_wifi_config.ssid);
    printf("Senha: %s\n", new_wifi_config.password);
    
    // Atualiza display com informação de transição
    ssd1306_fill(&display, 0);
    ssd1306_draw_string(&display, "Configuracao OK!", 5, 0);
    ssd1306_draw_string(&display, "Conectando a:", 0, 16);
    ssd1306_draw_string(&display, new_wifi_config.ssid, 10, 28);
    ssd1306_draw_string(&display, "Aguarde...", 10, 45);
    ssd1306_send_data(&display);
    
    // Aguarda um pouco para a página de confirmação ser enviada
    sleep_ms(5000);
    
    // Passo 3: Para o servidor e fecha o AP
    printf("\n=== Fase 3: Mudança de Modo ===\n");
    tcp_close(server);
    
    // Desativa modo AP
    cyw43_arch_disable_ap_mode();
    
    // Muda para modo cliente
    cyw43_arch_enable_sta_mode();
    
    printf("✓ Modo AP desativado\n");
    printf("✓ Modo cliente ativado\n");
    
    // Passo 4: Conecta à rede especificada
    printf("\n=== Fase 4: Conexão à Nova Rede ===\n");
    printf("Tentando conectar a: %s\n", new_wifi_config.ssid);
    
    // Atualiza estado do rover
    rover_estado = ESTADO_CONECTANDO;
    
    int connection_attempts = 0;
    while (cyw43_arch_wifi_connect_timeout_ms(new_wifi_config.ssid, 
                                            new_wifi_config.password, 
                                            CYW43_AUTH_WPA2_AES_PSK, 
                                            15000)) {
        connection_attempts++;
        printf("Tentativa %d falhou. Tentando novamente...\n", connection_attempts);
        
        ssd1306_fill(&display, 0);
        ssd1306_draw_string(&display, "Falha na Conexao", 5, 0);
        ssd1306_draw_string(&display, "Tentativa: ", 0, 16);
        char temp[10];
        sprintf(temp, "%d/3", connection_attempts);
        ssd1306_draw_string(&display, temp, 70, 16);
        ssd1306_draw_string(&display, "Tentando novamente...", 0, 32);
        ssd1306_send_data(&display);
        
        if (connection_attempts >= 3) {
            printf("\n❌ Não foi possível conectar após %d tentativas\n", connection_attempts);
            printf("Verifique as credenciais e tente novamente\n");
            
            ssd1306_fill(&display, 0);
            ssd1306_draw_string(&display, "Falha na Conexao", 5, 0);
            ssd1306_draw_string(&display, "Verifique as", 10, 20);
            ssd1306_draw_string(&display, "credenciais e", 10, 32);
            ssd1306_draw_string(&display, "reinicie o rover", 5, 44);
            ssd1306_send_data(&display);
            
            // Mata o padrão da matriz
            for (int i = 0; i < NUM_PIXELS; i++) {
                buffer_leds[i] = false;
            }
            definir_leds(0, 0, 0);
            
            return false;
        }
    }
    
    // Sucesso! Conexão estabelecida
    printf("\n✓ CONECTADO COM SUCESSO!\n");
    printf("IP obtido: %s\n", ipaddr_ntoa(&cyw43_state.netif[0].ip_addr));
    
    // Atualiza display com info de sucesso
    ssd1306_fill(&display, 0);
    ssd1306_draw_string(&display, "Conectado!", 25, 0);
    ssd1306_draw_string(&display, "Rede: ", 0, 16);
    ssd1306_draw_string(&display, new_wifi_config.ssid, 40, 16);
    ssd1306_draw_string(&display, "IP: ", 0, 32);
    ssd1306_draw_string(&display, ipaddr_ntoa(&cyw43_state.netif[0].ip_addr), 30, 32);
    ssd1306_draw_string(&display, "Iniciando rover...", 0, 48);
    ssd1306_send_data(&display);
    
    // Retorna para o estado normal do rover
    rover_estado = ESTADO_NORMAL;
    
    printf("\n=== Sistema Wi-Fi Configurado ===\n");
    printf("O Rover agora está conectado à sua rede e pronto para operação!\n");
    
    // Espera um momento para exibir a mensagem
    sleep_ms(3000);
    
    return true;
}

int main()
{
    // Inicializa UART para debug
    stdio_init_all();
    sleep_ms(1000);  // Aguarda a estabilização do sistema
    printf("\n\n=== Controlador Rover com Portal de Configuração Wi-Fi ===\n");
    
    // Configura GPIO para botões e ADC
    configurar_gpio();
    printf("GPIO e ADC configurados\n");
    
    // Inicializa componentes adicionais
    inicializar_display();
    inicializar_led_rgb();
    inicializar_matriz_leds();

    // Inicializa Wi-Fi
    if (cyw43_arch_init()) { 
        printf("Falha na inicialização do Wi-Fi\n"); 
        return 1; 
    }
    
    // ===== NOVO CÓDIGO: PORTAL DE CONFIGURAÇÃO WI-FI =====
    // Inicializa o portal de configuração Wi-Fi
    if (!setup_wifi_portal()) {
        printf("Falha na configuração do Wi-Fi. O rover não pode iniciar.\n");
        
        // Mensagem de erro no display
        ssd1306_fill(&display, 0);
        ssd1306_draw_string(&display, "Erro Wi-Fi", 25, 10);
        ssd1306_draw_string(&display, "Reinicie o", 20, 30);
        ssd1306_draw_string(&display, "dispositivo", 20, 45);
        ssd1306_send_data(&display);
        
        // Loop infinito em caso de falha
        while (true) {
            sleep_ms(1000);
        }
    }
    
    // ===== CONTINUAÇÃO DO CÓDIGO ORIGINAL =====
    // Configura socket UDP
    pcb = udp_new();
    ipaddr_aton(PC_IP, &pc_addr);
    udp_bind(pcb, IP_ADDR_ANY, PICO_PORT);
    udp_recv(pcb, rx_cb, NULL);
    printf("Socket UDP configurado\n");
    
    // Inicializa variáveis de tempo
    last_sent = 0;
    last_rx = 0;
    
    printf("Iniciando comunicação com o simulador...\n");
    printf("Controles:\n");
    printf("- Joystick eixo Y: Movimento para frente/trás\n");
    printf("- Joystick eixo X: Direção esquerda/direita\n");
    printf("- Botão %d (A): CAPTURAR ponto verde\n", BUTTON_CAPTURE);
    printf("- Botão %d: Ligar/Desligar luzes\n", BUTTON_LIGHTS);
    printf("- Botão %d: Ligar/Desligar câmera\n", BUTTON_CAMERA);
    
    // Atualiza o display para o modo de operação normal
    rover_estado = ESTADO_CONECTANDO;
    atualizar_display();
    
    while (true) {
        // Processa eventos Wi-Fi
        cyw43_arch_poll();
        
        // Obtém o tempo atual
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        // Se não estabelecemos conexão ainda, envia HELLO a cada segundo
        if (!conexao_ok || (now - last_rx > 5000)) {
            if (now - last_sent >= 1000) {
                last_sent = now;
                enviar_hello();
                
                // Se perdemos conexão, reporta
                if (conexao_ok && now - last_rx > 5000) {
                    printf("Sem resposta do simulador por 5s, enviando HELLO...\n");
                    conexao_ok = false;
                    atualizar_display();
                }
            }
        } 
        // Se já temos conexão, envia comandos a cada 100ms
        else if (now - last_sent >= 100) {
            last_sent = now;
            
            // Lê os valores do joystick
            float joy_x, joy_y;
            ler_joystick(&joy_x, &joy_y);
            
            // Envia comando para o simulador
            enviar_comandos_rover(joy_x, joy_y);
        }
        
        // Retorna ao estado normal após 500ms de animação de captura
        if (rover_estado == ESTADO_CAPTURANDO && now - ultima_captura > 500) {
            rover_estado = ESTADO_NORMAL;
            
            // Retorna matriz de LEDs para o padrão normal
            atualizar_buffer_matriz(padrao_normal);
            definir_leds(0, 0, 30); // Azul
            
            // Retorna LED RGB para azul
            definir_cor_rgb(0, 0, 255);
            
            atualizar_display();
        }
        
        // Se perdemos conexão, atualiza estado visual
        if (conexao_ok && now - last_rx > 5000) {
            rover_estado = ESTADO_CONECTANDO;
            definir_cor_rgb(255, 0, 0); // Vermelho para indicar falha
            atualizar_display();
            conexao_ok = false;
        }
        
        // Pequena pausa para não sobrecarregar a CPU
        sleep_ms(10);
    }
}