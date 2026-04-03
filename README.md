# nRF52840 DK - BLE Beacon + Scanner

Firmware para a **nRF52840 DK** com dois modos de operação: **iBeacon** (transmissor) e **Scanner BLE** (receptor), com saída via UART (PuTTY).

## O que faz

```
MODO BEACON:   nRF52840 ──iBeacon──> Qualquer scanner BLE (celular, outro nRF, etc)
MODO SCANNER:  Dispositivos BLE ──> nRF52840 ──UART──> PuTTY (lista dispositivos encontrados)
```

- **Modo Beacon**: Transmite pacotes iBeacon com UUID, Major e Minor configuráveis
- **Modo Scanner**: Escaneia o ambiente e mostra todos os dispositivos BLE via UART
- Detecta e destaca **iBeacons** encontrados durante o scan

### Controles (Botões)

| Botão | Função |
|---|---|
| **Botão 1** | Alterna entre modo Beacon ↔ Scanner |
| **Botão 2** | Scanner: inicia/para scan · Beacon: incrementa Minor |
| **Botão 3** | Reseta contador e inicia novo scan |
| **Botão 4** | Mostra status atual no PuTTY |

### LEDs de Status

| LED | Significado |
|---|---|
| LED1 piscando | Firmware rodando normalmente |
| LED2 aceso | Modo Beacon ativo |
| LED3 aceso | Modo Scanner ativo |
| LED4 pisca | Dispositivo BLE encontrado (scanner) |

## Demonstração

Ao gravar o firmware e abrir o PuTTY, você verá:

```
==========================================
  nRF52840 BLE Beacon + Scanner
==========================================
Botao 1: Alterna Beacon <-> Scanner
Botao 2: Start/Stop scan | Muda Minor
Botao 3: Reset scan + novo scan
Botao 4: Mostra status
==========================================

Iniciando modo Beacon...

========== MODO BEACON ==========
iBeacon ativo!
UUID: E2C56DB5-DFFB-48D2-B060-D0F5A71096E0
Major: 1  Minor: 1
TX Power: -59 dBm (1m referencia)
==================================
```

Ao pressionar **Botão 1** para modo Scanner:

```
========= MODO SCANNER ==========
Escaneando dispositivos BLE...
Intervalo: 100 ms  Janela: 50 ms
Filtro: duplicados removidos
==================================

[  1] AA:BB:CC:DD:EE:FF (pub) RSSI:-45 dBm  "Mi Band 7"
[  2] 11:22:33:44:55:66 (rnd) RSSI:-62 dBm
[  3] 77:88:99:AA:BB:CC (pub) RSSI:-38 dBm ** iBEACON ** Major:1 Minor:5 TxPwr:-59
```

## Hardware

| Componente | Descrição |
|---|---|
| **Placa** | nRF52840 DK (PCA10056) |
| **MCU** | nRF52840 (ARM Cortex-M4F, 64 MHz, 1 MB Flash, 256 KB RAM) |
| **Radio** | Bluetooth 5.0 / BLE integrado |
| **Interface UART** | VCOM via J-Link USB onboard |

## Pré-requisitos

### Software

- [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-SDK) v3.x (testado com v3.0.2)
- [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) com Toolchain Manager
- Terminal serial: [PuTTY](https://www.putty.org/), minicom ou Tera Term

### Hardware

- nRF52840 DK (PCA10056) conectada via USB
- (Opcional) Celular com app **nRF Connect** para verificar o beacon

## Estrutura do Projeto

```
nrf52840_ble_beacon_scanner/
├── CMakeLists.txt      # Configuração de build CMake/Zephyr
├── prj.conf            # Configuração do kernel (Kconfig)
├── app.overlay         # Overlay do devicetree (define UART)
├── src/
│   └── main.c          # Código fonte principal
├── .gitignore
├── LICENSE
└── README.md
```

## Como Compilar e Gravar

### 1. Copie o projeto para um caminho sem espaços

```bash
xcopy /E /I "caminho_original" C:\ncs\projects\nrf52840_ble_beacon_scanner
```

### 2. Compile

```bash
west build -b nrf52840dk/nrf52840 C:\ncs\projects\nrf52840_ble_beacon_scanner --no-sysbuild --build-dir C:\ncs\projects\nrf52840_ble_beacon_scanner\build
```

> **Nota:** O `--no-sysbuild` é necessário no nRF Connect SDK v3.x para projetos standalone.

### 3. Grave na placa

```bash
west flash --runner jlink --build-dir C:\ncs\projects\nrf52840_ble_beacon_scanner\build
```

## Como Usar

### 1. Configurar o PuTTY

| Parâmetro | Valor |
|---|---|
| **Connection type** | Serial |
| **Porta** | COMx (varia por PC) |
| **Baud Rate** | 115200 |
| **Data Bits** | 8 |
| **Stop Bits** | 1 |
| **Parity** | None |
| **Flow Control** | None |

### 2. Testar modo Beacon

1. O firmware inicia automaticamente em modo **Beacon**
2. No celular, abra o app **nRF Connect** e faça **Scan**
3. Procure por **"nRF52840_Beacon"** — o dispositivo deve aparecer como iBeacon
4. Pressione **Botão 2** na DK para incrementar o Minor e verificar a mudança no app

### 3. Testar modo Scanner

1. Pressione **Botão 1** para alternar para modo Scanner
2. O nRF52840 começa a escanear e os dispositivos aparecem no PuTTY
3. iBeacons são destacados com `** iBEACON **` e seus dados (Major/Minor)
4. Pressione **Botão 2** para parar/reiniciar o scan
5. Pressione **Botão 4** para ver o status e contagem de dispositivos

### 4. Verificar com outro nRF52840

Se tiver duas DKs:
1. **DK1**: Modo Beacon (transmitindo)
2. **DK2**: Modo Scanner (escaneando)
3. A DK2 deve detectar a DK1 como iBeacon no PuTTY

## Detalhes Técnicos

### iBeacon Format

```
| Flags | Manufacturer Data (Apple 0x004C)                           |
|       | Type | Len | UUID (16 bytes) | Major | Minor | TX Power   |
|  0x06 | 0x02 | 0x15| E2C56DB5-...    | 0x0001| 0x0001| 0xC5(-59) |
```

### Configurações de Scan

| Parâmetro | Valor |
|---|---|
| **Tipo** | Passivo (não envia scan request) |
| **Intervalo** | 100 ms |
| **Janela** | 50 ms |
| **Filtro** | Duplicados removidos |

### Configurações Kconfig (prj.conf)

```ini
CONFIG_BT=y                    # Habilita stack BLE
CONFIG_BT_OBSERVER=y           # Permite escanear (role Observer)
CONFIG_BT_BROADCASTER=y        # Permite transmitir (role Broadcaster)
CONFIG_BT_DEVICE_NAME="nRF52840_Beacon"

CONFIG_SERIAL=y                # UART para saída no PuTTY
CONFIG_UART_ASYNC_API=y        # API assíncrona
CONFIG_DK_LIBRARY=y            # LEDs e botões

CONFIG_LOG=y                   # Logging via RTT
CONFIG_USE_SEGGER_RTT=y
CONFIG_UART_CONSOLE=n          # Console NÃO pela UART (reservada para dados)
```

## Troubleshooting

| Problema | Solução |
|---|---|
| Beacon não aparece no celular | Verifique se está no modo Beacon (LED2 aceso). Resete a DK |
| Scanner não mostra dispositivos | Certifique-se que há dispositivos BLE por perto. Verifique LED3 |
| PuTTY não mostra nada | Verifique porta COM e baud rate 115200 |
| Erro de build com espaços no path | Copie para `C:\ncs\projects\` e use `--build-dir` |
| `west: command not found` | Use o terminal do Toolchain Manager |

## Licença

MIT License - veja [LICENSE](LICENSE)
