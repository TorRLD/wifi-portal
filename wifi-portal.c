// compile com: PICO_CYW43_ARCH_POLL=1
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include <math.h>
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
#define ADC_X_PIN  26  // Use pino 26 para eixo X do joystick (ADC0)
#define ADC_Y_PIN  27  // Use pino 27 para eixo Y do joystick (ADC1)

// Configurações dos botões da BitDogLab - ligar ao GND quando pressionados
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
// Declaração das funções de interrupção
void gpio_callback(uint gpio, uint32_t events);
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
    ssd1306_draw_string(&display, "Conectando...", 10, 45);
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

int main()
{
    // Inicializa UART para debug
    stdio_init_all();
    printf("\n\n=== Controlador Rover com Interrupções e Captura ===\n");
    
    // Configura GPIO para botões e ADC
    configurar_gpio();
    printf("GPIO e ADC configurados\n");
    // Inicializa componentes adicionais
    inicializar_display();
    inicializar_led_rgb();
    inicializar_matriz_leds();
    
    // Inicializa estado inicial
    rover_estado = ESTADO_CONECTANDO;
    definir_cor_rgb(0, 0, 255); // Azul para indicar conexão
    // Inicializa Wi-Fi
    if (cyw43_arch_init()) { 
        printf("Falha na inicialização do Wi-Fi\n"); 
        return 1; 
    }
    cyw43_arch_enable_sta_mode();
    
    // Conecta à rede Wi-Fi
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(
            "Nando Barbearia", "nando2661", CYW43_AUTH_WPA2_AES_PSK, 15000)) {
        printf("Tentativa de conexão falhou, tentando novamente...\n");
    }
    printf("Wi-Fi conectado! IP: %s\n", 
           ipaddr_ntoa(&cyw43_state.netif[0].ip_addr));
    
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