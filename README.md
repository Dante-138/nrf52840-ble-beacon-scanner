<div align="center">

# nRF52840 BLE Beacon + Scanner

**Embedded firmware for nRF52840 DK — iBeacon Transmitter & BLE Device Scanner**

![Zephyr](https://img.shields.io/badge/Zephyr_RTOS-v3.7-blue?logo=zephyr&logoColor=white)
![nRF Connect SDK](https://img.shields.io/badge/nRF_Connect_SDK-v3.0.2-00A9CE?logo=nordicsemiconductor&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-green)
![Platform](https://img.shields.io/badge/Platform-nRF52840_DK-orange)
![BLE](https://img.shields.io/badge/Bluetooth-5.0_LE-0082FC?logo=bluetooth&logoColor=white)

*Dual-mode firmware that turns the nRF52840 DK into an iBeacon transmitter or a BLE device scanner, with real-time output via UART.*

</div>

---

## Overview (English)

Bare-metal C firmware running on **Zephyr RTOS** for the Nordic **nRF52840 DK**. The device operates in two switchable modes controlled by hardware buttons:

- **Beacon Mode** — Transmits Apple-format iBeacon advertising packets (UUID, Major, Minor, TX Power)
- **Scanner Mode** — Passively scans all nearby BLE devices and outputs a formatted list via UART (PuTTY / serial terminal)

### Key Technical Highlights

- **RTOS threading** — Producer-consumer pattern with `K_MSGQ` (message queue) + `K_SEM` (semaphore)
- **Async UART with DMA** — Non-blocking serial output, race-condition-free
- **BLE advertising & scanning** — Full iBeacon format, passive scan with HW + SW duplicate filtering
- **Hardware interrupts** — GPIO edge-triggered callbacks for 4 buttons
- **Modular architecture** — Clean separation between BLE radio, data processing, and UART output

### Screenshots

<p align="center">
  <img src="docs/scanner_putty.png" width="420" alt="Scanner Mode — PuTTY UART output"/>
  &nbsp;&nbsp;
  <img src="docs/beacon_nrf_connect.png" width="280" alt="Beacon Mode — nRF Connect App"/>
</p>

<p align="center">
  <em>Left: Scanner mode listing 21 BLE devices via UART &nbsp;|&nbsp; Right: iBeacon detected on nRF Connect mobile app</em>
</p>

---

## Visão Geral

Firmware completo para a **nRF52840 DK** com dois modos de operação alternáveis por botão:

| Modo | Função | Analogia |
|------|--------|----------|
| **Beacon** | Transmite pacotes iBeacon detectáveis por qualquer dispositivo BLE | Como um farol emitindo sinal para quem estiver perto |
| **Scanner** | Escaneia e lista todos os dispositivos BLE ao redor via UART | Como um radar que detecta tudo em volta |

```
+-------------------------------------------------------------------+
|                       nRF52840 DK                                 |
|                                                                   |
|  MODO BEACON:   nRF52840 ---- iBeacon ----> Celulares / Apps     |
|  MODO SCANNER:  Dispositivos BLE --> nRF52840 --UART--> PuTTY    |
|                                                                   |
|  [BTN1] Alterna modo   [BTN2] Ação contextual                    |
|  [BTN3] Reset scan     [BTN4] Status                             |
|  (LED1) Running  (LED2) Beacon  (LED3) Scanner  (LED4) Found     |
+-------------------------------------------------------------------+
```

## Demonstração

### Modo Beacon — nRF Connect App (celular)

O beacon é detectado como **iBeacon** pelo app nRF Connect, exibindo UUID, Major, Minor e TX Power:

```
nRF52840_Beacon  (iBeacon)
+-- Device type: LE only
+-- Advertising type: Legacy
+-- Beacon:
|   +-- Company: Apple, Inc. <0x004C>
|   +-- Type: Beacon <0x02>
|   +-- UUID: e2c56db5-dffb-48d2-b060-d0f5a71096e0
|   +-- Major: 1
|   +-- Minor: 1
|   +-- RSSI at 1m: -59 dBm
+-- Complete Local Name: nRF52840_Beacon
```

### Modo Scanner — PuTTY (UART serial)

```
==========================================
  nRF52840 BLE Beacon + Scanner
==========================================
Botao 1: Alterna Beacon <-> Scanner
Botao 2: Start/Stop scan | Muda Minor
Botao 3: Reset scan + novo scan
Botao 4: Mostra status
==========================================

========= MODO SCANNER ==========
Escaneando dispositivos BLE...
Intervalo: 100 ms  Janela: 50 ms
Filtro: duplicados removidos
==================================

[  1] 23:08:30:10:00:3F (pub) RSSI:-71 dBm
[  2] CD:EB:7A:A7:DF:BC (rnd) RSSI:-83 dBm
[  3] 63:B0:6A:7D:82:B5 (rnd) RSSI:-37 dBm  "Mi Band 7"
[  4] 78:A7:A1:23:C9:FC (rnd) RSSI:-81 dBm
[  5] C7:4C:58:7A:D1:1F (rnd) RSSI:-65 dBm
[  6] EC:22:B3:4D:01:A4 (rnd) RSSI:-70 dBm
...

--- Scan parado. 23 dispositivos encontrados ---
```

## Arquitetura do Firmware

```
                +------------------------+
                |    BLE Radio (HW)      |
                +-----------+------------+
                            | advertising packets
                +-----------v------------+
                | scan_cb() [BLE Thread] |  <-- Não bloqueia!
                | - Filtra duplicados    |
                | - Parse ad data        |
                | - Enfileira mensagem   |
                +-----------+------------+
                            | K_MSGQ (message queue)
                +-----------v------------+
                | print_thread [Prio 8]  |  <-- Thread dedicada
                | - Lê da fila           |
                | - Formata string       |
                | - Envia pela UART      |
                +-----------+------------+
                            | K_SEM (semáforo TX done)
                +-----------v------------+
                |   UART DMA (Hardware)  |
                +-----------+------------+
                            |
                +-----------v------------+
                |   PuTTY / Terminal     |
                +------------------------+
```

**Por que essa arquitetura?** O BLE radio gera centenas de callbacks por segundo. Se o callback tentasse enviar diretamente pela UART, o buffer seria sobrescrito antes de completar o envio (texto embaralhado) e a thread BLE ficaria travada. A solução usa **producer-consumer**: o callback apenas enfileira (rápido), e uma thread separada consome e imprime (pode bloquear sem problema).

## Controles

### Botões

| Botão | Função |
|-------|--------|
| **Botão 1** | Alterna entre modo Beacon <-> Scanner |
| **Botão 2** | Scanner: inicia/para scan · Beacon: incrementa Minor |
| **Botão 3** | Reseta contador e inicia novo scan |
| **Botão 4** | Mostra status atual no PuTTY |

### LEDs

| LED | Significado |
|-----|-------------|
| LED1 piscando | Firmware rodando normalmente |
| LED2 aceso | Modo Beacon ativo (transmitindo) |
| LED3 aceso | Modo Scanner ativo (escaneando) |
| LED4 pisca | Dispositivo BLE encontrado |

## Hardware

| Componente | Descrição |
|------------|-----------|
| **Placa** | nRF52840 DK (PCA10056) |
| **MCU** | nRF52840 — ARM Cortex-M4F, 64 MHz, 1 MB Flash, 256 KB RAM |
| **Radio** | Bluetooth 5.0 / BLE integrado |
| **Interface UART** | J-Link VCOM via USB onboard (115200 baud) |

## Pré-requisitos

- [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-SDK) v3.x (testado com v3.0.2)
- [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) com Toolchain Manager
- Terminal serial: [PuTTY](https://www.putty.org/), minicom ou Tera Term
- nRF52840 DK (PCA10056) conectada via USB
- (Opcional) Celular com app **nRF Connect** para verificar o beacon

## Estrutura do Projeto

```
nrf52840_ble_beacon_scanner/
├── CMakeLists.txt          # Configuração de build (CMake/Zephyr)
├── prj.conf                # Configuração do kernel (Kconfig)
├── app.overlay             # Overlay do devicetree (mapeamento UART)
├── src/
│   └── main.c              # Código fonte do firmware (~600 linhas)
├── .gitignore
├── LICENSE                  # MIT
└── README.md
```

## Build & Flash

### 1. Clone o repositório

```bash
git clone https://github.com/Dante-138/nrf52840-ble-beacon-scanner.git
```

### 2. Copie para um caminho sem espaços (necessário para CMake/GCC)

```bash
xcopy /E /I nrf52840-ble-beacon-scanner C:\ncs\projects\nrf52840_ble_beacon_scanner
```

### 3. Compile (no terminal do nRF Connect SDK Toolchain)

```bash
west build -b nrf52840dk/nrf52840 C:\ncs\projects\nrf52840_ble_beacon_scanner ^
  --no-sysbuild --build-dir C:\ncs\projects\nrf52840_ble_beacon_scanner\build --pristine
```

> **Nota:** O `--no-sysbuild` é necessário no nRF Connect SDK v3.x para projetos standalone.

### 4. Grave na placa

```bash
west flash --runner jlink --build-dir C:\ncs\projects\nrf52840_ble_beacon_scanner\build
```

## Como Testar

### Testar modo Beacon

1. O firmware inicia automaticamente em modo **Beacon** (LED2 aceso)
2. No celular, abra o app **nRF Connect** e faça **Scan**
3. Procure por **"nRF52840_Beacon"** — aparecerá como iBeacon com UUID, Major e Minor
4. Pressione **Botão 2** na DK para incrementar o Minor e ver a mudança no app

### Testar modo Scanner

1. Pressione **Botão 1** para alternar para modo Scanner (LED3 aceso)
2. Abra o **PuTTY** (Serial, 115200 baud, porta COMx)
3. Os dispositivos BLE ao redor aparecem listados em tempo real
4. iBeacons são destacados com `** iBEACON **` e seus dados
5. Pressione **Botão 2** para parar/reiniciar o scan
6. Pressione **Botão 4** para ver o status

### Testar com dois nRF52840 DKs

1. **DK1**: Modo Beacon (transmitindo)
2. **DK2**: Modo Scanner (escaneando)
3. A DK2 detecta a DK1 como iBeacon no PuTTY

## Detalhes Técnicos

### Formato iBeacon

```
+-------+------------------------------------------------------------+
| Flags | Manufacturer Data (Apple 0x004C)                           |
|       | Type | Len  | UUID (16 bytes)  | Major  | Minor | TX Pwr  |
| 0x06  | 0x02 | 0x15 | E2C56DB5-...     | 0x0001 | 0x0001| -59 dBm |
+-------+------------------------------------------------------------+
```

### Configurações de Scan

| Parâmetro | Valor |
|-----------|-------|
| **Tipo** | Passivo (não envia scan request) |
| **Intervalo** | 100 ms (0x00A0) |
| **Janela** | 50 ms (0x0050) |
| **Filtro HW** | BT_LE_SCAN_OPT_FILTER_DUPLICATE |
| **Filtro SW** | Tabela de 64 endereços MAC únicos |

### Stack Tecnológico

| Camada | Tecnologia |
|--------|------------|
| **RTOS** | Zephyr RTOS v3.7 |
| **SDK** | nRF Connect SDK v3.0.2 |
| **BLE Stack** | Zephyr Bluetooth (SoftDevice Controller) |
| **UART** | Async API com DMA |
| **Concorrência** | K_MSGQ + K_SEM + K_THREAD |
| **Build** | CMake + Ninja via West |

### Configurações Kconfig (prj.conf)

```ini
CONFIG_BT=y                    # Habilita stack BLE
CONFIG_BT_OBSERVER=y           # Role: Observer (scan)
CONFIG_BT_BROADCASTER=y        # Role: Broadcaster (advertise)
CONFIG_BT_DEVICE_NAME="nRF52840_Beacon"

CONFIG_SERIAL=y                # UART para saída no PuTTY
CONFIG_UART_ASYNC_API=y        # API assíncrona com DMA
CONFIG_DK_LIBRARY=y            # Abstrai LEDs e botões do DK

CONFIG_LOG=y                   # Logging via RTT (não pela UART)
CONFIG_USE_SEGGER_RTT=y
CONFIG_UART_CONSOLE=n          # UART reservada para dados, não console
```

## Changelog

### v2.0 — Correções de estabilidade e UX

**UART Race Condition (texto embaralhado)**
- **Problema**: Buffer global único era sobrescrito antes da UART completar o envio
- **Causa raiz**: `scan_cb` chamava `uart_send` direto da thread BLE RX
- **Solução**: Desacoplamento via `K_MSGQ` (message queue). Callback enfileira, thread dedicada imprime. UART serializada por semáforo `uart_tx_done`

**Dispositivos duplicados no scan**
- **Problema**: Filtro do controller BLE tem capacidade limitada (~400+ duplicatas)
- **Solução**: Filtro por software com tabela de até 64 MACs. Limpa a cada novo scan

**Beacon sem nome no nRF Connect (mostrava "N/A")**
- **Problema**: Advertising era non-connectable non-scannable, sem scan response
- **Solução**: Mudou para scannable advertising com scan response contendo `BT_DATA_NAME_COMPLETE`

**Tipo de endereço duplicado no output**
- **Problema**: `bt_addr_le_to_str()` já incluía `(random)`, e `addr_type_str()` adicionava `(rnd)`
- **Solução**: Trocou para `bt_addr_to_str()` (só MAC) + `addr_type_str()` (só tipo)

### v1.0 — Release inicial

- Modo Beacon com iBeacon (UUID Apple AirLocate)
- Modo Scanner com output UART
- Controle por 4 botões + 4 LEDs de status

## Troubleshooting

| Problema | Solução |
|----------|---------|
| Beacon não aparece no celular | Verifique se está no modo Beacon (LED2 aceso). Resete a DK |
| Scanner não mostra dispositivos | Certifique-se que há dispositivos BLE por perto. Verifique LED3 |
| PuTTY não mostra nada | Verifique porta COM e baud rate 115200. Flow Control = None |
| Erro de build com espaços no path | Copie para `C:\ncs\projects\` e use `--build-dir` |
| `west: command not found` | Use o terminal do Toolchain Manager (não PowerShell direto) |

## Licença

MIT License — veja [LICENSE](LICENSE)

---

<div align="center">

**Desenvolvido com** Nordic nRF52840 DK **+** Zephyr RTOS **+** nRF Connect SDK

</div>
