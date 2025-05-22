# Rover Control Suite 🚀

**Rover Control Suite** é um _tool‑chain_ completo – firmware + simulador desktop – para
protótipos de robôs móveis baseados na **Raspberry Pi Pico W**.
Ele inclui:

* **Portal Wi‑Fi** (_captive portal_) para a primeira configuração da rede
* **Firmware de controle** com joystick analógico, botões, display OLED,
  matriz **WS2812 5 × 5** e LED RGB de status
* **Telemetria UDP** (texto ou binário) com _handshake_ **HELLO/ACK** e _heartbeat_
* **Simulador 2D** em **Python/Pygame** para testar tudo sem hardware
* **Modo de captura de pontos de interesse** com pontuação em tempo real

> Projeto mantido por **Heitor Lemos** – Licença MIT.

---

## 📂 Estrutura do repositório

| Caminho                           | Descrição                                                  |
| --------------------------------- | ------------------------------------------------------------ |
| `wifi-portal.c`                 | Firmware C para o Pico W: AP + servidor HTTP + controle UDP |
| rover/`rover_simulation.py`     | Simulador de rover em Python/Pygame                          |
| `CMakeLists.txt` & `cmake/` | Arquivos de build para o firmware                            |

---

## 🔧 Requisitos

### Hardware

| Componente                              | Qty | Observação                                |
| --------------------------------------- | --: | ------------------------------------------- |
| **Raspberry Pi Pico W**        |   1 | Soldada ou soqueteada na_baseboard_         |
| **BitDogLab baseboard**           |  – | Joystick, botões e periféricos integrados |
| Display**OLED SSD1306 128×64** |   1 | Conectado via I²C                         |
| Matriz**WS2812 5×5**            |   1 | Controlada via PIO                          |
| LED RGB                                |   1 | Indicador de estado                         |
| Alimentação 5 V                     |   1 | 500 mA min.                               |

> **Fora da BitDogLab?**
> Ajuste pinos no `wifi-portal.c`, adicione conversão de nível I²C
> e forneça 3 V3 ao Pico.

### Software

| Ferramenta                       | Versão mínima           |
| -------------------------------- | ------------------------- |
| **Pico SDK**               | 1.5.0 (testado c/ 2.1.1) |
| **CMake**                  | 3.13                      |
| **GNU Arm GCC**           | 10.3‑2021.10             |
| **Python** _(simulador)_ | 3.8                       |
| **pip pacotes**           | `pygame` ≥ 2.6       |

---

## ⚙️ Compilando o firmware

```bash
git clone https://github.com/TorRLD/wifi-portal.git
cd wifi-portal
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j$(nproc)
```

* Conecte o Pico W em **BOOTSEL**
* Copie `wifi_portal.uf2` para a unidade montada
* Reinicie o dispositivo

---

## 🛰️ Configuração Wi‑Fi (Portal Cativo)

1. Após ligar o Pico W, procure pela rede **`Rover-Setup`**
2. Senha‑padrão: **`roverpass`**
3. Lembre-se de selecionar conexão estática e escolher um IP (Exemplo: 192.168.4.xx)
4. Abra `http://192.168.4.1` e preencha SSID & senha
5. O dispositivo reinicia, conecta‑se à rede e pisca o LED azul
6. O IP é exibido no OLED; anote para usar no simulador ou UI web

---

## 🎮 Controles de Operação

| Entrada             | Ação no firmware                       | Atalho no simulador |
| ------------------- | ---------------------------------------- | ------------------- |
| Joystick Y          | Velocidade (‑MAX ↔ +MAX)             | –                  |
| Joystick X          | Direção (‑100 ↔ +100)              | –                  |
| **Botão A** | Solicita**CAPTURA** de ponto verde | `SPACE`           |
| **Botão B** | Liga / desliga**faróis**          | `L`               |
| **Botão C** | Liga / desliga**câmera**          | `C`               |

O OLED exibe `Status`, `Score` e dicas de uso.
A matriz WS2812 mostra animações distintas para modo normal
e captura concluída.

---

## 🖥️ Usando o simulador

Dentro da pasta rover, execute o script

```bash
cd rover
python rover_simulation.py 
```

*Janela 1024×768* com terreno gerado proceduralmente,
obstáculos, POIs e câmera virtual.

Funções extras do teclado:

| Tecla   | Função                                |
| ------- | --------------------------------------- |
| `P`   | Pausa / retoma o mundo físico          |
| `R`   | Recarrega bateria                       |
| `F1`  | **Manual** (joystick direto)      |
| `F2`  | **Semi-auto** (evita obstáculos) |
| `F3`  | **Autônomo** (navega p/ POIs)   |
| `M`   | Alterna protocolo Texto ↔ Binário     |
| `ESC` | Encerra                                 |

---

## 📡 Protocolo de Telemetria

* **Descoberta**: Pico envia `HELLO` → simulador responde `ACK`
* **Heartbeat**: `HELLO` a cada 5 s (link cai se >5 s sem resposta)
* **Texto** (default)
  ```
  speed=12.3,steering=-45.0,mode=0,lights=on,camera=off,capture=1
  ```
* **Binário**
  *Header* `RVRC` + *payload* `struct{ joystick; rover; }`
  Formatos declarados em `wifi-portal.c` & `rover_simulation.py`

---

Contribuições são bem‑vindas!
Abra _issues_ ou envie _pull requests_.

---

## 📝 Licença

Distribuído sob a **Licença MIT** – consulte [`LICENSE`](LICENSE) para detalhes.
