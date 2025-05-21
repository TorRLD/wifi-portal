# AgroRover Control Portal

## Sistema Web para Robôs de Pulverização Agrícola com Raspberry Pi Pico W

### 📚 Visão Geral

O **AgroRover Control Portal** é um sistema de configuração Wi‑Fi e controle remoto que transforma a Raspberry Pi Pico W (BitDogLab) em um ponto de acesso inicial, gerando um *portal cativo* onde o usuário configura rapidamente as credenciais da rede.
Após conectado à Internet, o firmware expõe controles de joystick, botões, LEDs e telemetria para operar um rover agrícola autônomo, fornecendo uma solução eficiente para o tratamento localizado de pragas em plantações.

### 🔎 Descrição Detalhada

Este projeto combina:

- Configuração Wi‑Fi simplificada através de interface web
- Portal cativo para primeira configuração de rede
- Feedback visual por LEDs externos
- Interação com periféricos BitDogLab (joystick, display OLED, matriz WS2812)
- Conexão com servidor em nuvem para interface avançada
- Sistema de controle para robôs agrícolas de pulverização pontual

Criando assim uma plataforma completa para agricultura de precisão e IoT agrícola.

### ✨ Características Principais

- **Configuração Wi‑Fi automática** — portal web para configuração inicial
- **Feedback visual** — LEDs indicando estados do sistema
- **Controle por botões** — interrupções para interação local
- **Atividade II – Integração em Nuvem** — comunicação UDP (texto ou binário)
- **Atividade II – Joystick Control** — leitura analógica contínua e envio de telemetria
- **Atividade II – Simulador Desktop** — teste completo sem hardware físico

### ⚙️ Requisitos de Hardware

| Componente             | Quantidade | Observações                       |
| ---------------------- | ---------: | ----------------------------------- |
| Raspberry Pi Pico W |          1 | Soqueteado na placa‑base BitDogLab |
| Joystick analógico    |          1 | Incluso na BitDogLab                |
| Botões A/B/C          |          3 | Interrupções (`gpio_irq`)       |
| Display OLED SSD1306   |          1 | I²C                                |
| Matriz WS2812 5 × 5 |          1 | PIO + DMA                         |
| LED RGB externo       |          1 | Estados de conexão                 |

> **Se quiser reproduzir fora da BitDogLab**, utilize fonte de 5 V, regulador 3 V3, conversor de nível I²C e fiação adequada para os periféricos acima.

### ⚡ Instalação Rápida do Firmware

```bash
git clone https://github.com/TorRLD/wifi-portal.git
cd wifi-portal
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j4
```

1. Conecte o Pico W em modo **BOOTSEL**.
2. Ele aparecerá como unidade de armazenamento.
3. Copie o arquivo `wifi-portal.uf2` para a unidade.

### 📱 Como Usar o Portal

1. Ligue o dispositivo; procure a rede **PicoW‑Setup**.
2. Conecte-se e abra `http://192.168.4.1`.
3. Não funciona na Atividade II Insira SSID e senha da sua rede.
4. Não funciona na Atividade IIQuando conectado, o LED verde pisca.
5. Com as mudanças da Atividade II, ele conecta na rede direto no código
6. Acesse a interface de controle remota (nuvem ou local).

---

## ✨ Novidades da Atividade II

A *Atividade II* introduziu **telemetria em tempo real**, novos **efeitos visuais** e um **simulador de desktop** para quem deseja testar tudo sem hardware físico.

Principais melhorias de código:

- **Leitura analógica do joystick** com envio de `speed`, `steering`, `lights`, `camera` e `capture` por UDP (texto / binário).
- **Botões dedicados** — A (captura), B (luzes) e C (câmera) — com *debounce* por interrupção.
- **Display OLED** exibindo conexão, pontuação e dicas de controle.
- **Matriz WS2812 5 × 5** com dois padrões (normal e captura).
- **LED RGB** indicando estados da conexão (azul = conectando, verde = captura).
- **Handshake HELLO/ACK** para descoberta automática do simulador e *heartbeat* a cada 5 s.
- **Rover Simulator** em Python/Pygame que renderiza terreno, obstáculos, câmera virtual e aceita comandos UDP idênticos.

---

## 🖥️ Simulação Desktop do Rover

### Pré‑requisitos

```bash
# Python ≥ 3.8
pip install pygame
```

### Como rodar

```bash
cd rover_simu          # pasta onde está rover_simulation.py
python rover_simulation.py
```

O simulador abre uma janela 1024 × 768 e aguarda o Pico W na porta UDP 8080.
Caso não possua o hardware, utilize as teclas impressas no terminal para pausar, recarregar bateria, alternar modos (F1‑F3) e injetar pacotes de teste.

| Tecla              | Ação                                   |
| ------------------ | ---------------------------------------- |
| **P**        | Pausar / Retomar                       |
| **R**        | Recarregar bateria                       |
| **L**        | Ligar / Desligar faróis               |
| **C**        | Ligar / Desligar câmera               |
| **F1/F2/F3** | Manual / Semi‑autônomo / Autônomo |
| **SPACE**    | Capturar ponto (teste)                   |
| **T**        | Pacote de teste                          |
| **M**        | Texto ↔ Binário                      |
| **ESC**      | Sair                                     |

> **Dica:** para testes locais, mantenha a porta **8081** liberada de saída no firewall; assim o Pico W responde ao simulador sem bloqueios.

---

### 🔧 Estrutura do Repositório

| Caminho                            | Função                                             |
| ---------------------------------- | ---------------------------------------------------- |
| `wifi-portal.c`                  | Firmware do Pico W (portal Wi‑Fi + controle UDP) |
| `rover_simu/rover_simulation.py` | Simulador completo em Python/Pygame                  |
| `CMakeLists.txt`                 | Configuração CMake do firmware                     |

### 📄 Licença

Este projeto está licenciado sob a **[Licença MIT](LICENSE)**.
