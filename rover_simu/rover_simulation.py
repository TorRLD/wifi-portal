import pygame
import socket
import struct
import threading
import time
import random
import math
import sys
import os
from pygame.locals import *

# Configurações da janela
WINDOW_WIDTH = 1024
WINDOW_HEIGHT = 768
TITLE = "Rover Simulator - BitDogLab"

# Configurações de rede
UDP_IP = "0.0.0.0"  # Escuta em todas as interfaces
UDP_PORT = 8080     # Porta para receber dados do Pico W
PICO_PORT = 8081    # Porta para enviar dados para o Pico W
LINK_OK = False
LAST_RX = 0
HELLO_TIMEOUT = 5    # s; se 5 s sem HELLO, link cai


# SIMPLIFICAÇÃO: Implementação mais robusta de comunicação para resolver problemas
# Use uma comunicação básica para estabelecer conexão, depois tentar o protocolo completo
USAR_PROTOCOLO_SIMPLES = True

# Estruturas de dados para o protocolo completo (quando USAR_PROTOCOLO_SIMPLES=False)
JOYSTICK_FORMAT = "ff???x"  # x, y, button, button_a, button_b, padding
JOYSTICK_SIZE = struct.calcsize(JOYSTICK_FORMAT)

ROVER_FORMAT = "ffffB??x"  # speed, steering, battery, temperature, mode, lights, camera, padding
ROVER_SIZE = struct.calcsize(ROVER_FORMAT)

print(f"Formato JOYSTICK: {JOYSTICK_FORMAT}, Tamanho: {JOYSTICK_SIZE} bytes")
print(f"Formato ROVER: {ROVER_FORMAT}, Tamanho: {ROVER_SIZE} bytes")

# Constantes de simulação
TERRAIN_ROUGHNESS = 0.1  # Quanto maior, mais difícil o terreno
MAX_SPEED = 5.0          # Velocidade máxima do rover (pixels/frame)
BATTERY_DRAIN_RATE = 0.01  # Taxa de drenagem da bateria por frame
TEMPERATURE_BASE = 25.0    # Temperatura base em °C
TEMPERATURE_VARIANCE = 10.0 # Variação máxima de temperatura
CAPTURE_DISTANCE = 50     # Distância máxima para capturar um ponto

# Modos de operação do rover
MODE_MANUAL = 0
MODE_SEMI_AUTO = 1
MODE_AUTONOMOUS = 2

# Classe principal do simulador do rover
class RoverSimulator:
    def __init__(self):
        # Inicializa o pygame
        pygame.init()
        pygame.display.set_caption(TITLE)
        
        # Configura a janela
        self.screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        self.clock = pygame.time.Clock()
        self.font = pygame.font.SysFont('Arial', 18)
        self.big_font = pygame.font.SysFont('Arial', 24, bold=True)
        
        # Carrega imagens e recursos
        self.load_assets()
        
        # Inicializa o estado do rover
        self.rover_x = WINDOW_WIDTH // 2
        self.rover_y = WINDOW_HEIGHT // 2
        self.rover_angle = 0
        self.rover_speed = 0
        self.rover_steering = 0
        self.rover_battery = 100.0
        self.rover_temperature = TEMPERATURE_BASE
        self.rover_mode = MODE_MANUAL
        self.rover_lights = False
        self.rover_camera = False
        
        # Variáveis para o terreno
        self.terrain_offset_x = 0
        self.terrain_offset_y = 0
        
        # Trajetória do rover (para desenhar o rastro)
        self.trajectory = []
        self.max_trajectory_points = 100
        
        # Obstáculos no mapa
        self.obstacles = self.generate_obstacles(15)  # Gera 15 obstáculos aleatórios
        
        # Pontos de interesse no mapa
        self.poi = self.generate_poi(5)  # Gera 5 pontos de interesse
        
        # NOVO: Lista para rastrear pontos capturados
        self.captured_poi = []
        self.capture_requested = False
        self.capture_score = 0
        self.capture_animation_time = 0
        self.capture_animation_pos = None
        
        # Configurações de rede
        self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.udp_socket.bind((UDP_IP, UDP_PORT))
        
        # NOVO: Configura um timeout para o socket para não bloquear
        self.udp_socket.settimeout(0.01)  # Timeout curto para permitir saída limpa
        
        # Endereço do Pico W (definido manualmente)
        # Inicialmente None, será atualizado automaticamente quando receber dados
        self.pico_address = None
        
        # Flag para indicar conexão
        self.connected = False
        print(f"Aguardando conexão do Pico W. Descoberta automática de endereço ativada.")
        
        # Thread para receber dados
        self.running = True
        self.receive_thread = threading.Thread(target=self.receive_data)
        self.receive_thread.daemon = True
        self.receive_thread.start()
        
        # Timestamp do último pacote recebido
        self.last_packet_time = time.time()
        
        # Variáveis para modo autônomo
        self.autonomous_target = None
        self.autonomous_path = []
        
        # Flag para pausar a simulação
        self.paused = False
        
        # NOVO: Log das últimas mensagens recebidas para depuração
        self.message_log = []
        self.max_log_entries = 5
        
        # NOVO: Lock para proteger o socket e variáveis compartilhadas
        self.socket_lock = threading.Lock()
        
    def load_assets(self):
        """Carrega as imagens e recursos necessários"""
        # Carrega a imagem do rover (ou cria uma imagem básica)
        try:
            self.rover_img = pygame.image.load('rover.png').convert_alpha()
            self.rover_img = pygame.transform.scale(self.rover_img, (64, 64))
        except:
            # Cria uma imagem básica se o arquivo não for encontrado
            self.rover_img = pygame.Surface((64, 64), pygame.SRCALPHA)
            pygame.draw.rect(self.rover_img, (200, 50, 50), (10, 10, 44, 44))
            pygame.draw.rect(self.rover_img, (50, 50, 200), (20, 5, 24, 15))
            pygame.draw.circle(self.rover_img, (30, 30, 30), (20, 50), 10)
            pygame.draw.circle(self.rover_img, (30, 30, 30), (44, 50), 10)
        
        # Imagens para obstáculos e pontos de interesse
        self.rock_img = pygame.Surface((40, 40), pygame.SRCALPHA)
        pygame.draw.circle(self.rock_img, (120, 120, 120), (20, 20), 15)
        
        self.poi_img = pygame.Surface((30, 30), pygame.SRCALPHA)
        pygame.draw.circle(self.poi_img, (50, 200, 50), (15, 15), 10)
        
        # NOVO: Imagem para pontos capturados
        self.captured_poi_img = pygame.Surface((30, 30), pygame.SRCALPHA)
        pygame.draw.circle(self.captured_poi_img, (100, 100, 100), (15, 15), 8)
        pygame.draw.circle(self.captured_poi_img, (200, 200, 200), (15, 15), 5)
        
        # Imagem para câmera
        self.camera_view = pygame.Surface((320, 240))
        self.camera_view.fill((20, 20, 20))
        
        # Ícones para luzes, bateria, etc.
        self.light_on_img = pygame.Surface((20, 20), pygame.SRCALPHA)
        pygame.draw.circle(self.light_on_img, (255, 255, 100), (10, 10), 8)
        
        self.light_off_img = pygame.Surface((20, 20), pygame.SRCALPHA)
        pygame.draw.circle(self.light_off_img, (100, 100, 100), (10, 10), 8)
        
    def generate_obstacles(self, count):
        """Gera obstáculos aleatórios no mapa"""
        obstacles = []
        for _ in range(count):
            x = random.randint(100, WINDOW_WIDTH - 100)
            y = random.randint(100, WINDOW_HEIGHT - 100)
            size = random.randint(20, 50)
            obstacles.append((x, y, size))
        return obstacles
    
    def generate_poi(self, count):
        """Gera pontos de interesse aleatórios no mapa"""
        points = []
        for _ in range(count):
            x = random.randint(100, WINDOW_WIDTH - 100)
            y = random.randint(100, WINDOW_HEIGHT - 100)
            points.append((x, y))
        return points
        
    def receive_data(self):
        """Thread para receber dados do Pico W"""
        global LINK_OK, LAST_RX  # Declara as variáveis globais
        print("Thread de recepção iniciada. Aguardando pacotes UDP...")
        
        while self.running:
            try:
                # CORREÇÃO: Proteja o acesso ao socket com lock
                with self.socket_lock:
                    if not self.running:
                        break
                    
                    try:
                        data, addr = self.udp_socket.recvfrom(1024)
                    except socket.timeout:
                        # Timeout normal do socket
                        continue
                    except Exception as e:
                        if self.running:  # Ignora erros se estamos encerrando
                            print(f"Erro ao receber dados: {e}")
                        continue
                
                # Remover caracteres nulos antes de decodificar
                data = data.replace(b'\x00', b'')
                
                # Tenta decodificar como texto, ignorando caracteres problemáticos
                try:
                    msg = data.decode('utf-8', errors='ignore')
                    # CORREÇÃO: Remove caracteres nulos da string
                    msg = msg.replace('\x00', '')
                except:
                    msg = "<não-decodificável>"
                
                print(f"Recebido pacote de {addr[0]}:{addr[1]} com {len(data)} bytes")

                if msg == "HELLO":
                    # Descoberta do Pico W - responde imediatamente com ACK
                    pico_address = (addr[0], PICO_PORT)
                    
                    # CORREÇÃO: Proteja o acesso ao socket
                    with self.socket_lock:
                        if self.running:
                            self.udp_socket.sendto(b"ACK", pico_address)
                    
                    LINK_OK = True
                    LAST_RX = time.time()
                    
                    # Atualiza o endereço e o estado de conexão
                    self.pico_address = pico_address
                    self.connected = True
                    self.last_packet_time = time.time()
                    
                    print(f"HELLO recebido de {addr[0]}. ACK enviado para {pico_address}")
                    self.add_to_message_log("RX: HELLO (estabelecendo conexão)")
                    continue
                
                # Atualiza o endereço do Pico W automaticamente
                if self.pico_address is None or addr[0] != self.pico_address[0]:
                    print(f"Endereço do Pico W detectado: {addr[0]}:{PICO_PORT}")
                    self.pico_address = (addr[0], PICO_PORT)
                
                # Atualiza estado de conexão e timestamp
                self.connected = True
                self.last_packet_time = time.time()
                LAST_RX = time.time()  # Atualiza variável global também
                LINK_OK = True          # Marca link como ativo
                
                # SIMPLIFICAÇÃO: Tenta decodificar como texto simples primeiro
                try:
                    text_data = msg
                    print(f"Mensagem de texto: {text_data}")
                    
                    # Adiciona ao log
                    self.add_to_message_log(f"RX: {text_data}")
                    
                    # Tenta extrair valores do texto (formato: key1=value1,key2=value2,...)
                    if "," in text_data and "=" in text_data:
                        values = {}
                        try:
                            pairs = text_data.split(",")
                            for pair in pairs:
                                if "=" in pair:
                                    key, value = pair.split("=", 1)
                                    key = key.strip()
                                    value = value.strip()
                                    
                                    if key in ["speed", "steering", "battery", "temperature"]:
                                        values[key] = float(value)
                                    elif key in ["mode"]:
                                        values[key] = int(value)
                                    elif key in ["lights", "camera"]:
                                        values[key] = value.lower() in ["true", "1", "yes", "on"]
                                    # NOVO: Verifica o comando de captura
                                    elif key == "capture" and value in ["1", "true", "yes", "on"]:
                                        self.capture_requested = True
                                        print("🟢 Comando de CAPTURA recebido!")
                            
                            # Atualiza o estado do rover com base nos valores extraídos
                            if "speed" in values:
                                self.rover_speed = values["speed"] / 100.0 * MAX_SPEED
                            if "steering" in values:
                                self.rover_steering = values["steering"] / 100.0
                            if "mode" in values:
                                self.rover_mode = values["mode"]
                            if "lights" in values:
                                self.rover_lights = values["lights"]
                            if "camera" in values:
                                self.rover_camera = values["camera"]
                            
                            print(f"Atualizado estado do rover com: {values}")
                        except Exception as e:
                            print(f"Erro ao extrair valores do texto: {e}")
                    
                    # Envia resposta de estado como texto se estiver usando protocolo simples
                    if USAR_PROTOCOLO_SIMPLES:
                        self.send_status_text()
                    else:
                        self.send_status()
                
                except Exception as e:
                    print(f"Erro ao processar mensagem: {e}")
                
                # Tenta processá-lo como um pacote binário se tiver pelo menos um cabeçalho
                if len(data) >= 4:
                    header = data[0:4]
                    print(f"Cabeçalho: {header}")
                    
                    if header == b'RVRC':
                        print("Cabeçalho RVRC detectado!")
                        
                        # Informações de debug para diagnóstico
                        print(f"DEBUG - Tamanho do pacote recebido: {len(data)} bytes")
                        
                        # Primeiro, verifica se temos apenas dados do rover (pacote de teste)
                        if len(data) >= 4 + ROVER_SIZE and len(data) < 4 + JOYSTICK_SIZE + ROVER_SIZE:
                            try:
                                # Extrai apenas os dados do rover (usado em pacotes de teste)
                                rover_data = struct.unpack(ROVER_FORMAT, data[4:4+ROVER_SIZE])
                                print(f"Dados do rover recebidos (pacote de teste): speed={rover_data[0]:.2f}, mode={rover_data[4]}")
                                
                                # Atualiza o rover com os dados básicos
                                self.update_from_rover_only(rover_data)
                                
                                # Envia dados de status de volta para o Pico W
                                if USAR_PROTOCOLO_SIMPLES:
                                    self.send_status_text()
                                else:
                                    self.send_status()
                            except struct.error as e:
                                print(f"Erro ao desempacotar dados do rover (pacote de teste): {e}")
                        
                        # Verifica se temos dados completos
                        elif len(data) >= 4 + JOYSTICK_SIZE + ROVER_SIZE:
                            try:
                                # Extrai os dados do joystick
                                joystick_data = struct.unpack(JOYSTICK_FORMAT, data[4:4+JOYSTICK_SIZE])
                                
                                # Extrai os dados do rover
                                rover_data = struct.unpack(ROVER_FORMAT, data[4+JOYSTICK_SIZE:4+JOYSTICK_SIZE+ROVER_SIZE])
                                
                                # Mostra os dados do joystick para depuração
                                print(f"Dados do joystick: x={joystick_data[0]:.2f}, y={joystick_data[1]:.2f}")
                                print(f"Dados do rover: speed={rover_data[0]:.2f}, steering={rover_data[1]:.2f}, mode={rover_data[4]}")
                                
                                # Atualiza o estado do rover com base nos dados recebidos
                                self.update_from_controller(joystick_data, rover_data)
                                
                                # Envia dados de status de volta para o Pico W
                                if USAR_PROTOCOLO_SIMPLES:
                                    self.send_status_text()
                                else:
                                    self.send_status()
                            except struct.error as e:
                                print(f"Erro ao desempacotar dados completos: {e}")
                        else:
                            print(f"Pacote RVRC muito pequeno: {len(data)} bytes")
                    else:
                        print(f"Cabeçalho desconhecido: {header}")
                
            except socket.timeout:
                # Timeout é normal para um socket não-bloqueante
                pass
            except Exception as e:
                if self.running:  # Ignora erros se estamos encerrando
                    print(f"Erro na thread de recepção: {e}")
            
            # Envia pacotes de status periodicamente mesmo sem receber pacotes
            current_time = time.time()
            if current_time - self.last_packet_time > 2.0 and self.connected and self.pico_address and self.running:
                if USAR_PROTOCOLO_SIMPLES:
                    self.send_status_text()
                else:
                    self.send_status()
                
            time.sleep(0.01)  # Pequeno atraso para não sobrecarregar a CPU
    
    def add_to_message_log(self, message):
        """Adiciona uma mensagem ao log para depuração"""
        # CORREÇÃO: Garante que a mensagem não tenha caracteres nulos
        message = message.replace('\x00', '')
        
        # Limita o tamanho da mensagem para evitar problemas
        if len(message) > 100:
            message = message[:97] + "..."
        
        self.message_log.append(message)
        if len(self.message_log) > self.max_log_entries:
            self.message_log.pop(0)
    
    def update_from_rover_only(self, rover_data):
        """Atualiza o estado do rover com base apenas nos dados do rover (sem joystick)"""
        # Extrai os dados do rover
        speed, steering, battery, temperature, mode, lights, camera = rover_data[0:7]
        
        # Atualiza os valores do rover
        self.rover_speed = speed / 100.0 * MAX_SPEED
        self.rover_steering = steering / 100.0
        self.rover_mode = mode
        self.rover_lights = lights
        self.rover_camera = camera
        
        # Para melhor depuração
        print(f"Atualizando rover com: velocidade={self.rover_speed:.2f}, direção={self.rover_steering:.2f}")
        print(f"Modo={self.rover_mode}, Luzes={self.rover_lights}, Câmera={self.rover_camera}")
    
    def update_from_controller(self, joystick_data, rover_data):
        """Atualiza o estado do rover com base nos dados do controlador"""
        # Extrai dados do joystick (ignorando o padding)
        joy_x, joy_y, joy_btn, btn_a, btn_b = joystick_data[0:5]
        
        # No modo manual, usa o joystick para controlar o rover
        if self.rover_mode == MODE_MANUAL:
            # Atualiza velocidade e direção com base no joystick
            self.rover_speed = rover_data[0] / 100.0 * MAX_SPEED  # Normaliza para a velocidade máxima
            self.rover_steering = rover_data[1] / 100.0  # -1.0 a 1.0
        
        # Atualiza o modo de operação
        self.rover_mode = rover_data[4]
        
        # Atualiza status de luzes e câmera
        self.rover_lights = rover_data[5]
        self.rover_camera = rover_data[6]
    
    def send_status_text(self):
        """Envia dados de status para o Pico W em formato de texto simples"""
        if not self.pico_address or not self.running:
            return
        
        # Cria uma mensagem de texto com o estado atual
        msg = (
            f"speed={self.rover_speed * 100.0 / MAX_SPEED:.1f},"
            f"steering={self.rover_steering * 100.0:.1f},"
            f"battery={self.rover_battery:.1f},"
            f"temp={self.rover_temperature:.1f},"
            f"mode={self.rover_mode},"
            f"lights={'on' if self.rover_lights else 'off'},"
            f"camera={'on' if self.rover_camera else 'off'},"
            f"score={self.capture_score}"
        )
        
        # Envia a mensagem
        try:
            # CORREÇÃO: Protege o acesso ao socket com lock
            with self.socket_lock:
                if self.running:
                    self.udp_socket.sendto(msg.encode('utf-8'), self.pico_address)
            self.add_to_message_log(f"TX: {msg[:30]}...")
        except Exception as e:
            print(f"Erro ao enviar status de texto: {e}")
    
    def send_status(self):
        """Envia dados de status para o Pico W usando o protocolo binário"""
        if not self.pico_address or not self.running:
            return
        
        # Prepara o pacote com os dados do rover
        header = b'RVRS'
        rover_data = struct.pack(
            ROVER_FORMAT,
            self.rover_speed * 100.0 / MAX_SPEED,  # Normaliza para -100 a 100
            self.rover_steering * 100.0,           # Normaliza para -100 a 100
            self.rover_battery,
            self.rover_temperature,
            self.rover_mode,
            self.rover_lights,
            self.rover_camera
            # O padding 'x' no formato não precisa de um valor
        )
        
        # Envia o pacote
        try:
            # CORREÇÃO: Protege o acesso ao socket com lock
            with self.socket_lock:
                if self.running:
                    self.udp_socket.sendto(header + rover_data, self.pico_address)
            # Não exibimos mensagens para cada envio para não sobrecarregar o console
        except Exception as e:
            print(f"Erro ao enviar dados de status: {e}")
    
    def update(self):
        """Atualiza o estado da simulação"""
        if self.paused:
            return
            
        # NOVO: Processa pedido de captura de pontos
        if self.capture_requested:
            self.try_capture_poi()
            self.capture_requested = False
            
        # Atualiza o rover com base no modo atual
        if self.rover_mode == MODE_MANUAL:
            # Modo manual - controle direto
            self.update_manual_mode()
        elif self.rover_mode == MODE_SEMI_AUTO:
            # Modo semi-autônomo - assistência ao controle
            self.update_semi_auto_mode()
        elif self.rover_mode == MODE_AUTONOMOUS:
            # Modo autônomo - navegação automatizada
            self.update_autonomous_mode()
        
        # Atualiza a posição do rover com base na velocidade e direção
        delta_angle = self.rover_steering * 2.0  # Fator de conversão para ângulo
        
        self.rover_angle += delta_angle
        rad_angle = math.radians(self.rover_angle)
        
        # Calcula o movimento com base no ângulo e velocidade
        dx = math.sin(rad_angle) * self.rover_speed
        dy = -math.cos(rad_angle) * self.rover_speed
        
        # Atualiza a posição
        new_x = self.rover_x + dx
        new_y = self.rover_y + dy
        
        # Verifica colisões com obstáculos
        if not self.check_collision(new_x, new_y):
            self.rover_x = new_x
            self.rover_y = new_y
            
            # Limita a posição ao tamanho da janela
            self.rover_x = max(32, min(self.rover_x, WINDOW_WIDTH - 32))
            self.rover_y = max(32, min(self.rover_y, WINDOW_HEIGHT - 32))
        
        # Atualiza a trajetória
        if abs(self.rover_speed) > 0.1:
            self.trajectory.append((self.rover_x, self.rover_y))
            if len(self.trajectory) > self.max_trajectory_points:
                self.trajectory.pop(0)
        
        # Atualiza a bateria
        self.rover_battery -= abs(self.rover_speed) * BATTERY_DRAIN_RATE
        self.rover_battery = max(0.0, min(self.rover_battery, 100.0))
        
        # Atualiza a temperatura com variações realistas
        temp_change = (random.random() - 0.5) * 0.2  # Pequena variação aleatória
        temp_change += abs(self.rover_speed) * 0.02  # Temperatura aumenta com velocidade
        self.rover_temperature += temp_change
        self.rover_temperature = max(TEMPERATURE_BASE - 5, min(self.rover_temperature, TEMPERATURE_BASE + TEMPERATURE_VARIANCE))
        
        # Verifica se perdemos a conexão
        current_time = time.time()
        if self.connected and current_time - self.last_packet_time > 5.0:
            print("⚠️ Sem comunicação com o Pico W nos últimos 5 segundos")
            # Não desativamos a flag connected para continuar tentando enviar pacotes
            
        # NOVO: Atualiza o tempo da animação de captura
        if self.capture_animation_time > 0:
            if time.time() - self.capture_animation_time > 2.0:  # Animação dura 2 segundos
                self.capture_animation_time = 0
                self.capture_animation_pos = None
    
    # NOVO: Função para tentar capturar um ponto de interesse
    def try_capture_poi(self):
        """Tenta capturar um ponto de interesse próximo ao rover"""
        for poi in self.poi[:]:  # Cria uma cópia para poder modificar durante o loop
            # Verifica se o ponto já foi capturado
            if poi in self.captured_poi:
                continue
                
            # Calcula a distância entre o rover e o ponto
            dist = math.sqrt((poi[0] - self.rover_x)**2 + (poi[1] - self.rover_y)**2)
            
            # Se o rover estiver próximo o suficiente, captura o ponto
            if dist < CAPTURE_DISTANCE:
                self.captured_poi.append(poi)
                self.capture_score += 100  # Adiciona pontos ao score
                
                # Inicia a animação de captura
                self.capture_animation_time = time.time()
                self.capture_animation_pos = poi
                
                print(f"🟢 Ponto capturado em {poi}! Score: {self.capture_score}")
                
                # Se ainda tiver poucos pontos, adiciona mais
                if len(self.poi) - len(self.captured_poi) < 2:
                    self.add_new_poi()
                
                # Encerra após a primeira captura (captura apenas um ponto por vez)
                break
    
    # NOVO: Função para adicionar novos pontos de interesse
    def add_new_poi(self):
        """Adiciona novos pontos de interesse ao mapa"""
        # Adiciona um ponto em uma posição aleatória
        for _ in range(2):  # Adiciona 2 novos pontos
            while True:
                x = random.randint(100, WINDOW_WIDTH - 100)
                y = random.randint(100, WINDOW_HEIGHT - 100)
                new_poi = (x, y)
                
                # Verifica se o ponto está longe de obstáculos
                valid = True
                for ox, oy, size in self.obstacles:
                    if math.sqrt((ox - x)**2 + (oy - y)**2) < size + 50:
                        valid = False
                        break
                
                # Verifica se está longe de outros pontos
                for px, py in self.poi:
                    if math.sqrt((px - x)**2 + (py - y)**2) < 100:
                        valid = False
                        break
                
                # Se a posição for válida, adiciona o ponto
                if valid:
                    self.poi.append(new_poi)
                    print(f"Novo ponto de interesse adicionado em {new_poi}")
                    break
    
    def update_manual_mode(self):
        """Atualiza no modo manual"""
        # Já tratado pela entrada do joystick
        pass
    
    def update_semi_auto_mode(self):
        """Atualiza no modo semi-autônomo"""
        # Assistência para evitar obstáculos
        min_distance = float('inf')
        closest_obstacle = None
        
        # Encontra o obstáculo mais próximo
        for ox, oy, size in self.obstacles:
            dist = math.sqrt((ox - self.rover_x)**2 + (oy - self.rover_y)**2)
            if dist < min_distance:
                min_distance = dist
                closest_obstacle = (ox, oy, size)
        
        # Se há um obstáculo próximo, ajusta a direção para evitá-lo
        if min_distance < 100:
            ox, oy, _ = closest_obstacle
            
            # Calcula ângulo para o obstáculo
            angle_to_obstacle = math.degrees(math.atan2(ox - self.rover_x, -(oy - self.rover_y))) % 360
            
            # Calcula a diferença de ângulo
            angle_diff = (angle_to_obstacle - self.rover_angle) % 360
            if angle_diff > 180:
                angle_diff -= 360
            
            # Aplica uma força repulsiva proporcional à proximidade
            repulsion = 1.0 - min_distance / 100.0
            steering_adjust = -math.copysign(repulsion, angle_diff)
            
            # Limita o ajuste de direção
            self.rover_steering = max(-1.0, min(1.0, self.rover_steering + steering_adjust * 0.2))
            
            # Reduz a velocidade perto de obstáculos
            self.rover_speed *= (0.8 + 0.2 * (min_distance / 100.0))
    
    def update_autonomous_mode(self):
        """Atualiza no modo autônomo"""
        # No modo autônomo, o rover busca pontos de interesse por conta própria
        
        # Se não temos um alvo, seleciona o ponto de interesse mais próximo não visitado
        if not self.autonomous_target:
            min_distance = float('inf')
            closest_poi = None
            
            for poi in self.poi:
                # Verifica se o ponto já foi capturado
                if poi in self.captured_poi:
                    continue
                
                # Calcula a distância até o ponto
                dist = math.sqrt((poi[0] - self.rover_x)**2 + (poi[1] - self.rover_y)**2)
                if dist < min_distance:
                    min_distance = dist
                    closest_poi = poi
            
            # Se encontrou um POI não visitado, define como alvo
            if closest_poi:
                self.autonomous_target = closest_poi
                print(f"Novo alvo: {closest_poi}")
            else:
                # Se todos os POIs foram visitados, seleciona um aleatório
                if self.poi:
                    # Tenta encontrar um ponto não capturado
                    uncaptured = [p for p in self.poi if p not in self.captured_poi]
                    if uncaptured:
                        self.autonomous_target = random.choice(uncaptured)
                    else:
                        self.autonomous_target = random.choice(self.poi)
        
        # Se temos um alvo, navegamos até ele
        if self.autonomous_target:
            tx, ty = self.autonomous_target
            
            # Calcula a distância até o alvo
            dist = math.sqrt((tx - self.rover_x)**2 + (ty - self.rover_y)**2)
            
            # Se chegamos ao alvo, tenta capturá-lo
            if dist < 30:
                # Se o alvo ainda não foi capturado, solicita captura
                if self.autonomous_target not in self.captured_poi:
                    self.capture_requested = True
                
                print(f"Alvo alcançado: {self.autonomous_target}")
                self.autonomous_target = None
                return
            
            # Calcula o ângulo para o alvo
            target_angle = math.degrees(math.atan2(tx - self.rover_x, -(ty - self.rover_y))) % 360
            
            # Calcula a diferença de ângulo
            angle_diff = (target_angle - self.rover_angle) % 360
            if angle_diff > 180:
                angle_diff -= 360
            
            # Ajusta a direção para apontar para o alvo
            self.rover_steering = max(-1.0, min(1.0, angle_diff / 90.0))
            
            # Ajusta a velocidade com base na distância e ângulo
            speed_factor = 1.0 - min(1.0, abs(angle_diff) / 90.0) * 0.8
            self.rover_speed = MAX_SPEED * speed_factor * 0.8
            
            # Aplica a lógica de desvio de obstáculos do modo semi-autônomo
            min_obstacle_dist = float('inf')
            closest_obstacle = None
            
            for ox, oy, size in self.obstacles:
                o_dist = math.sqrt((ox - self.rover_x)**2 + (oy - self.rover_y)**2) - size
                if o_dist < min_obstacle_dist:
                    min_obstacle_dist = o_dist
                    closest_obstacle = (ox, oy, size)
            
            if min_obstacle_dist < 80:
                # Há um obstáculo próximo, ajusta a rota
                ox, oy, _ = closest_obstacle
                
                # Calcula ângulo para o obstáculo
                obstacle_angle = math.degrees(math.atan2(ox - self.rover_x, -(oy - self.rover_y))) % 360
                
                # Calcula a diferença de ângulo
                obstacle_diff = (obstacle_angle - self.rover_angle) % 360
                if obstacle_diff > 180:
                    obstacle_diff -= 360
                
                # Aplica uma força repulsiva proporcional à proximidade
                repulsion = 1.0 - min_obstacle_dist / 80.0
                avoid_dir = -math.copysign(repulsion, obstacle_diff)
                
                # Combina com a direção para o alvo
                self.rover_steering = max(-1.0, min(1.0, self.rover_steering + avoid_dir))
                
                # Reduz a velocidade perto de obstáculos
                self.rover_speed *= (0.5 + 0.5 * (min_obstacle_dist / 80.0))
        else:
            # Sem alvo, desacelera
            self.rover_speed *= 0.9
            
    def check_collision(self, x, y):
        """Verifica se há colisão com obstáculos"""
        for ox, oy, size in self.obstacles:
            # Calcula a distância entre o rover e o obstáculo
            dist = math.sqrt((ox - x)**2 + (oy - y)**2)
            
            # Se a distância for menor que a soma dos raios, há colisão
            if dist < (32 + size/2):  # 32 é metade do tamanho do rover
                return True
        
        return False
    
    def draw(self):
        """Desenha a simulação na tela"""
        # Limpa a tela
        self.screen.fill((50, 50, 50))
        
        # Desenha um grid de referência
        grid_size = 50
        for x in range(0, WINDOW_WIDTH, grid_size):
            pygame.draw.line(self.screen, (70, 70, 70), (x, 0), (x, WINDOW_HEIGHT))
        for y in range(0, WINDOW_HEIGHT, grid_size):
            pygame.draw.line(self.screen, (70, 70, 70), (0, y), (WINDOW_WIDTH, y))
        
        # Desenha a trajetória
        if len(self.trajectory) > 1:
            pygame.draw.lines(self.screen, (100, 100, 255), False, self.trajectory, 2)
        
        # Desenha os obstáculos
        for x, y, size in self.obstacles:
            scaled_img = pygame.transform.scale(self.rock_img, (size, size))
            self.screen.blit(scaled_img, (x - size/2, y - size/2))
        
        # Desenha os pontos de interesse
        for x, y in self.poi:
            # Se o ponto já foi capturado, desenha diferente
            if (x, y) in self.captured_poi:
                self.screen.blit(self.captured_poi_img, (x - 15, y - 15))
            else:
                self.screen.blit(self.poi_img, (x - 15, y - 15))
            
            # Desenha um círculo ao redor do ponto alvo no modo autônomo
            if self.autonomous_target and (x, y) == self.autonomous_target:
                pygame.draw.circle(self.screen, (0, 255, 0), (x, y), 20, 2)
                
            # Desenha raio de captura ao redor do rover (para facilitar)
            if not (x, y) in self.captured_poi:
                dist = math.sqrt((x - self.rover_x)**2 + (y - self.rover_y)**2)
                if dist < CAPTURE_DISTANCE:
                    pygame.draw.circle(self.screen, (255, 255, 0), (x, y), 25, 1)
        
        # Rotaciona e desenha o rover
        rotated_rover = pygame.transform.rotate(self.rover_img, -self.rover_angle)
        rover_rect = rotated_rover.get_rect(center=(self.rover_x, self.rover_y))
        self.screen.blit(rotated_rover, rover_rect.topleft)
        
        # NOVO: Desenha a área de captura ao redor do rover
        pygame.draw.circle(self.screen, (100, 100, 250, 40), 
                          (int(self.rover_x), int(self.rover_y)), 
                          CAPTURE_DISTANCE, 1)
        
        # Desenha luzes do rover quando ativadas
        if self.rover_lights:
            # Calcula as posições das luzes com base no ângulo
            angle_rad = math.radians(self.rover_angle)
            light_dist = 40
            light_spread = 20
            
            # Luz esquerda
            left_angle = angle_rad + math.radians(light_spread)
            lx = self.rover_x + math.sin(left_angle) * light_dist
            ly = self.rover_y - math.cos(left_angle) * light_dist
            
            # Luz direita
            right_angle = angle_rad - math.radians(light_spread)
            rx = self.rover_x + math.sin(right_angle) * light_dist
            ry = self.rover_y - math.cos(right_angle) * light_dist
            
            # Desenha os feixes de luz
            for i in range(5, 100, 5):
                alpha = max(0, 255 - i * 2.5)
                radius = i / 5
                left_surf = pygame.Surface((radius * 2, radius * 2), pygame.SRCALPHA)
                pygame.draw.circle(left_surf, (255, 255, 200, alpha), (radius, radius), radius)
                self.screen.blit(left_surf, (lx - radius, ly - radius))
                
                right_surf = pygame.Surface((radius * 2, radius * 2), pygame.SRCALPHA)
                pygame.draw.circle(right_surf, (255, 255, 200, alpha), (radius, radius), radius)
                self.screen.blit(right_surf, (rx - radius, ry - radius))
        
        # NOVO: Desenha a animação de captura se estiver ativa
        if self.capture_animation_time > 0 and self.capture_animation_pos:
            # Calcula o tempo desde o início da animação
            elapsed = time.time() - self.capture_animation_time
            # A animação dura 2 segundos, o tamanho cresce e depois diminui
            if elapsed < 1.0:
                size = int(30 + 20 * elapsed)  # Cresce
            else:
                size = int(50 - 50 * (elapsed - 1.0))  # Diminui
            
            # Desenha círculos concêntricos
            x, y = self.capture_animation_pos
            pygame.draw.circle(self.screen, (255, 255, 0), (x, y), size, 2)
            pygame.draw.circle(self.screen, (255, 200, 0), (x, y), size - 10, 2)
            pygame.draw.circle(self.screen, (255, 150, 0), (x, y), size - 20, 2)
            
            # Mostra texto "Capturado!"
            if elapsed < 1.5:
                text = self.big_font.render("+100", True, (255, 255, 0))
                text_rect = text.get_rect(center=(x, y - 40))
                self.screen.blit(text, text_rect)
        
        # Desenha o painel de informações
        self.draw_info_panel()
        
        # Desenha a visão da câmera se ativada
        if self.rover_camera:
            self.draw_camera_view()
        
        # Se estiver pausado, desenha o indicador de pausa
        if self.paused:
            pause_text = self.big_font.render("SIMULAÇÃO PAUSADA", True, (255, 255, 255))
            text_rect = pause_text.get_rect(center=(WINDOW_WIDTH//2, WINDOW_HEIGHT//2))
            
            # Fundo semi-transparente
            overlay = pygame.Surface((WINDOW_WIDTH, WINDOW_HEIGHT), pygame.SRCALPHA)
            overlay.fill((0, 0, 0, 128))
            self.screen.blit(overlay, (0, 0))
            
            # Texto de pausa
            self.screen.blit(pause_text, text_rect)
        
        # Adiciona informação de pacotes recebidos
        if time.time() - self.last_packet_time > 5.0:
            warning_text = self.font.render("SEM COMUNICAÇÃO COM O PICO W", True, (255, 50, 50))
            self.screen.blit(warning_text, (WINDOW_WIDTH - 350, 50))
        
        # NOVO: Desenha o log de mensagens para depuração
        self.draw_message_log()
        
        # NOVO: Desenha o score e informações de captura
        self.draw_score_info()
        
        # Atualiza a tela
        pygame.display.flip()
    
    # NOVO: Desenha informações de score e pontos capturados
    def draw_score_info(self):
        """Desenha o score e informações sobre pontos capturados"""
        # Desenha o score no canto superior direito
        score_text = self.big_font.render(f"SCORE: {self.capture_score}", True, (255, 255, 100))
        self.screen.blit(score_text, (WINDOW_WIDTH - 180, 50))
        
        # Desenha contador de pontos capturados/total
        count_text = self.font.render(f"Pontos: {len(self.captured_poi)}/{len(self.poi)}", True, (200, 255, 200))
        self.screen.blit(count_text, (WINDOW_WIDTH - 180, 80))
        
        # Desenha instrução para o botão de captura
        if self.connected:
            help_text = self.font.render("Pressione o botão A no Pico W para capturar pontos", True, (200, 200, 255))
            self.screen.blit(help_text, (WINDOW_WIDTH // 2 - 180, WINDOW_HEIGHT - 30))
    
    def draw_message_log(self):
        """Desenha o log de mensagens para depuração"""
        if not self.message_log:
            return
            
        # Desenha no canto inferior esquerdo
        log_x = 10
        log_y = WINDOW_HEIGHT - 20 * len(self.message_log) - 10
        
        # Fundo semitransparente
        log_height = 20 * len(self.message_log)
        log_width = 400
        log_bg = pygame.Surface((log_width, log_height), pygame.SRCALPHA)
        log_bg.fill((0, 0, 0, 128))
        self.screen.blit(log_bg, (log_x-5, log_y-5))
        
        # Desenha as mensagens
        for i, message in enumerate(self.message_log):
            if message.startswith("TX:"):
                color = (100, 255, 100)  # Verde para mensagens enviadas
            else:
                color = (255, 200, 100)  # Laranja para mensagens recebidas
            
            # CORREÇÃO: Protege contra caracteres nulos
            try:
                msg_text = self.font.render(message, True, color)
                self.screen.blit(msg_text, (log_x, log_y + i * 20))
            except ValueError as e:
                # Se houver erro ao renderizar, cria um texto alternativo
                print(f"Erro ao renderizar mensagem: {e}")
                err_text = self.font.render("[Mensagem não renderizável]", True, (255, 100, 100))
                self.screen.blit(err_text, (log_x, log_y + i * 20))
    
    def draw_info_panel(self):
        """Desenha o painel de informações"""
        # Painel de fundo
        panel_rect = pygame.Rect(10, 10, 250, 180)
        pygame.draw.rect(self.screen, (30, 30, 30), panel_rect)
        pygame.draw.rect(self.screen, (100, 100, 100), panel_rect, 2)
        
        # Título
        title = self.big_font.render("ROVER STATUS", True, (255, 255, 255))
        self.screen.blit(title, (20, 15))
        
        # Linha separadora
        pygame.draw.line(self.screen, (100, 100, 100), (20, 45), (240, 45), 2)
        
        # Informações do rover
        y_pos = 55
        
        # Velocidade
        speed_text = self.font.render(f"Velocidade: {abs(self.rover_speed/MAX_SPEED*100):.1f}%", True, (255, 255, 255))
        self.screen.blit(speed_text, (20, y_pos))
        y_pos += 25
        
        # Direção
        dir_text = self.font.render(f"Direção: {self.rover_steering*100:.1f}%", True, (255, 255, 255))
        self.screen.blit(dir_text, (20, y_pos))
        y_pos += 25
        
        # Bateria
        bat_text = self.font.render(f"Bateria: {self.rover_battery:.1f}%", True, (255, 255, 255))
        bat_color = (0, 255, 0) if self.rover_battery > 50 else (255, 255, 0) if self.rover_battery > 20 else (255, 0, 0)
        
        # Barra de bateria
        bat_rect = pygame.Rect(130, y_pos + 5, 100, 10)
        bat_fill = pygame.Rect(130, y_pos + 5, self.rover_battery, 10)
        pygame.draw.rect(self.screen, (50, 50, 50), bat_rect)
        pygame.draw.rect(self.screen, bat_color, bat_fill)
        pygame.draw.rect(self.screen, (200, 200, 200), bat_rect, 1)
        
        self.screen.blit(bat_text, (20, y_pos))
        y_pos += 25
        
        # Temperatura
        temp_text = self.font.render(f"Temp: {self.rover_temperature:.1f}°C", True, (255, 255, 255))
        self.screen.blit(temp_text, (20, y_pos))
        y_pos += 25
        
        # Modo
        mode_names = ["Manual", "Semi-Auto", "Autônomo"]
        mode_text = self.font.render(f"Modo: {mode_names[self.rover_mode]}", True, (255, 255, 255))
        self.screen.blit(mode_text, (20, y_pos))
        y_pos += 25
        
        # Luzes e câmera
        light_img = self.light_on_img if self.rover_lights else self.light_off_img
        self.screen.blit(light_img, (20, y_pos))
        
        lights_text = self.font.render("Luzes", True, (255, 255, 255))
        self.screen.blit(lights_text, (45, y_pos))
        
        camera_img = self.light_on_img if self.rover_camera else self.light_off_img
        self.screen.blit(camera_img, (120, y_pos))
        
        camera_text = self.font.render("Câmera", True, (255, 255, 255))
        self.screen.blit(camera_text, (145, y_pos))
        
        # Conectividade
        if self.connected:
            # Verifica se a conexão está ativa (último pacote recebido nos últimos 2 segundos)
            if time.time() - self.last_packet_time < 2.0:
                status_text = self.font.render("Conectado", True, (50, 255, 50))
            else:
                status_text = self.font.render("Sem resposta...", True, (255, 200, 50))
        else:
            status_text = self.font.render("Desconectado", True, (255, 50, 50))
        
        # Mostra o status de conexão no canto superior direito
        self.screen.blit(status_text, (WINDOW_WIDTH - 150, 15))
        
        # Instruções
        if not self.connected:
            help_text = self.font.render("Aguardando conexão do Pico W...", True, (255, 255, 255))
            self.screen.blit(help_text, (WINDOW_WIDTH//2 - 150, WINDOW_HEIGHT - 30))
        
    def draw_camera_view(self):
        """Desenha a visão da câmera simulada"""
        # Posição da câmera no canto inferior direito
        x, y = WINDOW_WIDTH - 330, WINDOW_HEIGHT - 250
        
        # Fundo da câmera
        pygame.draw.rect(self.screen, (20, 20, 20), (x, y, 320, 240))
        pygame.draw.rect(self.screen, (100, 100, 100), (x, y, 320, 240), 2)
        
        # Captura uma seção da tela na frente do rover para simular a câmera
        # Calcula a posição para a captura baseada na direção do rover
        rad_angle = math.radians(self.rover_angle)
        cam_x = self.rover_x + math.sin(rad_angle) * 100
        cam_y = self.rover_y - math.cos(rad_angle) * 100
        
        # Região de captura
        capture_x = int(cam_x - 160)
        capture_y = int(cam_y - 120)
        
        # Certifica-se de que a região está dentro dos limites da tela
        if capture_x >= 0 and capture_y >= 0 and capture_x + 320 <= WINDOW_WIDTH and capture_y + 240 <= WINDOW_HEIGHT:
            try:
                # Captura a região
                capture_rect = pygame.Rect(capture_x, capture_y, 320, 240)
                capture = self.screen.subsurface(capture_rect).copy()
                
                # Aplica efeitos de câmera (podem ser ajustados para parecer mais realistas)
                # Adiciona ruído para simular uma câmera de baixa qualidade
                for _ in range(1000):
                    noise_x = random.randint(0, 319)
                    noise_y = random.randint(0, 239)
                    color = random.randint(0, 255)
                    capture.set_at((noise_x, noise_y), (color, color, color))
                
                # Desenha a imagem capturada
                self.screen.blit(capture, (x, y))
            except ValueError:
                # Fora dos limites da tela, mostra estática
                self.draw_camera_static(x, y)
        else:
            # Fora dos limites da tela, mostra estática
            self.draw_camera_static(x, y)
        
        # Adiciona overlay com informações da câmera
        overlay_text = self.font.render("CAM01 - ROVER", True, (0, 255, 0))
        self.screen.blit(overlay_text, (x + 10, y + 10))
        
        # Data e hora
        time_str = time.strftime("%d/%m/%Y %H:%M:%S")
        time_text = self.font.render(time_str, True, (0, 255, 0))
        self.screen.blit(time_text, (x + 10, y + 210))
    
    def draw_camera_static(self, x, y):
        """Desenha estática para a câmera quando fora do alcance"""
        for _ in range(5000):
            noise_x = random.randint(0, 319)
            noise_y = random.randint(0, 239)
            color = random.randint(0, 255)
            try:
                self.screen.set_at((x + noise_x, y + noise_y), (color, color, color))
            except IndexError:
                pass  # Ignora erros de índice
    
    def handle_events(self):
        """Processa eventos do pygame"""
        global USAR_PROTOCOLO_SIMPLES  # Declaração global adicionada
        
        for event in pygame.event.get():
            if event.type == QUIT:
                self.running = False
                return False
            
            elif event.type == KEYDOWN:
                # Tecla P pausa/despausa a simulação
                if event.key == K_p:
                    self.paused = not self.paused
                    print(f"Simulação {'pausada' if self.paused else 'retomada'}")
                
                # Tecla R recarrega a bateria
                elif event.key == K_r:
                    self.rover_battery = 100.0
                    print("Bateria recarregada")
                
                # Tecla ESC sai da aplicação
                elif event.key == K_ESCAPE:
                    self.running = False
                    return False
                
                # NOVO: Tecla C para simular captura (para testes)
                elif event.key == K_SPACE:
                    print("Simulando captura manual")
                    self.capture_requested = True
                
                # Teclas de função para modos
                elif event.key == K_F1:
                    self.rover_mode = MODE_MANUAL
                    print("Modo Manual ativado")
                elif event.key == K_F2:
                    self.rover_mode = MODE_SEMI_AUTO
                    print("Modo Semi-Autônomo ativado")
                elif event.key == K_F3:
                    self.rover_mode = MODE_AUTONOMOUS
                    print("Modo Autônomo ativado")
                
                # Teclas L e C para luzes e câmera
                elif event.key == K_l:
                    self.rover_lights = not self.rover_lights
                    print(f"Luzes {'ligadas' if self.rover_lights else 'desligadas'}")
                elif event.key == K_c:
                    self.rover_camera = not self.rover_camera
                    print(f"Câmera {'ligada' if self.rover_camera else 'desligada'}")
                
                # Tecla T para simular recepção de pacote (para testes)
                elif event.key == K_t:
                    print("Simulando recepção de pacote do Pico W")
                    
                    if USAR_PROTOCOLO_SIMPLES:
                        # Simula um pacote de texto
                        test_packet = "speed=50.0,steering=25.0,mode=0,lights=on,camera=off"
                        self.add_to_message_log(f"RX (sim): {test_packet}")
                        
                        # Extrai valores
                        values = {}
                        pairs = test_packet.split(",")
                        for pair in pairs:
                            key, value = pair.split("=", 1)
                            if key in ["speed", "steering"]:
                                values[key] = float(value)
                            elif key == "mode":
                                values[key] = int(value)
                            elif key in ["lights", "camera"]:
                                values[key] = value == "on"
                        
                        # Atualiza o rover
                        self.rover_speed = values["speed"] / 100.0 * MAX_SPEED
                        self.rover_steering = values["steering"] / 100.0
                        self.rover_mode = values["mode"]
                        self.rover_lights = values["lights"]
                        self.rover_camera = values["camera"]
                    else:
                        # Simula dados do joystick
                        fake_joystick_data = (0.5, -0.5, False, False, False, 0)
                        # Simula dados do rover
                        fake_rover_data = (50.0, 25.0, 80.0, 30.0, MODE_MANUAL, True, False, 0)
                        # Atualiza o estado do rover
                        self.update_from_controller(fake_joystick_data, fake_rover_data)
                    
                    # Atualiza timestamp
                    self.last_packet_time = time.time()
                    self.connected = True
                    print("Pacote simulado processado!")
                    
                    # Envia uma resposta
                    if USAR_PROTOCOLO_SIMPLES:
                        self.send_status_text()
                    else:
                        self.send_status()
                
                # Tecla I para mostrar informações sobre o formato dos pacotes
                elif event.key == K_i:
                    print("\n=== INFORMAÇÕES DE FORMATO DOS PACOTES ===")
                    print(f"Formato JOYSTICK: {JOYSTICK_FORMAT}, Tamanho: {JOYSTICK_SIZE} bytes")
                    print(f"Formato ROVER: {ROVER_FORMAT}, Tamanho: {ROVER_SIZE} bytes")
                    print(f"Tamanho total do pacote esperado: {4 + JOYSTICK_SIZE + ROVER_SIZE} bytes")
                    print("Pressione T para simular recepção de um pacote")
                    print("=======================================\n")
                
                # Tecla M para alternar entre protocolo simples e binário
                elif event.key == K_m:
                    USAR_PROTOCOLO_SIMPLES = not USAR_PROTOCOLO_SIMPLES
                    print(f"Usando protocolo {'simples (texto)' if USAR_PROTOCOLO_SIMPLES else 'binário'}")
        
        return True
    
    def run(self):
        """Loop principal da simulação"""
        global LINK_OK, LAST_RX  # Declaração global adicionada
        try:
            while self.running:
                # Processa eventos
                if not self.handle_events():
                    break
                
                # Atualiza a simulação
                self.update()
                
                # Desenha a tela
                self.draw()
                
                # Limita a taxa de quadros
                self.clock.tick(60)

                if LINK_OK and time.time() - LAST_RX > HELLO_TIMEOUT:
                    print("Link perdido – aguardando novo HELLO")
                    LINK_OK = False

        finally:
            # CORREÇÃO: Marca o programa como não executando para threads
            self.running = False
            
            # CORREÇÃO: Fecha o socket com segurança
            with self.socket_lock:
                try:
                    self.udp_socket.close()
                except:
                    pass
            
            # Limpa recursos
            pygame.quit()
            print("Simulação encerrada")

if __name__ == "__main__":
    # Exibe informações de inicialização
    print("===== Rover Simulator =====")
    print("Iniciando simulação...")
    print("Aguardando conexão do Raspberry Pi Pico W...")
    print("")
    print("Controles:")
    print("P - Pausa/Despausa")
    print("R - Recarrega bateria")
    print("L - Liga/Desliga luzes")
    print("C - Liga/Desliga câmera")
    print("ESPAÇO - Simula captura de ponto (teste)")
    print("F1 - Modo Manual")
    print("F2 - Modo Semi-Autônomo")
    print("F3 - Modo Autônomo")
    print("T - Simula recepção de pacote (para testes)")
    print("I - Mostra informações de formato dos pacotes")
    print("M - Alterna entre protocolo simples (texto) e binário")
    print("ESC - Sair")
    print(f"Usando inicialmente protocolo {'simples (texto)' if USAR_PROTOCOLO_SIMPLES else 'binário'}")
    print("=========================")
    
    # Inicia o simulador
    simulator = RoverSimulator()
    simulator.run()