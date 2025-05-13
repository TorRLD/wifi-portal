# AgroRover Control Portal

## Sistema Web para Robôs de Pulverização Agrícola com Raspberry Pi Pico W

### 📚 Visão Geral

O AgroRover Control Portal é um sistema de configuração WiFi e controle remoto para robôs agrícolas baseado no Raspberry Pi Pico W. Implementando um portal de captive portal inicial para configuração de rede e posteriormente um cliente para servidor na nuvem, o dispositivo se conecta a um sistema de controle de robôs de pulverização para agricultura de precisão, fornecendo uma solução eficiente para o tratamento localizado de pragas em plantações.

### 🔎 Descrição Detalhada

Este projeto combina:

- Configuração WiFi simplificada através de interface web
- Portal captive para primeira configuração de rede
- Feedback visual através de LEDs externos
- Interação com periféricos da BitDogLab (joystick, display OLED, matriz de LEDs)
- Conexão com servidor na nuvem para interface avançada
- Sistema de controle para robôs agrícolas de pulverização pontual

Criando assim uma plataforma completa para agricultura de precisão e IoT agrícola.

### ✨ Características Principais

- **Configuração WiFi Automática**: Portal web para configuração inicial da rede
- **Feedback Visual**: LEDs externos indicando estados diferentes do sistema
- **Controle por Botões**: Interrupções para interação local com o sistema
- **Integração com Nuvem**: Conexão com servidor remoto para interface avançada
- **Joystick Control**: Controle da posição virtual do robô via joystick da BitDogLab
- **Estado em Tempo Real**: Display OLED para feedback local do sistema
- **Matriz RGB**: Visualização do mapa da plantação e áreas infectadas
- **Alerta Sonoro**: Buzzer para indicação de operações importantes
- **Comunicação Bidirecional**: WebSockets para troca de dados em tempo real

### 🛠️ Hardware Utilizado

| Componente                       | Descrição                          |
| -------------------------------- | ------------------------------------ |
| **Placa Principal**        | Raspberry Pi Pico W (RP2040 + WiFi)  |
| **LEDs Externos**          | GPIO 11 (Verde), 12 (Azul)           |
| **Botões**                | GPIO 6 (Botão B), GPIO 5 (Botão A) |
| **Joystick**               | GPIO 26 (ADC0, X), GPIO 27 (ADC1, Y) |
| **Display OLED**           | SSD1306 128x64 (I2C)                 |
| **LED Matrix**             | WS2812B 5x5 (PIO)                    |
| **Buzzer**                 | GPIO 10 (PWM)                        |
| **I2C**                    | GPIO 14 (SDA), GPIO 15 (SCL)         |
| **Fonte de Alimentação** | USB 5V                               |

### ⚙️ Princípio de Funcionamento

#### Fase de Configuração:

- Inicialização em modo Access Point ("PicoW-Setup")
- Servidor HTTP na porta 80 com página de configuração
- Captura de credenciais WiFi através de formulário web
- Feedback visual através de LED Azul

#### Fase de Operação:

- Conexão à rede WiFi configurada
- Feedback via LED Verde quando conexão bem-sucedida
- Comunicação com servidor na nuvem para interface avançada
- Leitura do joystick para controle de movimento
- Exibição de estado no display OLED
- Representação visual na matriz de LEDs

#### Sistema de Interação:

- Botão B para controle local (interrupção de LEDs)
- Interface web para controle remoto
- Feedback sonoro via buzzer para alertas e confirmações

### 🔄 Fluxo de Operação

1. Inicialização do sistema e módulo WiFi
2. Criação do Access Point para configuração inicial
3. Aguardar conexão de dispositivo ao AP (LED Azul pisca)
4. Receber e processar credenciais WiFi
5. Tentar conexão à rede configurada
6. Em caso de sucesso, LED Verde pisca
7. Conectar ao servidor na nuvem
8. Iniciar operação normal:
   - Ler valores do joystick e botões
   - Enviar dados para o servidor
   - Receber comandos e atualizar periféricos
   - Controlar robô de pulverização virtual

### ⚡ Aspectos Técnicos Importantes

1. **Gestão de Conexão WiFi**:

   - Implementação de Access Point e Cliente
   - Manipulação de requisições HTTP
   - Processamento de formulários web
   - Comunicação REST com servidor na nuvem
2. **Tratamento de Interrupções**:

   - Configuração de GPIO IRQ para detecção de botões
   - Sistema de debounce para evitar falsos acionamentos
   - Callback para tratamento assíncrono de eventos
3. **Controle Não-Bloqueante**:

   - Mecanismo baseado em tempo para piscar LEDs
   - Verificação periódica de estado sem bloqueio
   - Sincronização de operações assíncronas
4. **Comunicação Bidirecional**:

   - WebSockets para troca de dados em tempo real
   - Serialização JSON para estruturação de mensagens
   - API HTTP para comandos e configurações

### 🧩 Estrutura do Código

| Arquivo              | Função                             |
| -------------------- | ------------------------------------ |
| `agrorover.c`      | Lógica principal e ponto de entrada |
| `wifi_manager.c/h` | Gerenciamento de conexão WiFi       |
| `http_server.c/h`  | Servidor HTTP para configuração    |
| `cloud_client.c/h` | Cliente para conexão com servidor   |
| `peripherals.c/h`  | Controle de LEDs, botões e joystick |
| `display.c/h`      | Funções para o display OLED        |
| `led_matrix.c/h`   | Controle da matriz WS2812B           |
| `CMakeLists.txt`   | Configuração de compilação       |

### 🚀 Instalação e Configuração

#### Pré-requisitos

- SDK do Raspberry Pi Pico
- CMake 3.13+
- Toolchain ARM GCC
- Bibliotecas: lwIP, PicoSDK WiFi

#### 🔧 Como Compilar

```bash
git clone https://github.com/josesilva/agrorover-portal.git
cd agrorover-portal
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j4
```


#### Conectando o Pico em modo bootloader (BOOTSEL)

1. Coloque o Raspberry Pi Pico W em modo bootloader (BOOTSEL). Para isso, mantenha pressionado o botão BOOTSEL na placa enquanto conecta o cabo USB.
2. O dispositivo será montado como um dispositivo de armazenamento no computador.
3. Copie o arquivo `agrorover.uf2` para o diretório `/path/to/PICO_DRIVE` que apareceu no seu sistema.

### 📱 Como Usar

1. Após ligar o dispositivo, conecte-se à rede WiFi **"PicoW-Setup"**.
2. Abra o navegador e acesse o endereço `http://192.168.4.1`.
3. Insira as credenciais da sua rede WiFi.
4. Aguarde o dispositivo conectar-se à rede (o LED Verde começará a piscar).
5. Acesse a interface de controle através do aplicativo web ou servidor na nuvem.

### 📄 Licença

Este projeto está licenciado sob a **Licença MIT**.
